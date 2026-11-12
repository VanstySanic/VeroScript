// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestEngine.h"
#include "VeroScriptEngine.h"
#include "VeroScriptBytecodeIO.h"
#include "VeroScriptCompile.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptPackage.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"

#if defined(VEROSCRIPT_WITH_APPLICATION)
#include "VeroScriptApplication.h"
#include "VeroScriptVersion.h"
#include "VeroScriptVmBackend.h"
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <variant>

namespace
{
	bool TestVmBackendConfiguration()
	{
		using namespace VeroScript;
		const char* Path = std::getenv("VEROSCRIPT_TEST_VM_BACKEND");
		#ifdef VEROSCRIPT_TEST_VM_LIBRARY
		if (!Path)
		{
			Path = VEROSCRIPT_TEST_VM_LIBRARY;
		}
		#endif
		if (!Path || !*Path)
		{
			std::cerr << "VM configuration regression requires an explicit backend library\n";
			return false;
		}
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		FEngineConfig Config;
		Config.VmBackendPath = Path;
		Config.ExecutionBackend = EExecutionBackend::Vm;
		Session.ApplyConfig(Config);
		const auto Factory = Session.GetVmExecutionDomain()->GetFactory();
		if (!Factory || !Errors.str().empty()) return false;
		for (const bool AfterExecution : {false, true})
		{
			if (AfterExecution && (!Session.LoadSource("void main(){print(26);}", "vm-config.vs").bSuccess || !Session.Run().bSuccess || Output.str() != "26\n"))
			{
				return false;
			}
			Errors.str({});
			Errors.clear();
			auto Invalid = Config;
			Invalid.VmBackendPath += std::string(1, '\0') + "suffix";
			Invalid.ExecutionBackend = EExecutionBackend::Jit;
			Session.ApplyConfig(Invalid);
			if (Errors.str().find("NUL") == std::string::npos || Session.GetExecutionBackend() != EExecutionBackend::Vm || Session.GetVmExecutionDomain()->GetFactory() != Factory)
			{
				std::cerr << "invalid VM path bypassed validation or changed Session configuration\n";
				return false;
			}
		}
		Session.Clear();
		Errors.str({});
		Errors.clear();
		Session.ApplyConfig(Config);
		if (!Errors.str().empty() || !Session.GetVmExecutionDomain()->IsFrozen() || Session.GetVmExecutionDomain()->GetFactory() != Factory)
		{
			std::cerr << "reapplying the same valid VM path changed its frozen selection\n";
			return false;
		}
		return true;
	}

	class FTemporaryDirectory final
	{
	public:
		FTemporaryDirectory()
		{
			const auto Suffix = std::chrono::steady_clock::now().time_since_epoch().count();
			Path = std::filesystem::temp_directory_path() / ("veroscript_cpp_engine_" + std::to_string(Suffix));
			std::filesystem::create_directories(Path);
		}

		~FTemporaryDirectory()
		{
			std::error_code Error;
			std::filesystem::remove_all(Path, Error);
		}

		std::filesystem::path Path;
	};

	#if defined(VEROSCRIPT_WITH_APPLICATION)
	bool TestApplicationConfigurationRejection(const std::filesystem::path& Directory)
	{
		using namespace VeroScript;
		class FInvalidVmFactory final : public IVmBackendFactory
		{
		public:
			//~ Begin IVmBackendFactory Interface
			const FVmBackendDescription& GetDescription() const noexcept override
			{
				return Description;
			}
			const void* GetHostIdentity() const noexcept override
			{
				return nullptr;
			}
			IVmBackendInstance* CreateInstance(std::string& Error) const override
			{
				Error = "invalid test factory must not create an instance";
				return nullptr;
			}
			void DestroyInstance(IVmBackendInstance* Instance) const noexcept override
			{
				delete Instance;
			}
			//~ End IVmBackendFactory Interface
		private:
			FVmBackendDescription Description;
		};
		auto Compiled = VeroScriptCompile::CompileSource("void main(){print(91);}", "<configuration-app>");
		if (!Compiled.bSuccess || !Compiled.Module) return false;
		FPackageBuildOptions Package;
		Package.PackageName = "cpp.configuration.tests";
		std::vector<Application::FContentFile> Files;
		Application::FDiagnostic Error;
		const std::string AppPath = PathUtil::ToUtf8(Directory / "configuration.vsapp");
		if (!Application::BuildApplication(*Compiled.Module, Package, FormatVersion(VEROSCRIPT_VERSION), {}, Files, Error) ||
		    !Application::WriteArchive(Files, AppPath, Error))
		{
			std::cerr << "cannot create configuration regression application: " << Error.Message << '\n';
			return false;
		}
		{
			std::ostringstream Output, Errors;
			if (!VeroScript::Test::FEngine::RunFile(AppPath, Output, Errors).bSuccess || Output.str() != "91\n")
			{
				std::cerr << "configuration regression application did not run: " << Errors.str();
				return false;
			}
		}
		for (const bool UseHost : {false, true})
		{
			std::ostringstream Output, Errors;
			FStreamEngineHost Host(Output, Errors);
			FEngineConfig Config;
			Config.VmBackendFactory = std::make_shared<FInvalidVmFactory>();
			VeroScriptSession Session(Host);
			std::string ExpectedError;
			if (Session.ApplyConfig(Config, &ExpectedError) || ExpectedError.empty()) return false;
			Errors.str({});
			Errors.clear();
			const auto Result = UseHost
			    ? VeroScriptEngine::RunFile(AppPath, Host, Config)
			    : VeroScriptEngine::RunFile(AppPath, Output, Errors, {}, {}, false, nullptr, {}, {}, {},
			        EExecutionBackend::Vm, nullptr, {}, {}, true, false, {}, Config.VmBackendFactory);
			if (Result.bSuccess || Result.ExitCode != 2 || Result.Error.Message != ExpectedError || !Output.str().empty())
			{
				std::cerr << "application execution ignored rejected configuration: " << Result.Error.Message << '\n';
				return false;
			}
		}
		return true;
	}
	#endif

