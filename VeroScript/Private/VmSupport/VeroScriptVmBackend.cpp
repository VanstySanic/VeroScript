// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptVmBackendLoader.h"
#include "VeroScriptExecutionServices.h"
#include "ExecutionSupport/VeroScriptExecutionCleanup.h"

#include <exception>
#include <utility>

namespace VeroScript
{
	bool VeroScriptVm::SetVmBackendFactory(std::shared_ptr<const IVmBackendFactory> Factory, std::string& OutError)
	{
		OutError.clear();
		if (VmExecutionDomain->bFrozen || VmExecutionDomain->ActiveDepth || ActiveModule || !Frames.empty() || ActiveJitCallDepth || SpawnedTasks.HasActiveWorkers())
		{
			OutError = "VM backend selection is fixed after execution begins";
			return false;
		}
		if (Factory)
		{
			const auto& Description = Factory->GetDescription();
			if (Factory->GetHostIdentity() != GetVmHostServices().OwnerIdentity || Description.Id.empty() || Description.Version.empty() || Description.ImplementationId.empty() || Description.CoreVersion != VEROSCRIPT_VERSION_CODE || !(Description.Capabilities & VmBytecodeCapability))
			{
				OutError = "VM backend factory is incompatible with the owning Core";
				return false;
			}
		}
		ReleaseVmBackend();
		VmExecutionDomain->IdleInstance.reset();
		VmExecutionDomain->Factory = std::move(Factory);
		VmExecutionDomain->LibraryPath.clear();
		return true;
	}

	bool VeroScriptVm::SetVmBackendPath(std::string_view Path, std::string& Error)
	{
		const std::string SelectionPath(Path);
		Error.clear();
		if (VmExecutionDomain->bFrozen || VmExecutionDomain->ActiveDepth || ActiveModule || !Frames.empty() || ActiveJitCallDepth || SpawnedTasks.HasActiveWorkers())
		{
			Error = "VM backend selection is fixed after execution begins";
			return false;
		}
		std::string Resolved;
		auto Factory = SelectionPath.empty() ? std::shared_ptr<const IVmBackendFactory>{} : LoadVmBackend(SelectionPath, Error, &Resolved);
		if (!SelectionPath.empty() && !Factory) return false;
		if (!SetVmBackendFactory(std::move(Factory), Error)) return false;
		VmExecutionDomain->LibraryPath = std::move(Resolved);
		return true;
	}

	bool VeroScriptVm::SetVmExecutionDomain(std::shared_ptr<FVmExecutionDomain> Domain, std::string& OutError)
	{
		OutError.clear();
		if (!Domain || VmExecutionDomain->bFrozen || ActiveModule || !Frames.empty() || ActiveJitCallDepth || SpawnedTasks.HasActiveWorkers() || (Domain->Factory && Domain->Factory->GetHostIdentity() != GetVmHostServices().OwnerIdentity))
		{
			OutError = "cannot attach this VM execution domain";
			return false;
		}
		ReleaseVmBackend();
		VmExecutionDomain = std::move(Domain);
		return true;
	}

