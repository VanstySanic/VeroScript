// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBackendSelection.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptRun.h"
#include <algorithm>

namespace VeroScript
{
	std::string ResolveBackendSelection(const std::string_view Service, const std::string_view Request,
		const std::map<std::string, std::string>& Configuration)
	{
		if (!Request.empty())
		{
			return std::string(Request);
		}
		const auto Found = Configuration.find(std::string(Service));
		if (Found == Configuration.end())
		{
			return std::string(AutomaticBackend);
		}
		return Found->second.empty() ? std::string(DisabledBackend) : Found->second;
	}

	FBackendSelectionResult SelectServiceBackend(const std::string_view Service,
		const std::string_view Selection, std::vector<std::string> Candidates,
		const std::function<bool(std::string_view, std::string&)>& TryCandidate,
		const std::function<void()>& CheckCancelled)
	{
		FBackendSelectionResult Result;
		if (Selection == DisabledBackend)
		{
			Result.State = EBackendSelectionState::Disabled;
			return Result;
		}
		const bool bAutomatic = Selection.empty() || Selection == AutomaticBackend;
		if (!bAutomatic)
		{
			Candidates = {std::string(Selection)};
		}
		std::sort(Candidates.begin(), Candidates.end());
		if (!TryCandidate || std::any_of(Candidates.begin(), Candidates.end(), [](const auto& Candidate)
			{
				return Candidate.empty() || Candidate == AutomaticBackend || Candidate == DisabledBackend;
			}))
		{
			Result.State = EBackendSelectionState::Failed;
			Result.Error = "invalid backend candidate or missing creation callback: " + std::string(Service);
			return Result;
		}
		if (std::adjacent_find(Candidates.begin(), Candidates.end()) != Candidates.end())
		{
			Result.State = EBackendSelectionState::Failed;
			Result.Error = "multiple providers for the same service/backend identity: " + std::string(Service);
			return Result;
		}
		for (const auto& Candidate : Candidates)
		{
			if (CheckCancelled)
			{
				CheckCancelled();
			}
			std::string Failure;
			const bool bSelected = TryCandidate(Candidate, Failure);
			if (CheckCancelled)
			{
				CheckCancelled();
			}
			if (bSelected)
			{
				Result.State = EBackendSelectionState::Selected;
				Result.BackendId = Candidate;
				return Result;
			}
			if (Failure.empty())
			{
				Failure = "backend is unavailable";
			}
			Result.Rejected.push_back({Candidate, std::move(Failure)});
		}
		Result.State = bAutomatic ? EBackendSelectionState::Unavailable : EBackendSelectionState::Failed;
		Result.Error = "no usable backend for service: " + std::string(Service);
		for (const auto& Rejected : Result.Rejected)
		{
			Result.Error += "\n" + Rejected.BackendId + ": " + Rejected.Error;
		}
		return Result;
	}

	bool ResolveExecutionBackends(const FEngineConfig& Input, FEngineConfig& Output, std::string& Error)
	{
		Error.clear();
		FEngineConfig Prepared = Input;
		if ((Prepared.VmBackendFactory && !Prepared.VmBackendPath.empty())
			|| (Prepared.FallbackVmBackendFactory && !Prepared.FallbackVmBackendPath.empty())
			|| (Prepared.bClearVmBackend && (Prepared.VmBackendFactory || !Prepared.VmBackendPath.empty())))
		{
			Error = "VM factory, path and clear settings are exclusive";
			return false;
		}
		if (Prepared.ModuleEnvironment)
		{
			const auto& Environment = Prepared.ModuleEnvironment->GetDescription();
			if (!Prepared.VmBackendFactory && Prepared.VmBackendPath.empty()
				&& ResolveBackendSelection("vm", {}, Environment.SelectedBackends) == DisabledBackend)
			{
				Prepared.bClearVmBackend = true;
			}
			if (!Prepared.JitBackendFactory
				&& ResolveBackendSelection("jit", {}, Environment.SelectedBackends) == DisabledBackend)
			{
				Prepared.bDisableJitBackend = true;
			}
			if (!Prepared.bClearVmBackend && !Prepared.VmBackendFactory && Prepared.VmBackendPath.empty()
				&& Environment.ExecutionBackends.Vm
				&& !Environment.ExecutionBackends.Vm({}, Prepared.VmBackendFactory, Error))
			{
				return false;
			}
			if (!Prepared.bDisableJitBackend && !Prepared.JitBackendFactory
				&& Prepared.ExecutionBackend != EExecutionBackend::Vm && Environment.ExecutionBackends.Jit
				&& !Environment.ExecutionBackends.Jit({}, Prepared.JitBackendFactory, Error))
			{
				return false;
			}
		}
		if (!Prepared.bClearVmBackend && !Prepared.VmBackendFactory && Prepared.VmBackendPath.empty())
		{
			const bool bDisabled = Prepared.ModuleEnvironment
				&& ResolveBackendSelection("vm", {}, Prepared.ModuleEnvironment->GetDescription().SelectedBackends) == DisabledBackend;
			if (bDisabled)
			{
				Prepared.bClearVmBackend = true;
			}
			else
			{
				Prepared.VmBackendFactory = Prepared.FallbackVmBackendFactory;
				Prepared.VmBackendPath = Prepared.FallbackVmBackendPath;
			}
		}
		if (Prepared.bDisableJitBackend)
		{
			Prepared.JitBackendFactory.reset();
		}
		Output = std::move(Prepared);
		return true;
	}
}