	bool TestInvalidFilePaths(const std::filesystem::path& Directory)
	{
		using namespace VeroScript;
		const auto SourcePath = Directory / "nul-input.vs";
		{
			std::ofstream File(SourcePath, std::ios::binary);
			File << "void main(){print(99);}";
		}
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		if (!Session.LoadSource("void main(){print(7);}", "last-good.vs").bSuccess) return false;
		const auto* Previous = Session.GetLoadedModule();
		const auto IsRejected = [](const FRunResult& Result)
		{
			return !Result.bSuccess && Result.ExitCode == 2 && Result.Error.Message.find("NUL") != std::string::npos;
		};
		for (const auto& Prefix : {SourcePath, Directory})
		{
			const std::string Invalid = PathUtil::ToUtf8(Prefix) + std::string(1, '\0') + "suffix.vs";
			std::unique_ptr<FBytecodeModule> Compiled;
			if (!IsRejected(VeroScriptEngine::CheckFile(Invalid, Errors)) || !IsRejected(VeroScriptEngine::CheckFile(Invalid, Host)) || !IsRejected(VeroScript::Test::FEngine::RunFile(Invalid, Output, Errors)) || !IsRejected(VeroScript::Test::FEngine::RunFile(Invalid, Host)) || !IsRejected(Session.LoadFile(Invalid)) || !IsRejected(Session.CompileFile(Invalid, Compiled)) || Compiled || Session.GetLoadedModule() != Previous || !Session.LoadedFilePath().empty())
			{
				std::cerr << "file API accepted a NUL path or replaced the last good module\n"
				          << Errors.str();
				return false;
			}
		}
		if (!Output.str().empty() || !Session.Run().bSuccess || Output.str() != "7\n") return false;
		const auto CatalogPath = Directory / "catalog.json";
		const std::string Sentinel = "existing catalog bytes";
		{
			std::ofstream File(CatalogPath, std::ios::binary);
			File << Sentinel;
		}
		const std::string InvalidCatalog = PathUtil::ToUtf8(CatalogPath) + std::string(1, '\0') + "suffix";
		if (Session.ExportBindingsToFile(InvalidCatalog)) return false;
		std::ifstream Catalog(CatalogPath, std::ios::binary);
		std::ostringstream Saved;
		Saved << Catalog.rdbuf();
		if (Saved.str() != Sentinel)
		{
			std::cerr << "invalid catalog path overwrote its existing file prefix\n";
			return false;
		}
		// The cache root is physical, while an in-memory source label is metadata.
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.bUseBytecodeCache = true;
		const std::string InvalidCache = PathUtil::ToUtf8(Directory) + std::string(1, '\0') + "cache";
		Config.BytecodeCacheDirectory = InvalidCache;
		Session.ApplyConfig(Config);
		if (!IsRejected(Session.LoadSource("void main(){}", "cache-input.vs")) || Session.GetLoadedModule() != Previous)
		{
			return false;
		}
		Session.SetUseBytecodeCache(false);
		const std::string VirtualLabel = std::string("<memory>") + std::string(1, '\0') + "label";
		return Session.LoadSource("void main(){}", VirtualLabel).bSuccess;
	}

