// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptSession.h"
#include "VeroScriptVm.h"
#include "VeroScriptVariant.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptHost.h"
#include "VeroScriptEngineInternal.h"
#include "ExecutionSupport/VeroScriptExecutionCleanup.h"
#include <algorithm>
#include <sstream>

namespace VeroScript
{
	VeroScriptSession::FWeakSlotScope::FWeakSlotScope(
	    VeroScriptVm& InVm, FHotReloadState* State, VeroScriptVm* InActiveVm)
	    : Vm(InVm), ActiveVm(InActiveVm)
	{
		if (ActiveVm)
		{
			Vm.WeakTargets.swap(ActiveVm->WeakTargets);
		}
		else if (State)
		{
			if (!State->WeakReferences)
			{
				State->WeakReferences = std::make_shared<std::vector<VeroScriptVm::FWeakTarget>>();
			}
			Persistent = State->WeakReferences;
			Vm.WeakTargets.swap(*std::static_pointer_cast<std::vector<VeroScriptVm::FWeakTarget>>(Persistent));
		}
	}
	VeroScriptSession::FWeakSlotScope::~FWeakSlotScope()
	{
		if (ActiveVm)
		{
			Vm.WeakTargets.swap(ActiveVm->WeakTargets);
		}
		else if (Persistent)
		{
			Vm.WeakTargets.swap(*std::static_pointer_cast<std::vector<VeroScriptVm::FWeakTarget>>(Persistent));
		}
	}
	VeroScriptSession::FValueOperationScope::FValueOperationScope(VeroScriptVm& InVm, FHotReloadState* InState)
	    : Vm(InVm), State(InState), WeakSlots(InVm, InState)
	{
		// Copy/assignment hooks can call script. Borrow the complete persistent
		// storage without running module initialization again.
		if (State)
		{
			Vm.Globals.swap(State->Globals);
			Vm.LocalStatics.swap(State->LocalStatics);
		}
	}
	VeroScriptSession::FValueOperationScope::~FValueOperationScope()
	{
		if (State)
		{
			Vm.LocalStatics.swap(State->LocalStatics);
			Vm.Globals.swap(State->Globals);
		}
	}
	VeroScriptSession::FValueSafePoint::FValueSafePoint(VeroScriptSession& In,
	    const std::vector<FValue>* InArguments, const FValue* InReturn, const FObjectHandle* InObject)
	    : Session(In), Arguments(InArguments), Return(InReturn), Object(InObject), Previous(In.ActiveValueSafePoint)
	{
		if (Session.PendingHotReloadRollback)
		{
			PendingReloadOperation = Session.ReloadStatus.OperationId;
		}
		if (!Previous && !Session.bRunning && !Session.bCleaningUp)
		{
			Session.ActiveExecutionCleanup.reset();
		}
		Session.ActiveValueSafePoint = this;
		try
		{
			if (!Session.DrainValueRoots(Error) && Error.empty())
			{
				Error = "external value cleanup failed";
			}
		}
		catch (...)
		{
			Session.ActiveValueSafePoint = Previous;
			throw;
		}
	}
	VeroScriptSession::FValueSafePoint::~FValueSafePoint() noexcept
	{
		if (!bFinished)
		{
			try
			{
				std::string Failure;
				Session.DrainValueRoots(Failure);
			}
			catch (...)
			{
			}
		}
		Session.ActiveValueSafePoint = Previous;
	}
	FRunResult VeroScriptSession::FValueSafePoint::Finish(FRunResult Result)
	{
		bFinished = true;
		const bool bDrained = Ready() && Session.DrainValueRoots(Error);
		if (!bDrained && (Result.bSuccess || Result.Error.Message.empty()))
		{
			Result.bSuccess = false;
			Result.ExitCode = 1;
			Result.bExitRequested = false;
			Result.Error = {};
			if (Session.ActiveExecutionCleanup)
			{
				Result.Error = Session.ActiveExecutionCleanup->Primary;
			}
			else
			{
				Result.Error.Message = Error.empty() ? "external value cleanup failed" : Error;
				Result.Error.Location.FileLabel = "<value-cleanup>";
			}
			Result.Diagnostics = {Result.Error};
			FHostStreamBridge Bridge(Session.Host);
			EngineInternal::WriteError(Bridge.ErrorStream(), Result.Error);
		}
		if (Session.ActiveExecutionCleanup)
		{
			const auto Append = [&](const FScriptError& Diagnostic)
			{
				if (std::none_of(Result.Diagnostics.begin(), Result.Diagnostics.end(), [&](const FScriptError& Existing)
				{
					return Existing.MessageId == Diagnostic.MessageId && Existing.Message == Diagnostic.Message;
				}))
				{
					Result.Diagnostics.push_back(Diagnostic);
				}
			};
			Append(Session.ActiveExecutionCleanup->Primary);
			for (const auto& Diagnostic : Session.ActiveExecutionCleanup->Diagnostics)
			{
				Append(Diagnostic);
			}
			if (Session.RequiresReset())
			{
				Append(MakeDiagnostic("diag.execution_state_reset_required"));
			}
		}
		if (PendingReloadOperation != 0 && PendingReloadOperation == Session.ReloadStatus.OperationId &&
		    Session.ReloadStatus.State == EReloadState::Applied && !Result.bSuccess)
		{
			Session.ReloadStatus.State = Session.RequiresReset() ? EReloadState::ResetRequired : EReloadState::AppliedWithErrors;
		}
		return Result;
	}
	bool VeroScriptSession::RetainValue(const FValue& Value, FValueRoot& Output, std::string& Error)
	{
		return RetainValueImpl(Value, Output, Error, false);
	}

