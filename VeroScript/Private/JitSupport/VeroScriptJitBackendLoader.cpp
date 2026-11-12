// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptJitModule.h"
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
					Error = "cannot load JIT library (Windows error " + std::to_string(GetLastError()) + ")";
				}
				#elif !defined(__EMSCRIPTEN__) && !VEROSCRIPT_PLATFORM_IOS
				Handle = dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL);
				if (!Handle)
				{
					const char* Reason = dlerror();
					Error = Reason ? Reason : "cannot load JIT library";
				}
				#else
				(void)Path;
				Error = "dynamic JIT libraries are unavailable on this platform";
				#endif
				return Handle != nullptr;
			}

			FJitModuleQuery Query() const noexcept
			{
				#if defined(_WIN32)
				return reinterpret_cast<FJitModuleQuery>(GetProcAddress(Handle, JitModuleQuerySymbol));
				#elif !defined(__EMSCRIPTEN__) && !VEROSCRIPT_PLATFORM_IOS
				return reinterpret_cast<FJitModuleQuery>(dlsym(Handle, JitModuleQuerySymbol));
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

		// Core-owned control blocks keep all backend destructors and their code alive,
		// including artifacts retained after a session/cache has been replaced.
		struct FProxyResources
		{
			std::atomic<std::size_t> Bytes{0};
		};

		struct FLoadedArtifact
		{
			FLoadedArtifact(std::shared_ptr<FModule> InModule, std::shared_ptr<FProxyResources> InResources,
				const FJitCompiledFunction& Source)
				: Module(std::move(InModule)), Resources(std::move(InResources)), Function(Source)
			{
				Resources->Bytes.fetch_add(OwnershipBytes(), std::memory_order_relaxed);
			}

			~FLoadedArtifact()
			{
				Resources->Bytes.fetch_sub(OwnershipBytes(), std::memory_order_relaxed);
			}

			static constexpr std::size_t OwnershipBytes() noexcept
			{
				return sizeof(FLoadedArtifact) - sizeof(FJitCompiledFunction);
			}

			// The module outlives even a malformed artifact's copied backend values.
			std::shared_ptr<FModule> Module;
			std::shared_ptr<FProxyResources> Resources;
			FJitCompiledFunction Function;
		};

		class FLoadedExecutable final : public IJitExecutable
		{
		public:

			//~ Begin IJitExecutable Interface
			EJitInvocationResult Invoke(FJitCallContext& Context) const override
			{
				return Source->Executable->Invoke(Context);
			}

			~FLoadedExecutable() override
			{
				Resources->Bytes.fetch_sub(OwnershipBytes(), std::memory_order_relaxed);
			}
			//~ End IJitExecutable Interface

			FLoadedExecutable(std::shared_ptr<FModule> InModule,
				std::shared_ptr<const FJitCompiledFunction> InSource, std::shared_ptr<FProxyResources> InResources)
				: Module(std::move(InModule)), Source(std::move(InSource)), Resources(std::move(InResources))
			{
				Resources->Bytes.fetch_add(OwnershipBytes(), std::memory_order_relaxed);
			}

			static constexpr std::size_t OwnershipBytes() noexcept
			{
				// The copied public artifact is measured by the core cache; this
				// proxy additionally retains the original backend artifact.
				return sizeof(FLoadedExecutable) + sizeof(FJitCompiledFunction);
			}

			bool BelongsTo(const std::shared_ptr<FProxyResources>& Owner) const noexcept
			{
				return Resources == Owner;
			}

			const std::shared_ptr<const FJitCompiledFunction>& GetSource() const noexcept
			{
				return Source;
			}

		private:
			std::shared_ptr<FModule> Module;
			std::shared_ptr<const FJitCompiledFunction> Source;
			std::shared_ptr<FProxyResources> Resources;
		};

		class FLoadedInstance final : public IJitBackendInstance
		{
		public:
			//~ Begin IJitBackendInstance Interface
			const FJitBackendDescription& GetDescription() const noexcept override
			{
				return Instance->GetDescription();
			}

			EJitCompilationResult Compile(const FJitCompileRequest& Request, FJitCompileResponse& Response) override
			{
				FJitCompileResponse Source;
				auto BackendRequest = Request;
				if (BackendRequest.AvailableResources.RetainedBytes < FLoadedExecutable::OwnershipBytes() + FLoadedArtifact::OwnershipBytes())
				{
					Response = {};
					Response.Reason = "dynamic JIT artifact ownership exceeds the remaining retention budget";
					bResourceLimitReached = true;
					return EJitCompilationResult::Deferred;
				}
				BackendRequest.AvailableResources.RetainedBytes -= FLoadedExecutable::OwnershipBytes() + FLoadedArtifact::OwnershipBytes();
				const auto Result = Instance->Compile(BackendRequest, Source);
				Response.Reason = std::move(Source.Reason);
				Response.Function.reset();
				if (Source.Function)
				{
					auto Artifact = std::make_shared<FLoadedArtifact>(Module, Resources, *Source.Function);
					if (Source.Function->Executable)
					{
						Artifact->Function.Executable = std::make_shared<FLoadedExecutable>(Module, std::move(Source.Function), Resources);
					}
					Response.Function = std::shared_ptr<const FJitCompiledFunction>(Artifact, &Artifact->Function);
				}
				return Result;
			}

			bool Publish(const FJitCompiledFunction& Function, std::string& Error) override
			{
				const auto* Entry = dynamic_cast<const FLoadedExecutable*>(Function.Executable.get());
				if (!Entry || !Entry->BelongsTo(Resources))
				{
					Error = "dynamic JIT publication requires an owned backend artifact";
					return false;
				}
				return Instance->Publish(*Entry->GetSource(), Error);
			}

			void Discard(const std::shared_ptr<const FJitCompiledFunction>& Function) noexcept override
			{
				const auto* Entry = Function
					? dynamic_cast<const FLoadedExecutable*>(Function->Executable.get()) : nullptr;
				Instance->Discard(Entry && Entry->BelongsTo(Resources) ? Entry->GetSource() : std::shared_ptr<const FJitCompiledFunction>{});
			}

			void ResetExecutionStatistics() noexcept override
			{
				Instance->ResetExecutionStatistics();
			}

			FJitStats GetStatistics() const noexcept override
			{
				return Instance->GetStatistics();
			}

			FJitResourceUsage GetResourceUsage() const noexcept override
			{
				auto Result = Instance->GetResourceUsage();
				const auto Bytes = Resources->Bytes.load(std::memory_order_relaxed) + sizeof(FLoadedInstance) + sizeof(FProxyResources);
				const auto Max = std::numeric_limits<std::size_t>::max();
				Result.RetainedBytes = Bytes > Max - Result.RetainedBytes ? Max : Result.RetainedBytes + Bytes;
				Result.bResourceLimitReached = Result.bResourceLimitReached || bResourceLimitReached;
				return Result;
			}

			void Retire() noexcept override
			{
				Instance->Retire();
			}
			//~ End IJitBackendInstance Interface

			FLoadedInstance(std::shared_ptr<FModule> InModule, std::unique_ptr<IJitBackendInstance> InInstance)
				: Module(std::move(InModule)), Instance(std::move(InInstance))
			{
			}

		private:
			std::shared_ptr<FModule> Module;
			std::unique_ptr<IJitBackendInstance> Instance;
			std::shared_ptr<FProxyResources> Resources = std::make_shared<FProxyResources>();
			bool bResourceLimitReached = false;
		};

		struct FFactoryDeleter
		{
			void operator()(IJitBackendFactory* Factory) const noexcept
			{
				Destroy(Factory);
			}

			void (VEROSCRIPT_JIT_MODULE_CALL *Destroy)(void*) = nullptr;
		};

		class FLoadedFactory final : public IJitBackendFactory
		{
		public:
			//~ Begin IJitBackendFactory Interface
			const FJitBackendDescription& GetDescription() const noexcept override
			{
				return Description;
			}

			std::unique_ptr<IJitBackendInstance> CreateInstance(const FJitProgramView& Program,
				const FJitResourceLimits& Limits, std::string& Error) const override
			{
				auto Instance = Factory->CreateInstance(Program, Limits, Error);
				if (!Instance) return {};
				return std::make_unique<FLoadedInstance>(Module, std::move(Instance));
			}
			//~ End IJitBackendFactory Interface

			FLoadedFactory(std::shared_ptr<FModule> InModule,
				std::unique_ptr<IJitBackendFactory, FFactoryDeleter> InFactory)
				: Module(std::move(InModule)), Factory(std::move(InFactory)), Description(Factory->GetDescription())
			{
			}

		private:
			std::shared_ptr<FModule> Module;
			std::unique_ptr<IJitBackendFactory, FFactoryDeleter> Factory;
			FJitBackendDescription Description;
		};

		std::shared_ptr<const IJitBackendFactory> TryLoad(const std::filesystem::path& Path, std::string& Error)
		{
			auto Module = std::make_shared<FModule>();
			try
			{
				if (!Module->Open(Path, Error)) return {};
				const auto Query = Module->Query();
				if (!Query)
				{
					Error = "library does not export veroscript_jit_query";
					return {};
				}
				FJitModuleDescriptor Descriptor;
				if (!Query(VEROSCRIPT_VERSION_CODE, sizeof(Descriptor), &Descriptor)
					|| Descriptor.StructSize != sizeof(Descriptor) || Descriptor.CoreVersion != VEROSCRIPT_VERSION_CODE)
				{
					Error = "JIT module Core version or descriptor layout is incompatible";
					return {};
				}
				if (!std::memchr(Descriptor.Abi, 0, sizeof(Descriptor.Abi))
					|| BuildJitModuleAbi() != Descriptor.Abi)
				{
					Error = "JIT backend ABI is incompatible with this core build";
					return {};
				}
				if (!std::memchr(Descriptor.Id, 0, sizeof(Descriptor.Id)) || !Descriptor.Id[0]
					|| !std::memchr(Descriptor.Version, 0, sizeof(Descriptor.Version)) || !Descriptor.Version[0]
					|| !Descriptor.CreateFactory || !Descriptor.DestroyFactory)
				{
					Error = "JIT module descriptor is incomplete";
					return {};
				}
				char CreationError[1024]{};
				std::unique_ptr<IJitBackendFactory, FFactoryDeleter> Factory(
					static_cast<IJitBackendFactory*>(Descriptor.CreateFactory(
						&GetJitHostServices(), CreationError, sizeof(CreationError))),
					FFactoryDeleter{Descriptor.DestroyFactory});
				if (!Factory)
				{
					CreationError[sizeof(CreationError) - 1] = 0;
					Error = CreationError[0] ? CreationError : "JIT backend factory initialization failed";
					return {};
				}
				const auto& Description = Factory->GetDescription();
				const auto HostTarget = GetJitHostTarget();
				if (Description.Id != Descriptor.Id || Description.CoreVersion != VEROSCRIPT_VERSION_CODE
					|| Description.Target.empty() || Description.MaxInvocationDepth == 0
					|| (Description.Target != "portable-cpp" && Description.Target != HostTarget)
					|| (Description.bNativeCode && (Description.Target == "portable-cpp" || HostTarget == "unknown")))
				{
					Error = "JIT factory has an incompatible runtime contract";
					return {};
				}
				return std::make_shared<FLoadedFactory>(std::move(Module), std::move(Factory));
			}
			catch (const std::exception& Exception)
			{
				// Copy diagnostics while the defining module and its RTTI are alive.
				Error = Exception.what();
			}
			catch (...)
			{
				Error = "JIT library initialization raised an exception";
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
			return Name.rfind("veroscriptjit", 0) == 0 && Name.size() >= 4 && Name.compare(Name.size() - 4, 4, ".dll") == 0;
			#elif defined(__APPLE__)
			return Name.rfind("libveroscriptjit", 0) == 0 && Path.extension() == ".dylib";
			#else
			return Name.rfind("libveroscriptjit", 0) == 0 && Path.extension() == ".so";
			#endif
		}
	}

	std::shared_ptr<const IJitBackendFactory> LoadJitBackend(const std::string_view Path,
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
			if (!GetHostPlatformInfo().bAllowsDynamicJitLibraries)
			{
				OutError = "dynamic JIT libraries are unavailable on this platform; use VM or a static portable backend";
				return {};
			}
			#if defined(_MSC_VER) && !defined(_DLL)
			OutError = "dynamic JIT loading requires the shared MSVC runtime (/MD or /MDd)";
			return {};
			#endif
			if (RequestedPath.empty() || RequestedPath.find('\0') != std::string::npos)
			{
				OutError = "JIT backend requires a nonempty file or directory path without NUL characters";
				return {};
			}
			const auto Root = std::filesystem::absolute(PathUtil::FromUtf8(RequestedPath)).lexically_normal();
			if (std::filesystem::is_regular_file(Root))
			{
				auto Factory = TryLoad(Root, OutError);
				if (Factory && OutLibraryPath)
				{
					*OutLibraryPath = PathUtil::ToUtf8(Root);
				}
				return Factory;
			}
			if (!std::filesystem::is_directory(Root))
			{
				OutError = "JIT backend path is not an existing file or directory: " + PathUtil::ToUtf8(Root);
				return {};
			}
			std::vector<std::filesystem::path> Candidates;
			for (std::filesystem::recursive_directory_iterator It(Root,
				std::filesystem::directory_options::skip_permission_denied), End; It != End; ++It)
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
						OutError = "JIT directory exceeds the 4096-candidate search limit";
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
				auto Factory = TryLoad(Candidate, Reason);
				if (Factory)
				{
					if (OutLibraryPath)
					{
						*OutLibraryPath = PathUtil::ToUtf8(Candidate);
					}
					return Factory;
				}
				if (Reasons.size() < 8192)
				{
					Reasons += "\n" + PathUtil::ToUtf8(Candidate) + ": " + Reason;
				}
			}
			OutError = "no compatible usable JIT backend in directory: " + PathUtil::ToUtf8(Root) + Reasons;
		}
		catch (const std::exception& Exception)
		{
			OutError = Exception.what();
		}
		catch (...)
		{
			OutError = "JIT backend loading failed";
		}
		return {};
	}
}