	bool TestRunFileMemoryBindings(const std::filesystem::path& Directory,
	    const std::shared_ptr<const VeroScript::IJitBackendFactory>& BackendFactory)
	{
		using namespace VeroScript;
		FHostBindingRegistry Bindings;
		FHostTypeId TypeId;
		std::string Error;
		if (!Bindings.RegisterType("MemoryHostClass", EHostTypeKind::Class, TypeId, Error))
		{
			std::cerr << Error << '\n';
			return false;
		}
		auto Handle = std::make_shared<FHostObject>();
		Handle->TypeName = "MemoryHostClass";
		Handle->TypeId = TypeId.Value;
		FValue HostValue = Handle;
		if (!Bindings.RegisterNative("host_identity_probe", ENativeType::UInt64, {},
		        [](const FNativeCallContext& Call, FValue& Out, std::string& OutError)
		{
			if (!Call.MemoryServices)
			{
				OutError = "native memory services missing";
				return false;
			}
			const auto& Value = *static_cast<const FValue*>(Call.UserData);
			const auto* Catalog = Call.MemoryServices->MemoryHostBindings();
			std::uint64_t Id = 0, Address = 0;
			const auto Identity = Memory::ObjectId(Value, Id, Catalog);
			const bool Addresses = Call.MemoryServices->MemoryAddressesAllowed() && Memory::NativeAddressesAvailable();
			const auto Location = Addresses ? Memory::ObjectAddress(Value, Address, Catalog)
			                                : FMemoryResult{};
			if (!Identity || !Location || !Id || (Addresses && !Address))
			{
				OutError = "RunFile native callback lost host memory identity metadata";
				return false;
			}
			Out = Id;
			return true;
		},
		        &HostValue, Error))
		{
			std::cerr << Error << '\n';
			return false;
		}
		const auto ScriptPath = Directory / "memory_bindings.vs";
		{
			std::ofstream Script(ScriptPath, std::ios::binary);
			Script << R"VS(
import "concurrent.vs";
export uint64 sample_identity() { return host_identity_probe(); }
export void identity_worker(channel<uint64> result) { result.send(sample_identity()); }
void main() {
    uint64 expected = sample_identity();
    for (int i=0; i<32; ++i) { assert(sample_identity() == expected); }
    channel<uint64> result = channel<uint64>();
    int named = std::concurrent::spawn("identity_worker", result);
    std::concurrent::join_task(named); assert(result.recv() == expected);
    int callable = std::concurrent::spawn(identity_worker, result);
    std::concurrent::join_task(callable); assert(result.recv() == expected);
    auto closure = [result]() -> void => result.send(sample_identity());
    int lambda = std::concurrent::spawn(closure);
    std::concurrent::join_task(lambda); assert(result.recv() == expected);
    print("host memory identity passed");
}
)VS";
			if (!Script) return false;
		}
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !BackendFactory)
			{
				continue;
			}

			// The second execution must restore the same catalog on a cache hit.
			for (unsigned Run = 0; Run < 2; ++Run)
			{
				std::ostringstream Output, Errors;
				const auto Result = VeroScript::Test::FEngine::RunFile(PathUtil::ToUtf8(ScriptPath), Output,
				    Errors, {}, {}, true, nullptr, {}, PathUtil::ToUtf8(Directory / "memory-cache"),
				    {VEROSCRIPT_STDLIB_DIR}, Backend, &Bindings, {}, {}, true, false, BackendFactory);
				if (!Result.bSuccess || Output.str() != "host memory identity passed\n")
				{
					std::cerr << "RunFile memory binding regression (" << ExecutionBackendName(Backend)
					          << "): " << Errors.str();
					return false;
				}
			}
		}
		return true;
	}

	bool TestSessionCancellation()
	{
		using namespace VeroScript;
		std::ostringstream Output;
		std::ostringstream Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		std::atomic<bool> Stop{false};
		std::atomic<unsigned> Requests{0};
		std::thread Protocol([&]
		{
			while (!Stop.load(std::memory_order_acquire))
			{
				Session.RequestCancel();
				Requests.fetch_add(1, std::memory_order_release);
			}
		});
		while (Requests.load(std::memory_order_acquire) == 0)
		{
			std::this_thread::yield();
		}
		// Exercise both owner-thread replacement paths and null transitions while
		// a protocol thread cancels. Each snapshot must retain its token's lifetime.
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		for (unsigned Index = 0; Index < 4096; ++Index)
		{
			auto Control = Index % 3 == 0 ? nullptr : std::make_shared<FExecutionControl>();
			if (Index % 2 == 0)
			{
				Session.SetExecutionControl(std::move(Control));
			}
			else
			{
				Config.ExecutionControl = std::move(Control);
				Session.ApplyConfig(Config);
			}
		}
		Stop.store(true, std::memory_order_release);
		Protocol.join();

		Session.SetExecutionControl(nullptr);
		std::thread NullRequest(
		    [&]
		{
			Session.RequestCancel();
		});
		NullRequest.join();
		auto Previous = std::make_shared<FExecutionControl>();
		auto Control = std::make_shared<FExecutionControl>();
		Session.SetExecutionControl(Previous);
		Session.SetExecutionControl(Control);
		if (Control->IsCancellationRequested()) return false;
		std::thread IdleRequest(
		    [&]
		{
			Session.RequestCancel();
		});
		IdleRequest.join();
		if (!Control->IsCancellationRequested() || Previous->IsCancellationRequested()) return false;
		Control->Reset();

		struct FCheckpoint
		{
			std::mutex Mutex;
			std::condition_variable Changed;
			bool Entered = false;
			bool Finished = false;
		} Checkpoint;
		bool TimedOut = false;
		if (!Session.RegisterNative("cancel_checkpoint", ENativeType::Void, {},
		        [](const FNativeCallContext& Context, FValue&, std::string& Error)
		{
			auto& State = *static_cast<FCheckpoint*>(Context.UserData);
			std::unique_lock<std::mutex> Lock(State.Mutex);
			State.Entered = true;
			State.Changed.notify_all();
			if (!State.Changed.wait_for(Lock, std::chrono::seconds(5),
			        [&]
			{
				return State.Finished;
			}))
			{
				Error = "cancellation protocol thread timed out";
				return false;
			}
			return true;
		}, &Checkpoint) ||
		    !Session.LoadSource("void main(){cancel_checkpoint(); print(42);}",
		                "<cpp_session_cancel>")
		        .bSuccess)
		{
			std::cerr << Errors.str();
			return false;
		}
		std::thread ActiveRequest([&]
		{
			std::unique_lock<std::mutex> Lock(Checkpoint.Mutex);
			TimedOut = !Checkpoint.Changed.wait_for(Lock, std::chrono::seconds(5),
			    [&]
			{
				return Checkpoint.Entered;
			});
			if (!TimedOut)
			{
				Session.RequestCancel();
			}
			Checkpoint.Finished = true;
			Checkpoint.Changed.notify_all();
		});
		const auto Cancelled = Session.Run();
		ActiveRequest.join();
		if (TimedOut || Cancelled.bSuccess || !Output.str().empty() || Cancelled.Error.MessageId != "diag.execution_cancelled_by_host")
		{
			std::cerr << Errors.str();
			return false;
		}
		// A completed cancellation must leave the Session reusable.
		Control->Reset();
		return Session.Run().bSuccess && Output.str() == "42\n";
	}

	bool TestPublicModuleMacros(const std::filesystem::path& Directory,
	    const std::shared_ptr<const VeroScript::IJitBackendFactory>& Factory)
	{
		using namespace VeroScript;
		const auto BasePath = Directory / "macros.vs";
		const auto FacadePath = Directory / "macro_facade.vs";
		const auto EntryPath = Directory / "macro_entry.vs";
		const auto PackagePath = Directory / "macros.vslib";
		const std::string EntryLabel = PathUtil::ToUtf8(EntryPath);
		const std::string CacheDirectory = PathUtil::ToUtf8(Directory / "macro-cache");
		const auto CacheFile = PathUtil::FromUtf8(
		    VeroScriptBytecodeIO::GetCachePathForScript(EntryLabel, CacheDirectory));
		const std::string Source =
		    "import \"macro_facade.vs\";\n"
		    "#ifdef PRIVATE_VALUE\n#error private macro leaked\n#endif\n"
		    "#ifndef IMPLICIT_ONLY\n#error implicit public macro missing\n#endif\n"
		    "int kept = 1; int sample(){return ADD(VALUE)+kept+implicit_value();}\n"
		    "void main(){print(sample());}\n";
		const auto WriteText = [](const auto& Path, const std::string& Text)
		{
			std::ofstream File(Path, std::ios::binary | std::ios::trunc);
			File << Text;
			File.close();
			return static_cast<bool>(File);
		};
		const auto BaseSource = [](const int Value)
		{
			return "#define PRIVATE_VALUE 999\n#define_export VALUE " + std::to_string(Value) + "\n#define_export ADD(x) ((x)+HOST_SEED+HOST_OFFSET())\n";
		};
		if (!WriteText(FacadePath, "export import \"macros.vs\";\n") || !WriteText(EntryPath, Source))
		{
			return false;
		}

		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !Factory)
			{
				continue;
			}

			std::ostringstream Output, Errors;
			FStreamEngineHost Host(Output, Errors);
			FEngineConfig Config;
			VeroScript::Test::Configure(Config);
			Config.ExecutionBackend = Backend;
			Config.JitBackendFactory = Factory;
			Config.Defines = {"HOST_SEED=2"};
			Config.PredefinedMacros = {{"HOST_OFFSET", {}, false, "3"}};
			Config.BytecodeCacheDirectory = CacheDirectory;
			Config.bUseBytecodeCache = true;
			Config.SourceOverlays = {{PathUtil::ToUtf8(Directory / "implicit_macros.vs"),
			    "#define_export IMPLICIT_ONLY 999\n"
			    "export int implicit_value(){return HOST_SEED+HOST_OFFSET()-5;}\n"}};
			Config.ImplicitImportPaths = {Config.SourceOverlays.front().Path};
			if (!WriteText(BasePath, BaseSource(21))) return false;
			// RunSource and RunFile must share module macro semantics, including warm caches.
			for (const bool FromFile : {false, true})
			{
				std::filesystem::file_time_type CacheStamp;
				for (unsigned Run = 0; Run < 2; ++Run)
				{
					Output.str("");
					const auto Result = FromFile ? VeroScript::Test::FEngine::RunFile(EntryLabel, Host, Config)
					                             : VeroScript::Test::FEngine::RunSource(Source, EntryLabel, Host, ERunMode::Script, Config);
					if (!Result.bSuccess || Output.str() != "27\n")
					{
						std::cerr << "C++ public macro/cache integration failed: " << Errors.str();
						return false;
					}
					std::error_code CacheError;
					if (Run == 0)
					{
						// Move the timestamp away from "now" so filesystem timestamp
						// resolution cannot hide an unnecessary rewrite on the warm run.
						std::filesystem::last_write_time(CacheFile,
						    std::filesystem::file_time_type::clock::now() - std::chrono::hours(24),
						    CacheError);
						if (!CacheError)
						{
							CacheStamp = std::filesystem::last_write_time(CacheFile, CacheError);
						}

						if (CacheError)
						{
							std::cerr << "C++ public macro cache was not persisted: " << CacheError.message();
							return false;
						}
					}
					else
					{
						const auto WarmStamp = std::filesystem::last_write_time(CacheFile, CacheError);
						if (CacheError || WarmStamp != CacheStamp)
						{
							std::cerr << "C++ public macro warm run did not reuse its bytecode cache\n";
							return false;
						}
					}
				}
			}
			VeroScriptSession Session(Host);
			VeroScript::Test::Configure(Session);
			Session.ApplyConfig(Config);
			Session.SetHotReloadEnabled(true);
			FValue Value;
			if (!Session.LoadFile(EntryLabel).bSuccess || !Session.Run(ERunMode::Eval).bSuccess ||
			    !Session.SetGlobal("kept", std::int32_t{10}).bSuccess)
			{
				return false;
			}

			if (!WriteText(BasePath, BaseSource(22))) return false;
			Output.str("");
			if (!VeroScript::Test::FEngine::RunFile(EntryLabel, Host, Config).bSuccess || Output.str() != "28\n" || !Session.Reload().bSuccess || !Session.Run(ERunMode::Eval).bSuccess || !Session.CallFunction("sample", {}, &Value).bSuccess || std::get_if<std::int32_t>(&Value) == nullptr || std::get<std::int32_t>(Value) != 37)
			{
				std::cerr << "C++ macro dependency change did not invalidate/reload correctly: " << Errors.str();
				return false;
			}
			// A failed dependency preparation must leave the active Session usable.
			if (!WriteText(BasePath, "#error changed dependency\n") || Session.Reload().bSuccess ||
			    !Session.CallFunction("sample", {}, &Value).bSuccess ||
			    std::get_if<std::int32_t>(&Value) == nullptr || std::get<std::int32_t>(Value) != 37)
			{
				return false;
			}

			const auto Invalid = VeroScript::Test::FEngine::CheckSource("\n  import \"\";", EntryLabel, Host, Config);
			if (Invalid.bSuccess || !Invalid.Error.bPhysicalLocation || Invalid.Error.Location.Line != 2 ||
			    Invalid.Error.Location.Column != 3)
			{
				return false;
			}

			// Stripped binary libraries must still expose their owned public macro interface.
			auto Compiled = VeroScriptCompile::CompileSource(BaseSource(23), PathUtil::ToUtf8(BasePath));
			FPackageBuildOptions PackageOptions;
			PackageOptions.Kind = EPackageKind::Library;
			PackageOptions.PackageName = "CppMacroIntegration";
			PackageOptions.PackageVersion = 1;
			PackageOptions.bStripDebugInfo = true;
			std::vector<std::uint8_t> Bytes;
			std::string PackageError;
			if (!Compiled.bSuccess || !BuildPackage(*Compiled.Module, PackageOptions, Bytes, PackageError) ||
			    !WriteText(PackagePath, std::string(Bytes.begin(), Bytes.end())) ||
			    !WriteText(FacadePath, "export import \"macros.vslib\";\n"))
			{
				return false;
			}

			Output.str("");
			const auto Packaged = VeroScript::Test::FEngine::RunSource(Source, EntryLabel, Host, ERunMode::Script, Config);
			if (!Packaged.bSuccess || Output.str() != "29\n")
			{
				std::cerr << "C++ binary public macro integration failed: " << Errors.str() << PackageError;
				return false;
			}
			if (!WriteText(FacadePath, "export import \"macros.vs\";\n")) return false;
		}
		return true;
	}
}