	bool VeroScriptVm::PrepareVmBackend()
	{
		const auto Factory = VmExecutionDomain->Factory;
		if (!Factory)
		{
			ReportErrorId("diag.vm_backend_required");
			if (ExecutionCleanup)
			{
				MarkExecutionResetRequired();
			}
			return false;
		}
		std::uint64_t Required = VmBytecodeCapability;
		if (HasExecutionGovernance())
		{
			Required |= VmGovernanceCapability;
		}
		if (Debugger)
		{
			Required |= VmDebugCapability;
		}
		if (bPersistentExecution || bHotReloadModuleInitializing)
		{
			Required |= VmHotReloadCapability;
		}
		if (ExecutionCleanup)
		{
			Required |= VmCleanupCapability;
		}
		if ((Factory->GetDescription().Capabilities & Required) != Required)
		{
			ReportErrorId("diag.vm_backend_capabilities", {}, {Factory->GetDescription().Id});
			if (ExecutionCleanup)
			{
				MarkExecutionResetRequired();
			}
			return false;
		}
		if (VmBackendInstance && VmBoundProgram == ActiveModule && VmBoundGeneration == ProgramGeneration) return true;
		if (VmEntryDepth)
		{
			ReportErrorId("diag.vm_backend_contract", {}, {"active VM binding changed"});
			MarkExecutionResetRequired();
			bExecutionStateInvalid = true;
			return false;
		}
		try
		{
			if (VmBackendInstance && !VmBackendInstance->SupportsIdleRebinding())
			{
				VmBackendInstance->Unbind();
				VmBackendInstance.reset();
			}
			if (!VmBackendInstance && VmExecutionDomain->IdleInstance && VmExecutionDomain->IdleThread == std::this_thread::get_id())
			{
				VmBackendInstance = std::move(VmExecutionDomain->IdleInstance);
				++VmBackendStats.Rebindings;
			}
			if (!VmBackendInstance)
			{
				std::string Error;
				auto* Instance = Factory->CreateInstance(Error);
				if (!Instance)
				{
					ReportErrorId("diag.vm_backend_failed", {}, {Error.empty() ? "instance creation failed" : Error});
					return false;
				}
				VmBackendInstance = std::shared_ptr<IVmBackendInstance>(Instance, [Factory](IVmBackendInstance* Value)
				{
					Factory->DestroyInstance(Value);
				});
				++VmBackendStats.Instances;
			}
			std::string Error;
			if (!VmBackendInstance->Bind(static_cast<FExecutionContext*>(this), ActiveModule, ProgramGeneration, Error))
			{
				VmBackendInstance.reset();
				ReportErrorId("diag.vm_backend_failed", {}, {Error.empty() ? "instance binding failed" : Error});
				return false;
			}
			VmBoundProgram = ActiveModule;
			VmBoundGeneration = ProgramGeneration;
			return true;
		}
		catch (const std::exception& Exception)
		{
			ReportErrorId("diag.vm_backend_failed", {}, {Exception.what()});
		}
		catch (...)
		{
			ReportErrorId("diag.vm_backend_failed", {}, {"instance preparation raised an exception"});
		}
		VmBackendInstance.reset();
		return false;
	}

	void VeroScriptVm::ReleaseVmBackend() noexcept
	{
		if (!VmBackendInstance || VmEntryDepth) return;
		VmBackendInstance->Unbind();
		if (!RequiresReset() && Frames.empty() && OperandStack.empty() && VmBackendInstance->SupportsIdleRebinding() && !VmExecutionDomain->IdleInstance)
		{
			VmExecutionDomain->IdleThread = std::this_thread::get_id();
			VmExecutionDomain->IdleInstance = std::move(VmBackendInstance);
		}
		VmBackendInstance.reset();
		VmBoundProgram = nullptr;
		VmBoundGeneration = 0;
	}

