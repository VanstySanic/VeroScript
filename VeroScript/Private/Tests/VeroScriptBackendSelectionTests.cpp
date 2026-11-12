// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBackendSelection.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptNativeExtension.h"
#include "VeroScriptRun.h"
#include <iostream>
#include <stdexcept>

using namespace VeroScript;

namespace
{
	struct FFactoryState
	{
		std::string Id;
		std::shared_ptr<std::vector<std::string>> Calls;
		bool bAvailable = true;
	};

	FNativeServiceProviderDesc Provider(const char* Id,
	    const std::shared_ptr<std::vector<std::string>>& Calls, const bool Available = true)
	{
		auto State = std::make_shared<FFactoryState>();
		State->Id = Id;
		State->Calls = Calls;
		State->bAvailable = Available;
		FNativeServiceProviderDesc Result;
		Result.Owner = State;
		Result.UserData = State.get();
		Result.ServiceId = "paint";
		Result.BackendId = Id;
		Result.ContractId = "paint.contract";
		Result.ContractVersion = "0.1.1";
		Result.Create = [](const auto&, void* Data, std::string& Error) -> void*
		{
			auto& Factory = *static_cast<FFactoryState*>(Data);
			Factory.Calls->push_back(Factory.Id);
			if (!Factory.bAvailable)
			{
				Error = "provider is unavailable on this host";
				return nullptr;
			}
			return new int(42);
		};
		Result.Destroy = [](void* Value, void* Data)
		{
			static_cast<FFactoryState*>(Data)->Calls->push_back("destroy");
			delete static_cast<int*>(Value);
		};
		return Result;
	}

	bool CheckFactories()
	{
		auto Calls = std::make_shared<std::vector<std::string>>();
		std::vector<FNativeServiceProviderDesc> Providers{
		    Provider("z-ready", Calls), Provider("a-unavailable", Calls, false), Provider("b-incompatible", Calls)};
		Providers.back().ContractVersion = "different";
		std::string Error;
		FBackendSelectionResult Selection;
		auto Instance = CreateNativeService(Providers, "paint", "auto", "paint.contract", "0.1.1", {}, Error, &Selection);
		if (!Instance || !Error.empty() || *static_cast<int*>(Instance->Get()) != 42 || Selection.BackendId != "z-ready" || Selection.Rejected.size() != 2 || *Calls != std::vector<std::string>{"a-unavailable", "z-ready"})
		{
			return false;
		}
		Instance.reset();
		Calls->clear();
		Instance = CreateNativeService(Providers, "paint", "a-unavailable", "paint.contract", "0.1.1", {}, Error, &Selection);
		if (Instance || Error.empty() || Selection.State != EBackendSelectionState::Failed || *Calls != std::vector<std::string>{"a-unavailable"})
		{
			return false;
		}
		Calls->clear();
		Instance = CreateNativeService(Providers, "paint", "none", "paint.contract", "0.1.1", {}, Error, &Selection);
		if (Instance || Selection.State != EBackendSelectionState::Disabled || !Calls->empty()) return false;

		FModuleEnvironmentDesc Desc;
		Desc.EntryOwnerId = "entry";
		Desc.Scopes.push_back({"entry", {}, false});
		Desc.SelectedBackends["paint"] = "";
		auto Environment = FModuleEnvironment::Create(Desc, Error);
		if (!Environment) return false;
		FNativeServiceCreateContext Context;
		Context.Environment = Environment;
		Instance = CreateNativeService(Providers, "paint", {}, "paint.contract", "0.1.1", Context, Error, &Selection);
		if (Instance || Selection.State != EBackendSelectionState::Disabled || !Calls->empty()) return false;
		Instance = CreateNativeService(Providers, "paint", "auto", "paint.contract", "0.1.1", Context, Error, &Selection);
		if (!Instance || Selection.BackendId != "z-ready") return false;
		const auto Owner = std::weak_ptr<void>(Providers.front().Owner);
		Providers.clear();
		if (Owner.expired()) return false;
		Instance.reset();
		return Owner.expired() && Calls->back() == "destroy";
	}

	bool CheckPolicy()
	{
		std::map<std::string, std::string> Settings{{"paint", "preferred"}, {"sound", ""}};
		if (ResolveBackendSelection("paint", {}, Settings) != "preferred" || ResolveBackendSelection("paint", "auto", Settings) != AutomaticBackend || ResolveBackendSelection("sound", {}, Settings) != DisabledBackend || ResolveBackendSelection("unknown", {}, Settings) != AutomaticBackend)
		{
			return false;
		}
		int Attempts = 0;
		const auto Try = [&](std::string_view, std::string& Error)
		{
			++Attempts;
			Error = "unavailable";
			return false;
		};
		if (SelectServiceBackend("paint", "auto", {}, Try).State != EBackendSelectionState::Unavailable || SelectServiceBackend("paint", "auto", {"same", "same"}, Try).State != EBackendSelectionState::Failed || Attempts != 0)
		{
			return false;
		}
		try
		{
			(void)SelectServiceBackend("paint", "auto", {"first", "second"}, Try,
			    []
			{
				throw std::runtime_error("cancelled");
			});
			return false;
		}
		catch (const std::runtime_error&)
		{
			return Attempts == 0;
		}
	}

	bool CheckExecutionDefaults()
	{
		int JitQueries = 0, VmQueries = 0;
		FModuleEnvironmentDesc Desc;
		Desc.EntryOwnerId = "entry";
		Desc.Scopes.push_back({"entry", {}, false});
		Desc.ExecutionBackends.Identity = "selection-test";
		Desc.ExecutionBackends.Vm = [&](auto, auto&, auto&)
		{
			++VmQueries;
			return true;
		};
		Desc.ExecutionBackends.Jit = [&](auto, auto&, auto&)
		{
			++JitQueries;
			return true;
		};
		std::string Error;
		FEngineConfig Request, Output;
		Request.ModuleEnvironment = FModuleEnvironment::Create(Desc, Error);
		if (!Request.ModuleEnvironment || !ResolveExecutionBackends(Request, Output, Error) || JitQueries != 1 || VmQueries != 1)
		{
			return false;
		}
		Request.ExecutionBackend = EExecutionBackend::Vm;
		if (!ResolveExecutionBackends(Request, Output, Error) || JitQueries != 1 || VmQueries != 2) return false;
		Desc.SelectedBackends = {{"vm", "none"}, {"jit", ""}};
		Request.ModuleEnvironment = FModuleEnvironment::Create(Desc, Error);
		Request.ExecutionBackend = EExecutionBackend::Mixed;
		if (!ResolveExecutionBackends(Request, Output, Error) || !Output.bClearVmBackend || !Output.bDisableJitBackend || JitQueries != 1 || VmQueries != 2)
		{
			return false;
		}
		Desc.SelectedBackends.clear();
		Desc.ExecutionBackends.Vm = [](auto, auto&, std::string& Failure)
		{
			Failure = "explicit backend failed";
			return false;
		};
		Request.ModuleEnvironment = FModuleEnvironment::Create(Desc, Error);
		Output.MaxCallDepth = 123;
		return !ResolveExecutionBackends(Request, Output, Error) && Error == "explicit backend failed" && Output.MaxCallDepth == 123;
	}
}

int main()
{
	if (!CheckPolicy() || !CheckFactories() || !CheckExecutionDefaults())
	{
		std::cerr << "service backend selection failed\n";
		return 1;
	}
	std::cout << "service backend selection passed\n";
	return 0;
}