static bool TestOutputCleanupReentry()
{
	using namespace VeroScript;
	std::ostringstream Output, Errors;
	FStreamEngineHost Host(Output, Errors);
	VeroScriptSession Session(Host);
	VeroScript::Test::Configure(Session);
	const char* Source = "class Counter { public: int value=42; int read(){return value;} } "
	                     "int retained=42; int answer(){return 42;} fn()->int callback(){return answer;} void main(){}";
	for (int Kind = 0; Kind != 6; ++Kind)
	{
		Session.SetHotReloadEnabled(true);
		if (!Session.LoadSource(Source, "output-cleanup.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess) return false;
		FObjectHandle Object;
		if (!Session.ConstructObject("Counter", {}, Object).bSuccess) return false;
		FValue CallableValue;
		if (!Session.CallFunction("callback", {}, &CallableValue).bSuccess) return false;
		const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(CallableValue));
		bool Attempted = false, Rejected = false;
		const auto Previous = Session.GetDebugProgram();
		struct FCleanupProbe
		{
			VeroScriptSession& Session;
			bool& Attempted;
			bool& Rejected;
		};
		FValue Value = MakeHostObject(1, "CleanupProbe", new FCleanupProbe{Session, Attempted, Rejected},
		    [](void* Pointer)
		{
			auto* Probe = static_cast<FCleanupProbe*>(Pointer);
			Probe->Attempted = true;
			Probe->Rejected =
			    !Probe->Session.LoadSource("int answer(){return 99;}", "replacement.vs").bSuccess;
			delete Probe;
		});
		FRunResult Result;
		if (Kind == 0)
		{
			Result = Session.CallFunction("answer", {}, &Value);
		}

		else if (Kind == 1)
		{
			Result = Session.CallMethod(Object, "read", {}, &Value);
		}

		else if (Kind == 2)
		{
			Result = Session.InvokeCallable(Callable, {}, &Value);
		}

		else if (Kind == 3)
		{
			Result = Session.GetField(Object, "value", Value);
		}

		else if (Kind == 4)
		{
			Result = Session.GetGlobal("retained", Value);
		}

		else
		{
			// Replacing an old script object also releases its native host base.
			Object->SetHostBase(std::get<FHostObjectHandle>(Value));
			Value = {};
			Result = Session.ConstructObject("Counter", {}, Object);
		}
		if (!Attempted || !Rejected || !Result.bSuccess || Session.GetDebugProgram().Generation != Previous.Generation || (Kind != 5 && (!std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 42)))
		{
			std::cerr << "output cleanup reentered Session for operation " << Kind << '\n'
			          << Errors.str();
			return false;
		}
		Session.ReleaseCallable(Callable);
	}
	return true;
}

static bool TestGovernedSessionJitReuse(
    const std::shared_ptr<const VeroScript::IJitBackendFactory>& Factory)
{
	// The controlled AsmJit path is available on native x64/AArch64 builds.
	if (!Factory || !Factory->GetDescription().bNativeCode || sizeof(void*) != 8) return true;
	using namespace VeroScript;
	for (const auto Backend : {EExecutionBackend::Jit, EExecutionBackend::Mixed})
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.JitBackendFactory = Factory;
		Config.ExecutionBackend = Backend;
		Config.ExecutionLimits.MaxInstructions = 1000;
		Session.ApplyConfig(Config);
		FJitPolicy Policy;
		Policy.MaxCompileAttempts = 1;
		Policy.MaxCachedEntries = 1;
		Policy.EagerCompileBudget = 1;
		Policy.bEnableControlledMixed = true;
		Session.SetJitPolicy(Policy);
		if (!Session.LoadSource("int main(){return 42;}", "governed-session-cache.vs").bSuccess || !Session.Run().bSuccess || Session.GetLastJitStats().ControlledNativeExecutions == 0 || Session.GetJitCacheStats().CompileAttempts != 1)
		{
			std::cerr << "governed Session JIT did not warm its native entry\n"
			          << Errors.str();
			return false;
		}
		for (int Call = 0; Call < 3; ++Call)
		{
			FValue Value;
			const auto Result = Call == 1 ? Session.Run() : Session.CallFunction("main", {}, &Value);
			if (!Result.bSuccess || (Call != 1 && (!std::holds_alternative<std::int32_t>(Value) || std::get<std::int32_t>(Value) != 42)) || Session.GetLastJitStats().ControlledNativeExecutions == 0 || Session.GetLastJitStats().CompileAttempts != 0 || Session.GetJitCacheStats().CompileAttempts != 1)
			{
				std::cerr << "governed Session cache lost its context or restarted its compile quota\n"
				          << Errors.str();
				return false;
			}
		}
		// Changing governance after warming must enforce the replacement budget.
		Session.SetExecutionLimits({1, 0});
		if (Session.Run().bSuccess)
		{
			std::cerr << "governed Session ignored its replacement instruction limit\n";
			return false;
		}
		Session.SetExecutionLimits({1000, 0});
		if (Backend == EExecutionBackend::Mixed)
		{
			Policy.bEnableControlledMixed = false;
			Session.SetJitPolicy(Policy);
			if (!Session.Run().bSuccess || Session.GetLastJitStats().Dispatches != 0 || Session.GetJitCacheStats().RetainedPayloadBytes != 0)
			{
				std::cerr << "governed Mixed execution ignored its native opt-out\n"
				          << Errors.str();
				return false;
			}
		}
	}
	return true;
}