	bool VeroScriptVm::ExecuteVmBackend(IVmExecutionServices& Services, FVmExecutionView& View,
	    std::uint64_t CallToken, bool bContinuation, bool bInstrumented, bool bGoverned)
	{
		if (!PrepareVmBackend())
		{
			if (ExecutionCleanup)
			{
				MarkExecutionResetRequired();
				ExecutionCleanup->bExhausted = true;
			}
			return false;
		}
		FVmInvocation Invocation{Services, View, VmExecutionDomain.get(), static_cast<FExecutionContext*>(this),
		    ActiveModule, ProgramGeneration, CallToken, StopFrameDepth, bContinuation, bInstrumented, bGoverned};
		const auto Boundary = StopFrameDepth;
		const auto Program = ActiveModule;
		const auto Generation = ProgramGeneration;
		const auto InitialFrames = Frames.size();
		const auto InitialStack = OperandStack.size();
		const auto InitialOffset = Frames.empty() ? 0 : Frames.back().InstructionPointer;
		const bool InitialPendingException = bHasPendingScriptException;
		const bool InitialError = bHasError;
		const bool InitialExit = bExitRequested;
		const bool WasFrozen = VmExecutionDomain->bFrozen;
		struct FEntryGuard
		{
			FEntryGuard(std::size_t& InDepth, std::size_t& InDomainDepth) : Depth(InDepth), DomainDepth(InDomainDepth)
			{
				++Depth;
				++DomainDepth;
			}
			~FEntryGuard()
			{
				--DomainDepth;
				--Depth;
			}
			std::size_t& Depth;
			std::size_t& DomainDepth;
		} Guard(VmEntryDepth, VmExecutionDomain->ActiveDepth);
		++VmBackendStats.Entries;
		if (bContinuation)
		{
			++VmBackendStats.Continuations;
		}
		try
		{
			// A synchronous backend/host callback observes a committed domain.
			// Only a verified no-entry result can release this provisional freeze.
			VmExecutionDomain->bFrozen = true;
			const auto Result = VmBackendInstance->Execute(Invocation);
			if (Invocation.bEntered)
			{
				NotifyExecutionCommitted();
			}
			VmBackendStats.RetainedBytes = VmBackendInstance->GetRetainedBytes();
			if (Result == EVmExecutionResult::NotEntered && !Invocation.bEntered && StopFrameDepth == Boundary && Frames.size() == InitialFrames && OperandStack.size() == InitialStack && ActiveModule == Program && ProgramGeneration == Generation && (Frames.empty() || Frames.back().InstructionPointer == InitialOffset) && bHasError == InitialError && bHasPendingScriptException == InitialPendingException && bExitRequested == InitialExit)
			{
				VmExecutionDomain->bFrozen = WasFrozen;
				ReportErrorId("diag.vm_backend_failed", {}, {"selected VM did not enter the prepared call"});
				return false;
			}
			const bool BadResult = (Result != EVmExecutionResult::Returned && Result != EVmExecutionResult::ControlTransfer && Result != EVmExecutionResult::Failed) || !Invocation.bEntered || StopFrameDepth != Boundary || Invocation.FrameBoundary != Boundary || (!bExitRequested && Frames.size() < Boundary) || ActiveModule != Program || ProgramGeneration != Generation || (Result == EVmExecutionResult::Returned && ((bHasError && !InitialError) || (bHasPendingScriptException && !InitialPendingException) || (!bExitRequested && Frames.size() > StopFrameDepth))) || (Result == EVmExecutionResult::ControlTransfer && !bHasError && !bHasPendingScriptException && !bExitRequested) || (Result == EVmExecutionResult::Failed && !bHasError && !bHasPendingScriptException && !bExitRequested);
			if (BadResult)
			{
				ReportErrorId("diag.vm_backend_contract", {}, {"inconsistent execution result (frames=" + std::to_string(Frames.size()) + ", boundary=" + std::to_string(Boundary) + ", stop=" + std::to_string(StopFrameDepth) + ", result=" + std::to_string(static_cast<int>(Result)) + ")"});
				MarkExecutionResetRequired();
				bExecutionStateInvalid = true;
				return false;
			}
			return Result == EVmExecutionResult::Returned;
		}
		catch (const FVmServiceException& Transfer)
		{
			if (Invocation.bEntered)
			{
				NotifyExecutionCommitted();
			}
			std::rethrow_exception(Transfer.GetException());
		}
		catch (const std::exception& Exception)
		{
			if (Invocation.bEntered)
			{
				NotifyExecutionCommitted();
			}
			ReportErrorId("diag.vm_backend_failed", {}, {Exception.what()});
		}
		catch (...)
		{
			if (Invocation.bEntered)
			{
				NotifyExecutionCommitted();
			}
			ReportErrorId("diag.vm_backend_failed", {}, {"execution raised a native exception"});
		}
		MarkExecutionResetRequired();
		bExecutionStateInvalid = true;
		return false;
	}
}
