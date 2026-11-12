// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmBackendLoader.h"
#include "VeroScriptVmModule.h"
#include "VeroScriptPath.h"

#include <algorithm>
#include <atomic>
#include <limits>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif !defined(__EMSCRIPTEN__) && !VEROSCRIPT_PLATFORM_IOS
#include <dlfcn.h>
#endif

namespace VeroScript
{
	namespace
	{
		class FModule final
		{
		public:
			~FModule()
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

			bool Open(const std::filesystem::path& Path, std::string& Error)
			{
				#if defined(_WIN32)
				Handle = LoadLibraryExW(Path.c_str(), nullptr,
				    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
				if (!Handle)
				{
					Error = "cannot load VM library (Windows error " + std::to_string(GetLastError()) + ")";
				}
				#elif !defined(__EMSCRIPTEN__) && !VEROSCRIPT_PLATFORM_IOS
				Handle = dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL);
				if (!Handle)
				{
					const char* Reason = dlerror();
					Error = Reason ? Reason : "cannot load VM library";
				}
				#else
				(void)Path;
				Error = "dynamic VM libraries are unavailable on this platform";
				#endif
				return Handle != nullptr;
			}

			FVmModuleQuery Query() const noexcept
			{
				#if defined(_WIN32)
				return reinterpret_cast<FVmModuleQuery>(GetProcAddress(Handle, VmModuleQuerySymbol));
				#elif !defined(__EMSCRIPTEN__) && !VEROSCRIPT_PLATFORM_IOS
				return reinterpret_cast<FVmModuleQuery>(dlsym(Handle, VmModuleQuerySymbol));
				#else
				return nullptr;
				#endif
			}

		private:
			#if defined(_WIN32)
			HMODULE Handle = nullptr;
			#else
			void* Handle = nullptr;
			#endif
		};

		class FLoadedFactory final : public IVmBackendFactory
		{
		public:
			FLoadedFactory(std::shared_ptr<FModule> InModule, IVmBackendFactory* InFactory,
			    void(VEROSCRIPT_JIT_MODULE_CALL* InDestroy)(void*))
			    : Module(std::move(InModule)), Factory(InFactory), Destroy(InDestroy)
			{
			}
			~FLoadedFactory() override
			{
				Destroy(Factory);
			}
			const FVmBackendDescription& GetDescription() const noexcept override
			{
				return Factory->GetDescription();
			}
			const void* GetHostIdentity() const noexcept override
			{
				return Factory->GetHostIdentity();
			}
			IVmBackendInstance* CreateInstance(std::string& Error) const override
			{
				return Factory->CreateInstance(Error);
			}
			void DestroyInstance(IVmBackendInstance* Instance) const noexcept override
			{
				Factory->DestroyInstance(Instance);
			}

		private:
			std::shared_ptr<FModule> Module;
			IVmBackendFactory* Factory;
			void(VEROSCRIPT_JIT_MODULE_CALL* Destroy)(void*);
		};

		std::shared_ptr<const IVmBackendFactory> TryLoad(const std::filesystem::path& Path,
		    std::string& Error, bool& HardFailure)
		{
			auto Module = std::make_shared<FModule>();
			try
			{
				if (!Module->Open(Path, Error)) return {};
				const auto Query = Module->Query();
				if (!Query)
				{
					Error = "library does not export veroscript_vm_query";
					return {};
				}
				FVmModuleDescriptor Descriptor;
				if (!Query(VEROSCRIPT_VERSION_CODE, sizeof(Descriptor), &Descriptor) || Descriptor.StructSize != sizeof(Descriptor) || Descriptor.CoreVersion != VEROSCRIPT_VERSION_CODE)
				{
					Error = "VM module product version or descriptor layout is incompatible";
					return {};
				}
				if (!std::memchr(Descriptor.Abi, 0, sizeof(Descriptor.Abi)) || BuildVmModuleAbi() != Descriptor.Abi)
				{
					Error = "VM module ABI is incompatible with this Core build";
					return {};
				}
				if (!std::memchr(Descriptor.Id, 0, sizeof(Descriptor.Id)) || !Descriptor.Id[0] || !std::memchr(Descriptor.Version, 0, sizeof(Descriptor.Version)) || !Descriptor.Version[0] || !Descriptor.CreateFactory || !Descriptor.DestroyFactory)
				{
					Error = "VM module descriptor is incomplete";
					return {};
				}
				HardFailure = true;
				char CreationError[1024]{};
				auto* Factory = static_cast<IVmBackendFactory*>(Descriptor.CreateFactory(
				    &GetVmHostServices(), CreationError, sizeof(CreationError)));
				if (!Factory)
				{
					CreationError[sizeof(CreationError) - 1] = 0;
					Error = CreationError[0] ? CreationError : "VM factory initialization failed";
					return {};
				}
				// This guard also owns the defining module while exceptions are inspected.
				const auto Destroy = Descriptor.DestroyFactory;
				std::unique_ptr<IVmBackendFactory, decltype(Destroy)> Guard(Factory, Destroy);
				const auto& Description = Factory->GetDescription();
				if (Description.Id != Descriptor.Id || Description.Version != Descriptor.Version || Description.CoreVersion != VEROSCRIPT_VERSION_CODE || Description.ImplementationId.empty() || Factory->GetHostIdentity() != GetVmHostServices().OwnerIdentity || !(Description.Capabilities & VmBytecodeCapability))
				{
					HardFailure = false;
					Error = "VM factory does not match its module or owning Core";
					return {};
				}
				auto Result = std::make_shared<FLoadedFactory>(Module, Factory, Descriptor.DestroyFactory);
				Guard.release();
				return Result;
			}
			catch (const std::exception& Exception)
			{
				Error = Exception.what();
			}
			catch (...)
			{
				Error = "VM module initialization raised an exception";
			}
			return {};
		}
		bool IsCandidate(const std::filesystem::path& Path)
		{
			auto Name = PathUtil::ToUtf8(Path.filename());
			std::transform(Name.begin(), Name.end(), Name.begin(), [](unsigned char C)
			{
				return static_cast<char>(std::tolower(C));
			});
			#if defined(_WIN32)
			return Name.rfind("veroscriptvm", 0) == 0 && Name.size() >= 4 && Name.compare(Name.size() - 4, 4, ".dll") == 0;
			#elif defined(__APPLE__)
			return Name.rfind("libveroscriptvm", 0) == 0 && Path.extension() == ".dylib";
			#else
			return Name.rfind("libveroscriptvm", 0) == 0 && Path.extension() == ".so";
			#endif
		}
	}