int main(int ArgumentCount, char** Arguments)
{
	if (!TestVmBackendConfiguration()) return 1;
	if (!TestOutputCleanupReentry()) return 1;
	using namespace VeroScript;
	if (!TestSessionCancellation())
	{
		std::cerr << "C++ session cancellation contract was not preserved\n";
		return 1;
	}
	FTemporaryDirectory Temporary;
	#if defined(VEROSCRIPT_WITH_APPLICATION)
	if (!TestApplicationConfigurationRejection(Temporary.Path)) return 1;
	#endif
	if (!TestInvalidFilePaths(Temporary.Path)) return 1;
	std::shared_ptr<const IJitBackendFactory> BackendFactory;
	if (ArgumentCount > 1)
	{
		std::string Error;
		BackendFactory = LoadJitBackend(Arguments[1], Error);
		if (!BackendFactory)
		{
			std::cerr << Error << '\n';
			return 1;
		}
	}
	if (!TestRunFileMemoryBindings(Temporary.Path, BackendFactory)) return 1;
	if (!TestPublicModuleMacros(Temporary.Path, BackendFactory)) return 1;
	if (!TestGovernedSessionJitReuse(BackendFactory)) return 1;
	// App i18n services/budgets flow through the normal engine and session config.
	{
		class FMessages final : public I18n::II18nResourceSnapshot
		{
		public:
			I18n::EI18nLookupResult Lookup(const I18n::FI18nLookupRequest& R,
			    const I18n::FI18nWork&, I18n::FI18nMessage& Out, I18n::FI18nError&) const override
			{
				if (R.Domain != "cpp" || R.Key != "label" || R.Language != "en-US") return I18n::EI18nLookupResult::NotFound;
				Out = {"host context", "literal"};
				return I18n::EI18nLookupResult::Found;
			}
		};
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.JitBackendFactory = BackendFactory;
		Config.I18nServices = std::make_shared<I18n::FI18nServices>();
		I18n::FI18nError I18nError;
		if (!Config.I18nServices->PublishResources(std::make_shared<FMessages>(), I18nError)) return 1;
		const std::string Source = "string label(){std::i18n::context c=std::i18n::context();return c.resolve(std::i18n::text::message_key(\"cpp\",\"label\"));}void main(){assert(label()==\"host context\");}";
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		if (!VeroScript::Test::FEngine::RunSource(Source, "i18n-config.vs", Host, ERunMode::Script, Config).bSuccess)
		{
			std::cerr << Errors.str();
			return 1;
		}
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Session.ApplyConfig(Config);
		FValue Result;
		if (!Session.LoadSource(Source, "i18n-config.vs").bSuccess || !Session.Run(ERunMode::Eval).bSuccess ||
		    !Session.CallFunction("label", {}, &Result).bSuccess ||
		    std::get<std::string>(Result) != "host context")
		{
			return 1;
		}

		Config.I18nLimits.MaxOutputBytes = 1;
		const auto Limited = VeroScript::Test::FEngine::RunSource(Source, "i18n-budget.vs", Host, ERunMode::Script, Config);
		Session.ApplyConfig(Config);
		const auto SessionLimited = Session.CallFunction("label", {}, &Result);
		if (Limited.bSuccess || Limited.Error.MessageId != "diag.i18n_resource_limit" ||
		    SessionLimited.bSuccess || SessionLimited.Error.MessageId != "diag.i18n_resource_limit")
		{
			std::cerr << "C++ i18n configuration was not applied\n";
			return 1;
		}
	}
	const std::filesystem::path ScriptPath = Temporary.Path / "alias_cache.vs";
	const std::filesystem::path CachePath = Temporary.Path / "cache";
	{
		std::ofstream Script(ScriptPath, std::ios::binary);
		Script << "void main() { HostAlias value = \"cached\"; print(value); }\n";
		if (!Script)
		{
			std::cerr << "failed to create cache regression script\n";
			return 1;
		}
	}

	FHostBindingRegistry StringBindings;
	std::string BindingError;
	if (!StringBindings.RegisterTypeAlias(
	        "HostAlias", {}, "string", BindingError))
	{
		std::cerr << BindingError << '\n';
		return 1;
	}
	std::ostringstream FirstOutput;
	std::ostringstream FirstErrors;
	const FRunResult First = VeroScript::Test::FEngine::RunFile(
	    PathUtil::ToUtf8(ScriptPath),
	    FirstOutput,
	    FirstErrors,
	    {},
	    {},
	    true,
	    nullptr,
	    {},
	    PathUtil::ToUtf8(CachePath),
	    {},
	    EExecutionBackend::Vm,
	    &StringBindings);
	if (!First.bSuccess || FirstOutput.str() != "cached\n")
	{
		std::cerr << "initial alias compilation failed\n"
		          << FirstErrors.str();
		return 1;
	}

	FHostBindingRegistry IntBindings;
	if (!IntBindings.RegisterTypeAlias("HostAlias", {}, "int", BindingError))
	{
		std::cerr << BindingError << '\n';
		return 1;
	}
	std::ostringstream SecondOutput;
	std::ostringstream SecondErrors;
	const FRunResult Second = VeroScript::Test::FEngine::RunFile(
	    PathUtil::ToUtf8(ScriptPath),
	    SecondOutput,
	    SecondErrors,
	    {},
	    {},
	    true,
	    nullptr,
	    {},
	    PathUtil::ToUtf8(CachePath),
	    {},
	    EExecutionBackend::Vm,
	    &IntBindings);
	if (Second.bSuccess)
	{
		std::cerr << "bytecode cache reused stale host-alias semantics\n";
		return 1;
	}

	{
		std::ostringstream Output;
		std::ostringstream Errors;
		FStreamEngineHost Host(Output, Errors);
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		const auto Directory = Temporary.Path / "source-cache";
		const std::string CacheDirectory = PathUtil::ToUtf8(Directory);
		Config.BytecodeCacheDirectory = CacheDirectory;
		Config.bUseBytecodeCache = true;
		Config.ExecutionBackend = EExecutionBackend::Vm;
		const std::string Label = PathUtil::ToUtf8(Temporary.Path / "virtual.vs");
		if (!VeroScript::Test::FEngine::RunSource("void main(){print(41);}", Label, Host, ERunMode::Script, Config).bSuccess || Output.str() != "41\n" || !std::filesystem::exists(Directory) || std::filesystem::is_empty(Directory))
		{
			std::cerr << "C++ source execution ignored explicit bytecode caching\n"
			          << Errors.str();
			return 1;
		}
		Output.str("");
		if (!VeroScript::Test::FEngine::RunSource("void main(){print(42);}", Label, Host, ERunMode::Script, Config).bSuccess || Output.str() != "42\n")
		{
			std::cerr << "C++ source cache did not invalidate changed entry content\n"
			          << Errors.str();
			return 1;
		}
		const auto DisabledDirectory = Temporary.Path / "disabled-source-cache";
		const std::string DisabledCacheDirectory = PathUtil::ToUtf8(DisabledDirectory);
		Config.BytecodeCacheDirectory = DisabledCacheDirectory;
		Config.bUseBytecodeCache = false;
		if (!VeroScript::Test::FEngine::RunSource("void main(){}", Label, Host, ERunMode::Script, Config).bSuccess || std::filesystem::exists(DisabledDirectory))
		{
			std::cerr << "C++ source execution ignored the cache opt-out\n"
			          << Errors.str();
			return 1;
		}
		Config.bUseBytecodeCache = true;
		VeroScriptSession HotReloadSession(Host);
		VeroScript::Test::Configure(HotReloadSession);
		HotReloadSession.ApplyConfig(Config);
		HotReloadSession.SetHotReloadEnabled(true);
		if (!HotReloadSession.LoadSource("void main(){}", Label).bSuccess || !HotReloadSession.Run().bSuccess || std::filesystem::exists(DisabledDirectory))
		{
			std::cerr << "source cache bypass for hot reload was not preserved\n"
			          << Errors.str();
			return 1;
		}
	}

	{
		std::ostringstream CapabilityOutput;
		std::ostringstream CapabilityErrors;
		FStreamEngineHost CapabilityHost(CapabilityOutput, CapabilityErrors);
		FEngineConfig CapabilityConfig;
		VeroScript::Test::Configure(CapabilityConfig);
		CapabilityConfig.StdlibSearchPaths = {VEROSCRIPT_STDLIB_DIR};
		CapabilityConfig.ExecutionBackend = BackendFactory ? EExecutionBackend::Jit : EExecutionBackend::Vm;
		CapabilityConfig.JitBackendFactory = BackendFactory;
		CapabilityConfig.Capabilities.bAllowProcessDirectoryChange = false;
		const FRunResult CapabilityRun = VeroScript::Test::FEngine::RunSource(
		    "import \"io.vs\"; void main() { std::filesystem::chdir(\".\"); }\n",
		    "<cpp_chdir_capability>",
		    CapabilityHost,
		    ERunMode::Script,
		    CapabilityConfig);
		if (CapabilityRun.bSuccess || CapabilityErrors.str().find("process directory change capability") == std::string::npos)
		{
			std::cerr << "C++ engine did not enforce the process directory capability\n"
			          << CapabilityErrors.str();
			return 1;
		}
	}

	struct FWorkerProbe
	{
		std::thread::id OwnerThread;
		std::atomic<int> Calls{0};
		std::atomic<bool> RanOffOwner{false};
	};
	FWorkerProbe Probe;
	Probe.OwnerThread = std::this_thread::get_id();
	std::ostringstream ConcurrentOutput;
	std::ostringstream ConcurrentErrors;
	FStreamEngineHost ConcurrentHost(ConcurrentOutput, ConcurrentErrors);
	VeroScriptSession Session(ConcurrentHost);
	VeroScript::Test::Configure(Session);
	FEngineConfig ConcurrentConfig;
	VeroScript::Test::Configure(ConcurrentConfig);
	ConcurrentConfig.StdlibSearchPaths = {VEROSCRIPT_STDLIB_DIR};
	Session.ApplyConfig(ConcurrentConfig);
	if (!Session.RegisterNative(
	        "observe_worker",
	        ENativeType::Void,
	        {},
	        [](const FNativeCallContext& Context, FValue&, std::string&) -> bool
	{
		auto* const WorkerProbe = static_cast<FWorkerProbe*>(Context.UserData);
		if (!WorkerProbe)
		{
			return false;
		}
		WorkerProbe->RanOffOwner.store(
		    std::this_thread::get_id() != WorkerProbe->OwnerThread,
		    std::memory_order_relaxed);
		WorkerProbe->Calls.fetch_add(1, std::memory_order_relaxed);
		return true;
	},
	        &Probe))
	{
		std::cerr << "failed to register concurrent host callback\n";
		return 1;
	}
	const FRunResult ConcurrentLoad = Session.LoadSource(
	    "import \"concurrent.vs\";\n"
	    "export void host_worker(channel<int> output) {\n"
	    "    observe_worker();\n"
	    "    output.send(42);\n"
	    "}\n"
	    "void main() {\n"
	    "    task<int> work = std::concurrent::spawn_task<int>(\"host_worker\");\n"
	    "    print(work.await());\n"
	    "}\n",
	    "concurrent_host.vs");
	if (!ConcurrentLoad.bSuccess)
	{
		std::cerr << "failed to load concurrent host callback regression\n"
		          << ConcurrentErrors.str();
		return 1;
	}
	const FRunResult ConcurrentRun = Session.Run();
	if (!ConcurrentRun.bSuccess || ConcurrentOutput.str() != "42\n" || Probe.Calls.load(std::memory_order_relaxed) != 1 || !Probe.RanOffOwner.load(std::memory_order_relaxed))
	{
		std::cerr << "concurrent host callback contract was not preserved\n"
		          << ConcurrentErrors.str();
		return 1;
	}

	const std::string CallableSource =
	    "import \"concurrent.vs\";\n"
	    "int twice(int value) { return value * 2; }\n"
	    "fn(int)->int saved = twice;\n"
	    "int execute(fn(int)->int work) {\n"
	    "    task<int> result = std::concurrent::spawn_task<int>(work, 21);\n"
	    "    return result.await();\n"
	    "}\n"
	    "void main() {}\n";
	std::ostringstream CallableOutput;
	std::ostringstream CallableErrors;
	FStreamEngineHost CallableHost(CallableOutput, CallableErrors);
	VeroScriptSession CallableSession(CallableHost);
	VeroScript::Test::Configure(CallableSession);
	CallableSession.SetHotReloadEnabled(true);
	CallableSession.ApplyConfig(ConcurrentConfig);
	FValue SavedCallable;
	FValue CallableResult;
	if (!CallableSession.LoadSource(CallableSource, "callable_host.vs").bSuccess || !CallableSession.Run(ERunMode::Eval).bSuccess || !CallableSession.GetGlobal("saved", SavedCallable).bSuccess || !CallableSession.CallFunction("execute", {SavedCallable}, &CallableResult).bSuccess || !std::holds_alternative<std::int32_t>(CallableResult) || std::get<std::int32_t>(CallableResult) != 42)
	{
		std::cerr << "C++ session did not preserve callable ownership across VM calls\n"
		          << CallableErrors.str();
		return 1;
	}
	std::ostringstream ForeignOutput;
	std::ostringstream ForeignErrors;
	FStreamEngineHost ForeignHost(ForeignOutput, ForeignErrors);
	VeroScriptSession ForeignSession(ForeignHost);
	VeroScript::Test::Configure(ForeignSession);
	ForeignSession.SetHotReloadEnabled(true);
	ForeignSession.ApplyConfig(ConcurrentConfig);
	if (!ForeignSession.LoadSource(CallableSource, "foreign_callable_host.vs").bSuccess || !ForeignSession.Run(ERunMode::Eval).bSuccess || ForeignSession.CallFunction("execute", {SavedCallable}, &CallableResult).bSuccess)
	{
		std::cerr << "C++ session accepted a foreign callable\n"
		          << ForeignErrors.str();
		return 1;
	}

	return 0;
}