	bool VeroScriptSession::RetainMigratableValue(const FValue& Value, FValueRoot& Output, std::string& Error)
	{
		return RetainValueImpl(Value, Output, Error, true);
	}

	bool VeroScriptSession::RetainValueImpl(const FValue& Value, FValueRoot& Output, std::string& Error, const bool Migratable)
	{
		if (PendingHotReloadRollback && ValueRequiresOwner(Value))
		{
			Error = "cannot export module values before replacement initialization commits";
			return false;
		}
		if (bCleaningUp && ValueRequiresOwner(Value))
		{
			Error = "cannot export module values during final graph cleanup";
			return false;
		}
		if (!VisitValueGraph(Value, [&](const FValue& V, const void*)
		{
			if (const auto* H = std::get_if<FObjectHandle>(&V); H && *H)
			{
				return (*H)->ProgramOwner == ProgramOwner && !(*H)->bDestroyed && !(*H)->bDestroying;
			}
			if (const auto* H = std::get_if<FFunctionHandle>(&V); H && *H)
			{
				return (*H)->ProgramOwner == ProgramOwner && (*H)->ProgramGeneration == ProgramGeneration;
			}
			if (Variant::IsVariant(V)) return Variant::ValidateSource(V, ProgramOwner, ProgramGeneration, Error);
			return true;
		}))
		{
			if (Error.empty())
			{
				Error = "cannot retain a foreign, destroyed or excessively nested value";
			}
			return false;
		}
		return Migratable ? ValueRoots->RetainForMigration(Value, Output, Error) : ValueRoots->Retain(Value, Output, Error);
	}

	bool VeroScriptSession::DrainValueRoots(std::string& Error)
	{
		if (bRunning) return true; // callback retirement is drained by the outer scope
		return ValueRoots->Drain([this, &Error](auto& Values, const auto& Keep)
		{
			return CleanValueRoots(Values, Keep, &Error);
		}, Error);
	}

	bool VeroScriptSession::MakeVariant(std::string_view TypeName, const FValue& Payload, FValue& Output, std::string& Error)
	{
		if (!ValueRoots->IsOpen() || !ValueRoots->IsOwnerThread())
		{
			Error = "variant session is closed or accessed from another thread";
			return false;
		}
		const bool WasRunning = bRunning;
		bRunning = true;
		struct FRestore
		{
			bool& Flag;
			bool Old;
			~FRestore()
			{
				Flag = Old;
			}
		} Restore{bRunning, WasRunning};
		// Callback boxing must not lease globals away from an active execution.
		// Idle value copies use the same persistent context as assignment hooks.
		const auto PinnedModule = Module;
		FValueRoot Staging;
		if (!ValueRoots->Reserve(Staging, Error)) return false;
		FBytecodeModule Empty;
		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		if (!ConfigureExecutionVm(Vm))
		{
			Error = Vm.GetError().Message;
			return false;
		}
		FValueOperationScope ValueScope(Vm, WasRunning ? nullptr : HotReloadState.get());
		FValue Prepared;
		if (!Vm.MakeVariant(PinnedModule ? *PinnedModule : Empty, TypeName, Payload, Prepared, &EffectiveBindings().GetNativeFunctions()))
		{
			Error = Vm.GetError().Message;
			return false;
		}
		const bool Dependent = ValueRequiresOwner(Prepared);
		FValue BoxCopy = Prepared; // box handle only; no payload clone or allocation
		if (!ValueRoots->CommitReserved(Staging, Prepared, Error)) return false;
		if (Dependent && !ValueRoots->IsOpen())
		{
			Error = "value owner closed while boxing";
			return false;
		}
		Output = std::move(BoxCopy);
		return true;
	}

