// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptTestEngine.h"
#include "../../../../../../VeroScript/Private/Tests/VeroScriptTestEngine.h"
#include "VeroScriptEngine.h"
#include "VeroScriptHost.h"
#include "VeroScriptJitBackendLoader.h"
#include "VeroScriptMemory.h"
#include "VeroScriptPath.h"
#include "VeroScriptSession.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>

namespace
{
	using namespace VeroScript;

	void Require(const bool Condition, const char* Message)
	{
		if (!Condition)
		{
			throw std::runtime_error(Message);
		}
	}

	void Require(const FMemoryResult Result)
	{
		if (!Result)
		{
			throw std::runtime_error(Result.MessageId ? Result.MessageId : "memory operation failed");
		}
	}

	void Require(const FRunResult& Result)
	{
		if (!Result.bSuccess)
		{
			throw std::runtime_error(Result.Error.Message);
		}
	}

	struct FTemporaryDirectory
	{
		FTemporaryDirectory()
			: Path(std::filesystem::temp_directory_path() / ("veroscript_cpp_memory_"
				+ std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
		{
			std::filesystem::create_directories(Path);
		}
		~FTemporaryDirectory()
		{
			std::error_code Error;
			std::filesystem::remove_all(Path, Error);
		}
		std::filesystem::path Path;
	};

	struct FLeaseScope
	{
		FLeaseScope() = default;
		FLeaseScope(const FLeaseScope&) = delete;
		FLeaseScope& operator=(const FLeaseScope&) = delete;
		~FLeaseScope()
		{
			if (Lease.Token.Serial)
			{
				(void)Memory::ReleaseLease(Lease.Token);
			}
		}
		FMemoryLease Lease;
	};

	void TestEngineLimits(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		FTemporaryDirectory Temporary;
		const auto Path = Temporary.Path / "limits.vs";
		const std::string Source = "void main(){auto first=new std::memory::buffer(8);"
			"auto second=new std::memory::buffer(8);print(first.size()+second.size());}";
		{
			std::ofstream File(Path, std::ios::binary);
			File << Source;
			Require(static_cast<bool>(File), "write engine quota fixture");
		}
		for (const auto Backend : {EExecutionBackend::Vm, EExecutionBackend::Mixed, EExecutionBackend::Jit})
		{
			if (Backend == EExecutionBackend::Jit && !Factory)
			{
				continue;
			}
			for (const bool FromFile : {false, true})
			{
				for (const auto Limits : {FMemoryLimits{4, 0}, FMemoryLimits{0, 12},
					FMemoryLimits{4, 64}, FMemoryLimits{16, 12}, FMemoryLimits{}})
				{
					std::ostringstream Output, Errors;
					FStreamEngineHost Host(Output, Errors);
					FEngineConfig Config;
					VeroScript::Test::Configure(Config);
					Config.ExecutionBackend = Backend;
					Config.JitBackendFactory = Factory;
					Config.MemoryLimits = Limits;
					Config.bUseBytecodeCache = false;
					const auto Result = FromFile
						? VeroScript::Test::FEngine::RunFile(PathUtil::ToUtf8(Path), Host, Config)
						: VeroScript::Test::FEngine::RunSource(Source, "<memory-limits>", Host, ERunMode::Script, Config);
					if (Limits.MaxBufferBytes || Limits.MaxRetainedBytes)
					{
						Require(!Result.bSuccess && Result.Error.MessageId == "diag.memory_limit"
							&& Output.str().empty(), "engine did not enforce configured memory limits");
					}
					else
					{
						Require(Result);
						Require(Output.str() == "16\n", "zero memory quotas must remain unlimited");
					}
				}
			}
		}
	}

	void TestPersistentSession(const std::shared_ptr<const IJitBackendFactory>& Factory)
	{
		struct FProbe
		{
			VeroScriptSession* Session = nullptr;
			unsigned Calls = 0;
		} Probe;
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Probe.Session = &Session;
		FEngineConfig Config;
		VeroScript::Test::Configure(Config);
		Config.MemoryLimits = {16, 64};
		Config.JitBackendFactory = Factory;
		Session.ApplyConfig(Config);
		Session.SetHotReloadEnabled(true);
		Require(Session.RegisterNative("probe_memory", ENativeType::Void, {},
			[](const FNativeCallContext& Call, FValue&, std::string& Error)
			{
				auto& Probe = *static_cast<FProbe*>(Call.UserData);
				if (!Call.MemoryServices
					|| &Call.MemoryServices->GetMemoryContext() != &Probe.Session->GetMemoryContext()
					|| Call.MemoryServices->MemoryHostBindings() != &Probe.Session->GetBindingCatalog()
					|| Call.MemoryServices->MemoryAddressesAllowed()
						!= Probe.Session->GetCapabilities().bAllowMemoryAddress
					|| !Call.MemoryServices->GetMemoryWork().Check
					|| Probe.Session->SetMemoryLimits({32, 128}).Status != EMemoryStatus::Busy
					|| Call.MemoryServices->GetMemoryContext().SetLimits({32, 128}).Status != EMemoryStatus::Busy)
				{
					Error = "native callback did not receive the active Session memory services";
					return false;
				}
				++Probe.Calls;
				return true;
			}, &Probe), "register memory probe");
		Require(Session.LoadSource(
			"std::memory::buffer packet=new std::memory::buffer(4);"
			"std::memory::buffer make(int n){return new std::memory::buffer(n);}"
			"int inspect(){probe_memory();return packet.get(0);} fn()->int saved=inspect;"
			"int memory_size(){return packet.size();} fn()->int saved_memory=memory_size;"
			"class Holder{public:std::memory::buffer bytes=new std::memory::buffer(2);"
			"int size(){return bytes.size();}} void main(){}", "<session-memory>"));
		Require(Session.Run(ERunMode::Eval));
		Require(Session.GetMemoryStats().OwnedBytes == 4, "initial Session memory accounting");
		FValue Bytes;
		Require(Session.CallFunction("make", {std::int32_t{8}}, &Bytes));
		auto& Context = Session.GetMemoryContext();
		Require(Memory::Write(Context, Bytes, 0, "uint8", EMemoryByteOrder::Little, std::uint8_t{42}));
		Require(Session.SetGlobal("packet", Bytes));
		FValue Value;
		Require(Session.CallFunction("inspect", {}, &Value));
		Require(std::get<std::int32_t>(Value) == 42, "memory survives successive function VMs");
		Require(Session.GetGlobal("saved", Value));
		const auto Callable = Session.RetainCallable(std::get<FFunctionHandle>(Value));
		Require(static_cast<bool>(Callable), "retain memory callable");
		Require(Session.InvokeCallable(Callable, {}, &Value));
		Require(std::get<std::int32_t>(Value) == 42 && Probe.Calls == 2, "callable receives Session memory context");
		Require(Session.ReleaseCallable(Callable), "release memory callable");
		auto Restricted = Session.GetCapabilities();
		Restricted.bAllowMemoryAddress = false;
		Session.SetCapabilities(Restricted);
		Require(Session.CallFunction("inspect", {}, &Value));
		Require(std::get<std::int32_t>(Value) == 42 && Probe.Calls == 3,
			"native memory services must reflect address restrictions without denying byte access");
		Require(Session.GetGlobal("saved_memory", Value));
		const auto CrossThread = Session.RetainCallable(std::get<FFunctionHandle>(Value), false);
		Require(static_cast<bool>(CrossThread), "retain callable without owner-thread restriction");
		FRunResult WrongThread;
		// The owner remains idle until join: the Session is externally serialized.
		std::thread Caller(
			[&]
			{
				WrongThread = Session.InvokeCallable(CrossThread);
			});
		Caller.join();
		Require(!WrongThread.bSuccess && WrongThread.Error.MessageId == "diag.memory_wrong_thread",
			"cross-thread callable permission must not transfer the Session memory domain");
		Require(Session.InvokeCallable(CrossThread, {}, &Value));
		Require(std::get<std::int32_t>(Value) == 8, "wrong-thread failure closed the owner memory domain");
		Require(Session.ReleaseCallable(CrossThread), "release cross-thread memory callable");
		FObjectHandle Object;
		Require(Session.ConstructObject("Holder", {}, Object));
		Require(Session.CallMethod(Object, "size", {}, &Value));
		Require(std::get<std::int32_t>(Value) == 2, "constructor and method share memory context");
		Require(Session.SetMemoryLimits({4, 64}).Status == EMemoryStatus::LimitExceeded,
			"limit reduction below live capacity must fail");
		auto Rejected = Config;
		Rejected.MemoryLimits = {4, 64};
		Rejected.ExecutionBackend = EExecutionBackend::Vm;
		Rejected.Capabilities.bAllowConsoleOutput = false;
		Session.ApplyConfig(Rejected);
		Require(Context.GetLimits().MaxBufferBytes == 16
			&& Session.GetExecutionBackend() == Config.ExecutionBackend
			&& Session.GetCapabilities().bAllowConsoleOutput
			&& !Session.GetCapabilities().bAllowMemoryAddress, "failed ApplyConfig partially changed execution settings");
		Session.Clear();
		std::int64_t Size = 0;
		Require(Memory::Size(Context, Bytes, Size));
		Require(Size == 8, "Clear must preserve host-owned memory values and access domain");
		Require(Session.LoadSource("void main(){print(7);}", "<after-clear>"));
		Require(Session.Run());
		Require(Output.str() == "7\n", "Session remains reusable after memory calls and Clear");
	}

	void TestMemoryReload()
	{
		FTemporaryDirectory Temporary;
		const auto Path = Temporary.Path / "reload.vs";
		const auto WriteVersion = [&](const int Increment, const bool FailInitialization)
		{
			std::ofstream File(Path, std::ios::binary);
			File << "std::memory::buffer packet=new std::memory::buffer(4);"
				<< (FailInitialization ? "std::memory::buffer oversized=new std::memory::buffer(100);" : "")
				<< "int read(){int value=packet.get(0);return value+" << Increment << ";} void main(){}";
			Require(static_cast<bool>(File), "write memory reload fixture");
		};
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
		Require(Session.SetMemoryLimits({16, 32}));
		Session.SetHotReloadEnabled(true);
		WriteVersion(1, false);
		Require(Session.LoadFile(PathUtil::ToUtf8(Path)));
		Require(Session.Run(ERunMode::Eval));
		FValue Bytes, Value;
		Require(Session.GetGlobal("packet", Bytes));
		Require(Memory::Write(Session.GetMemoryContext(), Bytes, 0, "uint8",
			EMemoryByteOrder::Little, std::uint8_t{42}));
		WriteVersion(2, false);
		Require(Session.Reload());
		Require(Session.Run(ERunMode::Eval));
		Require(Session.GetGlobal("packet", Value));
		Require(std::get<FHostObjectHandle>(Value) == std::get<FHostObjectHandle>(Bytes),
			"hot reload must preserve memory region identity");
		Require(Session.CallFunction("read", {}, &Value));
		Require(std::get<std::int32_t>(Value) == 44, "reloaded code lost memory contents");
		WriteVersion(3, true);
		Require(Session.Reload());
		const auto Failed = Session.Run(ERunMode::Eval);
		Require(!Failed.bSuccess && Failed.Error.MessageId == "diag.memory_limit",
			"memory quota failure must abort replacement initialization");
		Require(Session.CallFunction("read", {}, &Value));
		Require(std::get<std::int32_t>(Value) == 44 && Session.GetMemoryStats().OwnedBytes == 4,
			"failed memory initialization must restore the previous program without retained allocations");
	}

	void TestShutdownLease()
	{
		std::ostringstream Output, Errors;
		FStreamEngineHost Host(Output, Errors);
		std::atomic<unsigned> Releases{0};
		auto Storage = std::make_shared<std::array<std::uint8_t, 2>>();
		const std::weak_ptr<std::array<std::uint8_t, 2>> StorageLifetime = Storage;
		(*Storage)[0] = 21;
		FValue Region;
		FLeaseScope Scope;
		auto& Lease = Scope.Lease;
		{
			VeroScriptSession Session(Host);
		VeroScript::Test::Configure(Session);
			Session.SetHotReloadEnabled(true);
			Require(Session.LoadSource(
				"class Reader{public:std::memory::buffer bytes=new std::memory::buffer(1);"
				"Reader(){bytes.set(0,7);}~Reader(){print(bytes.get(0));}}"
				"Reader reader=Reader(); void main(){}", "<memory-shutdown>"));
			Require(Session.Run(ERunMode::Eval));
			Require(Memory::WrapExternalRegion(
				Session.GetMemoryContext(), Storage->data(), Storage->size(), true, Storage,
				[](void* User)
				{
					static_cast<std::atomic<unsigned>*>(User)->fetch_add(1);
				},
				&Releases, Region));
			Require(Memory::AcquireLease(Session.GetMemoryContext(), Region, false, Lease));
			Storage.reset(); // Only the registered region/lease now owns the bytes.
		}
		Require(Output.str() == "7\n", "script cleanup lost Session memory services before destruction");
		FMemoryContext Other;
		std::int64_t Size = 0;
		Require(Memory::Size(Other, Region, Size).Status == EMemoryStatus::InvalidArgument,
			"region must remain inaccessible from another context after Session destruction");
		Require(Lease.Data && Lease.Data[0] == 21, "native lease must retain bytes beyond Session destruction");
		Region = {};
		Require(Releases.load() == 0, "live lease must delay external release");
		Require(!StorageLifetime.expired(), "lease must retain the external owner without host aliases");
		FMemoryResult Released;
		std::thread Worker(
			[&]
			{
				Released = Memory::ReleaseLease(Lease.Token);
			});
		Worker.join();
		Require(Released);
		Require(Releases.load() == 1, "cross-thread final lease release must invoke external callback once");
		Require(StorageLifetime.expired(), "final lease release must drop external owner storage");
		Require(Memory::ReleaseLease(Lease.Token).Status == EMemoryStatus::Expired, "reject duplicate lease release");
		Lease.Token = {};
	}

	int RunTests(const std::string& BackendPath)
	{
		try
		{
			std::shared_ptr<const IJitBackendFactory> Factory;
			if (!BackendPath.empty())
			{
				std::string Error;
				Factory = LoadJitBackend(BackendPath, Error);
				Require(static_cast<bool>(Factory), Error.c_str());
			}
			TestEngineLimits(Factory);
			TestPersistentSession(Factory);
			TestMemoryReload();
			TestShutdownLease();
		}
		catch (const std::exception& Error)
		{
			std::cerr << Error.what() << '\n';
			return 1;
		}
		return 0;
	}
}

#if defined(_WIN32)
int wmain(const int Count, wchar_t* const Arguments[])
{
	return RunTests(Count > 1 ? VeroScript::PathUtil::ToUtf8(std::filesystem::path(Arguments[1])) : "");
}
#else
int main(const int Count, char* const Arguments[])
{
	return RunTests(Count > 1 ? Arguments[1] : "");
}
#endif