	std::shared_ptr<const IVmBackendFactory> LoadVmBackend(const std::string_view Path,
	    std::string& OutError, std::string* OutLibraryPath)
	{
		try
		{
			// Input may alias either output string.
			const std::string RequestedPath(Path);
			OutError.clear();
			if (OutLibraryPath)
			{
				OutLibraryPath->clear();
			}
			#if defined(__EMSCRIPTEN__) || VEROSCRIPT_PLATFORM_IOS
			OutError = "dynamic VM modules are unavailable on this target; use static registration";
			return {};
			#endif
			#if defined(_MSC_VER) && !defined(_DLL)
			OutError = "dynamic VM loading requires the shared MSVC runtime (/MD or /MDd)";
			return {};
			#endif
			if (RequestedPath.empty() || RequestedPath.find('\0') != std::string::npos)
			{
				OutError = "VM backend requires a nonempty file or directory path without NUL characters";
				return {};
			}
			const auto Root = std::filesystem::absolute(PathUtil::FromUtf8(RequestedPath)).lexically_normal();
			if (std::filesystem::is_regular_file(Root))
			{
				bool HardFailure = false;
				auto Factory = TryLoad(Root, OutError, HardFailure);
				if (Factory && OutLibraryPath)
				{
					*OutLibraryPath = PathUtil::ToUtf8(Root);
				}
				return Factory;
			}
			if (!std::filesystem::is_directory(Root))
			{
				OutError = "VM backend path is not an existing file or directory: " + PathUtil::ToUtf8(Root);
				return {};
			}
			std::vector<std::filesystem::path> Candidates;
			for (std::filesystem::recursive_directory_iterator It(Root,
			         std::filesystem::directory_options::skip_permission_denied),
			    End;
			    It != End; ++It)
			{
				if (It.depth() >= 2)
				{
					It.disable_recursion_pending();
				}
				std::error_code EntryError;
				if (It->is_regular_file(EntryError) && IsCandidate(It->path()))
				{
					if (Candidates.size() >= 4096)
					{
						OutError = "VM directory exceeds the 4096-candidate search limit";
						return {};
					}
					Candidates.push_back(It->path());
				}
			}
			std::sort(Candidates.begin(), Candidates.end(), [](const auto& Left, const auto& Right)
			{
				return Left.generic_u8string() < Right.generic_u8string();
			});
			std::string Reasons;
			for (const auto& Candidate : Candidates)
			{
				std::string Reason;
				bool HardFailure = false;
				auto Factory = TryLoad(Candidate, Reason, HardFailure);
				if (Factory)
				{
					if (OutLibraryPath)
					{
						*OutLibraryPath = PathUtil::ToUtf8(Candidate);
					}
					return Factory;
				}
				if (HardFailure)
				{
					OutError = Reason;
					return {};
				}
				if (Reasons.size() < 8192)
				{
					Reasons += "\n" + PathUtil::ToUtf8(Candidate) + ": " + Reason;
				}
			}
			OutError = "no compatible usable VM backend in directory: " + PathUtil::ToUtf8(Root) + Reasons;
		}
		catch (const std::exception& Exception)
		{
			OutError = Exception.what();
		}
		catch (...)
		{
			OutError = "VM backend loading failed";
		}
		return {};
	}
}