	bool VeroScriptSession::CleanValueRoots(std::vector<FValue>& Values, const std::vector<FValue>& Keep,
	    std::string* Error, std::vector<FValue>* ActiveGlobals, VeroScriptVm* ActiveVm) const
	{
		if (!Module)
		{
			Values.clear();
			return true;
		}
		const bool WasRunning = bRunning;
		bRunning = true;
		const bool WasCleaning = bCleaningUp;
		bCleaningUp = true;
		struct FRestore
		{
			bool& Flag;
			bool Old;
			~FRestore()
			{
				Flag = Old;
			}
		} Restore{bRunning, WasRunning};
		FRestore CleanupRestore{bCleaningUp, WasCleaning};
		std::vector<FValue> Live = Keep;
		AppendRollbackKeepValues(Live);
		// An exported wrapper can retire inside a callback while the current
		// call still uses its arguments/receiver or is returning the same graph.
		// Keep those borrowed values until the caller can register its result.
		for (auto* Scope = ActiveValueSafePoint; Scope; Scope = Scope->Previous)
		{
			if (Scope->Arguments)
			{
				Live.insert(Live.end(), Scope->Arguments->begin(), Scope->Arguments->end());
			}
			if (Scope->Return)
			{
				Live.push_back(*Scope->Return);
			}
			if (Scope->Object && *Scope->Object)
			{
				Live.emplace_back(*Scope->Object);
			}
		}
		if (ActiveGlobals && HotReloadState)
		{
			Live.insert(Live.end(), HotReloadState->Globals.begin(), HotReloadState->Globals.end());
		}
		{
			std::lock_guard<std::mutex> Lock(RetainedCallableMutex);
			for (const auto& Entry : RetainedCallables)
			{
				Live.emplace_back(Entry.second.Callable);
			}
		}
		FHostStreamBridge Bridge(Host);
		VeroScriptVm Vm(Bridge.OutputStream(), InputStreamOrCin(), Bridge.ErrorStream(), VmExecutionDomain, ProgramOwner);
		if (!ConfigureExecutionVm(Vm))
		{
			if (Error && Error->empty())
			{
				*Error = Vm.GetError().Message;
			}
			return false;
		}
		if (ActiveVm && ActiveVm->ExecutionCleanup)
		{
			Vm.ExecutionCleanup = ActiveVm->ExecutionCleanup;
			Vm.bBorrowedExecutionCleanup = true;
		}
		Vm.bNativeRetentionAllowed = false;
		Vm.NativeValueRoots.reset();
		Vm.NativeRootCleanup = {};
		const FWeakSlotScope WeakSlots(Vm, HotReloadState.get(), ActiveVm);
		if (ActiveVm)
		{
			auto Limits = ActiveVm->ExecutionLimits;
			if (Limits.MaxInstructions)
			{
				Limits.MaxInstructions = ActiveVm->ExecutionGovernance.ExecutedInstructions < Limits.MaxInstructions
				    ? Limits.MaxInstructions - ActiveVm->ExecutionGovernance.ExecutedInstructions
				    : 1;
			}
			Vm.SetExecutionLimits(Limits);
			Vm.SetExecutionDeadline(std::min(ActiveVm->ExternalExecutionDeadline, ActiveVm->ExecutionGovernance.Deadline));
			Vm.SetExecutionCheck(ActiveVm->ExternalExecutionCheck);
		}
		FHotReloadState Current;
		if (ActiveGlobals)
		{
			Current.Globals.resize(ActiveGlobals->size());
			for (std::size_t I = 0; I < ActiveGlobals->size(); ++I)
			{
				Current.Globals[I].swap((*ActiveGlobals)[I]);
			}
			if (ActiveVm)
			{
				Current.LocalStatics.swap(ActiveVm->LocalStatics);
			}
		}
		struct FRestoreStorage
		{
			std::vector<FValue>* Active;
			std::vector<FValue>& Values;
			std::shared_ptr<FLocalStaticState>* ActiveStatics;
			std::shared_ptr<FLocalStaticState>& Statics;
			~FRestoreStorage()
			{
				if (Active)
				{
					for (std::size_t I = 0; I < Active->size(); ++I)
					{
						(*Active)[I] = std::move(Values[I]);
					}
				}
				if (ActiveStatics)
				{
					ActiveStatics->swap(Statics);
				}
			}
		} RestoreStorage{ActiveGlobals, Current.Globals, ActiveGlobals && ActiveVm ? &ActiveVm->LocalStatics : nullptr,
		    Current.LocalStatics};
		const bool Ok = Vm.DiscardExternalValues(*Module, Values, Live, &EffectiveBindings().GetNativeFunctions(),
		    ActiveGlobals ? &Current : HotReloadState.get());
		if (ActiveVm && Vm.ExecutionCleanup)
		{
			ActiveVm->ExecutionCleanup = Vm.ExecutionCleanup;
			if (!Ok)
			{
				ActiveVm->bHasError = true;
				ActiveVm->Error = Vm.GetError();
			}
		}
		if (!Ok && Error && Error->empty())
		{
			*Error = Vm.GetError().Message;
		}
		if (ActiveVm && !ActiveVm->ExecutionGovernance.Charge(Vm.GetExecutedInstructionCount(), ActiveVm->ExecutionLimits.MaxInstructions))
		{
			if (!ActiveVm->bHasError)
			{
				ActiveVm->ReportError("execution instruction limit exceeded");
			}
			return false;
		}
		return Ok;
	}

