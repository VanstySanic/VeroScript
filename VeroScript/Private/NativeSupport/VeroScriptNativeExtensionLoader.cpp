// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptNativeExtensionLoader.h"
#include "ModuleSupport/VeroScriptModuleSupport.h"
#include "VeroScriptRuntime.h"
#include <array>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>
#include <type_traits>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif !defined(__EMSCRIPTEN__) && !VEROSCRIPT_PLATFORM_IOS
#include <dlfcn.h>
#endif

namespace VeroScript
{
	static_assert(std::is_standard_layout_v<FNativeExtensionDescriptor> && std::is_trivially_copyable_v<FNativeExtensionDescriptor>);
	struct FLoadedNativeExtension::FImpl
	{
		~FImpl()
		{
			#if defined(_WIN32)
			if (Handle)
			{
				FreeLibrary(Handle);
			}

			#elif !defined(__EMSCRIPTEN__) && !VEROSCRIPT_PLATFORM_IOS
			if (Handle)
			{
				dlclose(Handle);
			}

			#endif
		}

		std::shared_ptr<const void> Owner, FileLease;
		FNativeExtensionDescriptor Descriptor;
		std::string Path, ContentIdentity;
		#if defined(_WIN32)
		HMODULE Handle = nullptr;
		#else
		void* Handle = nullptr;
		#endif
	};
	namespace
	{
		struct FModuleCache
		{
			std::mutex Mutex;
			std::map<std::string, std::weak_ptr<const FLoadedNativeExtension>> Modules;
		};
		FModuleCache& Cache()
		{
			// Weak bookkeeping never owns module code or file leases. A host may
			// call the loader from constructors/destructors of global objects.
			static auto* State = new FModuleCache;
			return *State;
		}
		template <std::size_t N>
		bool Field(const char (&V)[N])
		{
			return std::memchr(V, 0, N) && ModuleSupport::Text(std::string(V));
		}
		bool Validate(const FNativeExtensionDescriptor& D, const FNativeExtensionLoadOptions& O, std::string& Error)
		{
			if (D.StructSize != sizeof(D) || D.ApiVersion != NativeExtensionApiVersion || !Field(D.Abi) ||
			    !Field(D.CoreBuild) || !Field(D.Platform) || !Field(D.Id) || !Field(D.Version) || !D.Roles ||
			    (D.Roles & ~15u) || (D.Flags & ~NativeExtensionParallelRegistrations) || !D.Create ||
			    !D.Destroy)
			{
				Error = "invalid native extension descriptor";
				return false;
			}
			if (D.Abi != BuildNativeExtensionAbi() ||
			    std::strcmp(D.CoreBuild, VEROSCRIPT_CORE_IMPLEMENTATION_ID) ||
			    std::strcmp(D.Platform, GetHostPlatformInfo().Platform))
			{
				Error = "native extension ABI/build/platform mismatch: expected " +
				    BuildNativeExtensionAbi() + ", actual " + D.Abi;
				return false;
			}
			if ((!O.ExpectedId.empty() && O.ExpectedId != D.Id) ||
			    (!O.ExpectedVersion.empty() && O.ExpectedVersion != D.Version) ||
			    (O.ExpectedRoles && O.ExpectedRoles != D.Roles))
			{
				Error = "native extension identity or role disagrees with its declared contract";
				return false;
			}
			Error.clear();
			return true;
		}
		bool Query(FNativeExtensionQuery Function, FNativeExtensionDescriptor& D, const FNativeExtensionLoadOptions& O, std::string& Error)
		{
			if (O.CheckCancelled)
			{
				O.CheckCancelled();
			}

			if (!Function || Function(NativeExtensionApiVersion, sizeof(D), &D) != NativeExtensionApiVersion)
			{
				Error = "native extension does not support protocol 0.1.1";
				return false;
			}
			return Validate(D, O, Error);
		}
	}
	FLoadedNativeExtension::~FLoadedNativeExtension() = default;
	const FNativeExtensionDescriptor& FLoadedNativeExtension::GetDescriptor() const noexcept
	{
		return Impl->Descriptor;
	}
	const std::string& FLoadedNativeExtension::GetPath() const noexcept
	{
		return Impl->Path;
	}
	const std::string& FLoadedNativeExtension::GetContentIdentity() const noexcept
	{
		return Impl->ContentIdentity;
	}
	FLoadedNativeExtension::FLoadedNativeExtension(std::shared_ptr<FImpl> P) : Impl(std::move(P))
	{
	}
	std::shared_ptr<const FLoadedNativeExtension> QueryNativeExtension(FNativeExtensionQuery Function,
	    std::shared_ptr<const void> Owner, std::string& Error, FNativeExtensionLoadOptions O)
	try
	{
		auto P = std::make_shared<FLoadedNativeExtension::FImpl>();
		// Parameters outlive function-try handlers; retain their leases while
		// reading and destroying an exception implemented by the extension.
		P->Owner = Owner;
		P->FileLease = O.FileLease;
		P->ContentIdentity = O.ContentIdentity;
		if (!Query(Function, P->Descriptor, O, Error)) return {};
		return std::shared_ptr<const FLoadedNativeExtension>(new FLoadedNativeExtension(std::move(P)));
	}
	catch (const std::exception& E)
	{
		Error = E.what();
		return {};
	}
	catch (...)
	{
		Error = "native extension query threw an exception";
		return {};
	}
	std::shared_ptr<const FLoadedNativeExtension> LoadNativeExtension(
	    const std::string& Path, std::string& Error, FNativeExtensionLoadOptions O)
	{
		// Keep the library mapped until the caught exception has been destroyed.
		std::shared_ptr<FLoadedNativeExtension::FImpl> P;
		try
		{
			Error.clear();
			const auto File = ModuleSupport::Physical(Path);
			if (File.empty())
			{
				Error = "native extension requires an explicit absolute file";
				return {};
			}
			#if defined(__EMSCRIPTEN__) || VEROSCRIPT_PLATFORM_IOS
			Error = "dynamic native extensions are unavailable on this platform; use static registration";
			return {};
			#endif
			#if defined(_WIN32) && !defined(_DLL)
			Error = "dynamic C++ extensions require the shared MSVC runtime; use static registration for a static CRT host";
			return {};
			#endif
			P = std::make_shared<FLoadedNativeExtension::FImpl>();
			P->FileLease = std::move(O.FileLease);
			P->Path = File;
			std::ifstream Input(PathUtil::FromUtf8(File), std::ios::binary | std::ios::ate);
			const auto Size = Input ? Input.tellg() : std::streampos{-1};
			if (Size <= 0 || static_cast<std::uint64_t>(Size) > 1024ull * 1024 * 1024)
			{
				Error = "cannot read native extension file or file exceeds limit";
				return {};
			}
			Input.seekg(0);
			std::uint64_t Hash = 14695981039346656037ull;
			std::array<char, 65536> Bytes{};
			std::uint64_t Remaining = static_cast<std::uint64_t>(Size);
			while (Remaining)
			{
				if (O.CheckCancelled)
				{
					O.CheckCancelled();
				}

				const auto Count = static_cast<std::streamsize>(std::min<std::uint64_t>(Remaining, Bytes.size()));
				if (!Input.read(Bytes.data(), Count))
				{
					Error = "native extension changed while hashing";
					return {};
				}
				Hash = Runtime::Fnv1a64(Hash, Bytes.data(), static_cast<std::size_t>(Count));
				Remaining -= static_cast<std::uint64_t>(Count);
			}
			if (Input.peek() != std::char_traits<char>::eof())
			{
				Error = "native extension changed while hashing";
				return {};
			}
			// This is a cache identity. Authentication uses the caller's verified
			// artifact/lease; FNV is not advertised as a cryptographic signature.
			P->ContentIdentity = "binary-fnv64:" + std::to_string(Hash);
			const auto Key = PathUtil::IdentityKey(File);
			auto& Modules = Cache().Modules;
			{
				std::lock_guard<std::mutex> Lock(Cache().Mutex);
				const auto Found = Modules.find(Key);
				if (auto Existing = Found == Modules.end() ? nullptr : Found->second.lock())
				{
					if (Existing->GetContentIdentity() != P->ContentIdentity)
					{
						Error = "loaded native library was modified; use an immutable new path or restart";
						return {};
					}
					if (!Validate(Existing->GetDescriptor(), O, Error)) return {};
					return Existing;
				}
			}
			FNativeExtensionQuery Function = nullptr;
			#if defined(_WIN32)
			P->Handle = LoadLibraryExW(PathUtil::FromUtf8(File).c_str(), nullptr,
			    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
			if (P->Handle)
			{
				Function = reinterpret_cast<FNativeExtensionQuery>(
				    GetProcAddress(P->Handle, NativeExtensionQuerySymbol));
			}

			#elif !defined(__EMSCRIPTEN__) && !VEROSCRIPT_PLATFORM_IOS
			P->Handle = dlopen(PathUtil::FromUtf8(File).c_str(), RTLD_NOW | RTLD_LOCAL);
			if (P->Handle)
			{
				Function = reinterpret_cast<FNativeExtensionQuery>(dlsym(P->Handle, NativeExtensionQuerySymbol));
			}

			#endif
			if (!P->Handle)
			{
				Error = "cannot load native extension library";
				return {};
			}
			if (!Query(Function, P->Descriptor, O, Error)) return {};
			auto Result = std::shared_ptr<const FLoadedNativeExtension>(new FLoadedNativeExtension(P));
			{
				std::lock_guard<std::mutex> Lock(Cache().Mutex);
				const auto Found = Modules.find(Key);
				if (auto Existing = Found == Modules.end() ? nullptr : Found->second.lock())
				{
					if (Existing->GetContentIdentity() != Result->GetContentIdentity())
					{
						Error =
						    "native library changed during parallel load; restart or use an immutable new path";
						return {};
					}
					if (!Validate(Existing->GetDescriptor(), O, Error)) return {};
					return Existing;
				}
				for (auto I = Modules.begin(); I != Modules.end();)
				{
					if (I->second.expired())
					{
						I = Modules.erase(I);
					}
					else
					{
						++I;
					}
				}
				Modules[Key] = Result;
			}
			return Result;
		}
		catch (const std::exception& E)
		{
			Error = E.what();
			return {};
		}
		catch (...)
		{
			Error = "native extension load threw an exception";
			return {};
		}
	}
}