	void VeroScriptSession::AppendRollbackKeepValues(std::vector<FValue>& Keep) const
	{
		if (!PendingHotReloadRollback) return;
		const auto& Rollback = *PendingHotReloadRollback;
		// The live graph is mutated in place during replacement initialization.
		// Old values removed from it still belong to the undo journal and must
		// survive any forced graph cleanup until rollback has restored them.
		const auto Append = [&Keep](const auto& Values)
		{
			Keep.insert(Keep.end(), Values.begin(), Values.end());
		};
		Append(Rollback.PreviousState.Globals);
		Append(Rollback.PreviousState.ExternalRoots);
		for (const auto& Undo : Rollback.Objects)
		{
			Append(Undo.Fields);
		}
		for (const auto& Undo : Rollback.Functions)
		{
			Append(Undo.Captures);
		}
		for (const auto& Undo : Rollback.DynamicArrays)
		{
			Append(Undo.Value.Elements);
		}
		for (const auto& Undo : Rollback.Queues)
		{
			Append(Undo.Value.Elements);
		}
		for (const auto& Undo : Rollback.Stacks)
		{
			Append(Undo.Value.Elements);
		}
		for (const auto& Undo : Rollback.Deques)
		{
			Append(Undo.Value.Elements);
		}
		const auto AppendEntries = [&Keep](const auto& Entries)
		{
			for (const auto& Entry : Entries)
			{
				Keep.push_back(Entry.second);
			}
		};
		const auto AppendMap = [&AppendEntries](const auto& Map)
		{
			AppendEntries(Map.IntEntries);
			AppendEntries(Map.UIntEntries);
			AppendEntries(Map.Int64Entries);
			AppendEntries(Map.UInt64Entries);
			AppendEntries(Map.StrEntries);
			AppendEntries(Map.WStrEntries);
			AppendEntries(Map.EnumEntries);
			AppendEntries(Map.HostEntries);
		};
		for (const auto& Undo : Rollback.Maps)
		{
			AppendMap(Undo.Value);
		}
		for (const auto& Undo : Rollback.OrderedMaps)
		{
			AppendMap(Undo.Value);
		}
		// Sets and packed scalar storage cannot contain script graph handles.
	}

	bool VeroScriptSession::PrepareValueRootReplacement(std::string& Error, const bool AllowMigration)
	{
		if (!DrainValueRoots(Error) || ValueRoots->HasExternalRoots(!AllowMigration)) return false;
		return ValueRoots->InvalidateGeneration(Error) && DrainValueRoots(Error);
	}

	bool VeroScriptSession::SettleMigratableRoots(const bool Applied, std::string& Error)
	{
		auto* Values = Applied && HotReloadState ? &HotReloadState->ExternalRoots : nullptr;
		if (!ValueRoots->CompleteMigration(PendingMigratableRoots, Values, Error))
		{
			bExecutionResetRequired = true;
			return false;
		}
		PendingMigratableRoots.clear();
		if (HotReloadState)
		{
			if (Applied && !HotReloadState->ExternalRoots.empty())
			{
				const auto Live = ValueRoots->SnapshotLiveValues();
				if (!CleanValueRoots(HotReloadState->ExternalRoots, Live, &Error))
				{
					bExecutionResetRequired = true;
					return false;
				}
			}
			HotReloadState->ExternalRoots.clear();
		}
		return true;
	}
}
