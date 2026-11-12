// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptVariant.h"
#include "VeroScriptHotReloadInternal.h"
#include <algorithm>
#include <new>

namespace VeroScript
{
	bool VeroScriptVm::RetainForGeneration(const FValue& Value, FValueRoot& Output, std::string& Failure)
	{
		if (!ActiveModule || !NativeValueScope || !bNativeRetentionAllowed || bDrainingNativeRoots ||
			(!bNativeRootExecutionActive && !NativeRootCleanup))
		{
			Failure = "value retention requires an active execution outside language cleanup";
			return false;
		}
		std::uint64_t Visited = 0;
		if (!VisitValueGraph(Value,
				[&](const FValue& V, const void*)
				{
					if ((++Visited & 1023u) == 0 && !ChargeExecutionWork(1024))
					{
						return false;
					}
					if (const auto* Object = std::get_if<FObjectHandle>(&V); Object && *Object)
					{
						return (*Object)->ProgramOwner == ProgramOwner && !(*Object)->bDestroyed &&
							   !(*Object)->bDestroying;
					}
					if (const auto* Function = std::get_if<FFunctionHandle>(&V); Function && *Function)
					{
						return (*Function)->ProgramOwner == ProgramOwner &&
							   (*Function)->ProgramGeneration == ProgramGeneration &&
							   (*Function)->FunctionIndex < ActiveModule->Functions.size() &&
							   (*Function)->FunctionName == ActiveModule->Functions[(*Function)->FunctionIndex].Name &&
							   (*Function)->TypeName ==
								   ActiveModule->Functions[(*Function)->FunctionIndex].ClosureTypeName;
					}
					if (Variant::IsVariant(V))
					{
						return Variant::ValidateSource(V, ProgramOwner, ProgramGeneration, Failure);
					}
					return true;
				}))
		{
			if (Failure.empty())
			{
				Failure = "cannot retain a foreign, destroyed or excessively nested value";
			}
			return false;
		}
		if (!NativeValueRoots)
		{
			NativeValueRoots = std::make_shared<FValueRootOwner>();
		}
		return NativeValueRoots->RetainForGeneration(Value, Output, Failure);
	}

	bool VeroScriptVm::DrainNativeValueRoots(std::string& Failure)
	{
		if (!NativeValueRoots || !NativeValueRoots->IsOwnerThread() || bDrainingNativeRoots || !ActiveModule)
		{
			return true;
		}
		bDrainingNativeRoots = true;
		struct FRestore
		{
			bool& Flag;
			~FRestore()
			{
				Flag = false;
			}
		} Restore{bDrainingNativeRoots};
		try
		{
			return NativeValueRoots->DrainGeneration(
				[&](auto& Retired, const auto& Keep)
				{
					std::vector<FValue> Live = Keep;
					Live.insert(Live.end(), OperandStack.begin(), OperandStack.end());
					for (const auto& Frame : Frames)
					{
						Live.insert(Live.end(), Frame.Locals.begin(), Frame.Locals.end());
						if (Frame.ThisObject)
						{
							Live.emplace_back(Frame.ThisObject);
						}
						Live.push_back(Frame.ReturnValue);
						for (const auto& Transfer : Frame.FinallyContinuations)
						{
							Live.push_back(Transfer.Value);
						}
					}
					for (auto* Scope = NativeValueScope; Scope; Scope = Scope->Previous)
					{
						if (Scope->Arguments)
						{
							Live.insert(Live.end(), Scope->Arguments, Scope->Arguments + Scope->ArgumentCount);
						}
						Live.push_back(Scope->Return);
					}
					if (bHasPendingScriptException)
					{
						Live.push_back(PendingScriptException);
					}
					if (NativeRootCleanup)
					{
						return NativeRootCleanup(Retired, Live, Globals, Failure);
					}
					// An isolated VM prevents destructor failure from clearing the active stack.
					VeroScriptVm Cleanup(OutputStream, InputStream, ErrorStream);
					Cleanup.VmExecutionDomain = VmExecutionDomain;
					Cleanup.ExecutionCleanup = ExecutionCleanup;
					Cleanup.bBorrowedExecutionCleanup = ExecutionCleanup != nullptr;
					Cleanup.ExternalExecutionCleanup = &ExecutionCleanup;
					Cleanup.ExternalExecutionResetRequired = ExternalExecutionResetRequired
						? ExternalExecutionResetRequired : &bExecutionResetRequired;
					Cleanup.bNativeRetentionAllowed = false;
					Cleanup.SetProgramOwner(ProgramOwner);
					Cleanup.SetProgramGeneration(ProgramGeneration);
					Cleanup.SetCapabilities(Capabilities);
					Cleanup.SetMemoryContext(MemoryContext);
					Cleanup.SetMemoryHostBindings(MemoryBindings);
					Cleanup.SetStandardLibraryLimits(StandardLibraryLimits);
					Cleanup.SetNumericLimits(NumericLimits);
					Cleanup.SetI18nServices(I18nServices, I18nLimits);
					auto CleanupLimits = ExecutionLimits;
					if (CleanupLimits.MaxInstructions)
					{
						CleanupLimits.MaxInstructions =
							ExecutionGovernance.ExecutedInstructions < CleanupLimits.MaxInstructions
								? CleanupLimits.MaxInstructions - ExecutionGovernance.ExecutedInstructions
								: 1;
					}
					Cleanup.SetExecutionLimits(CleanupLimits);
					Cleanup.SetExecutionDeadline(std::min(ExternalExecutionDeadline, ExecutionGovernance.Deadline));
					Cleanup.SetExecutionCheck(ExternalExecutionCheck);
					Cleanup.SetExecutionControl(ExecutionControl);
					Cleanup.SetInstanceCallInterceptor(InstanceCallInterceptor);
					Cleanup.SetMutationCallback(MutationCallback);
					Cleanup.SetExecutionBackend(EExecutionBackend::Vm);
					Cleanup.Executor = Executor;
					FHotReloadState State;
					State.Globals.resize(Globals.size());
					for (std::size_t I = 0; I < Globals.size(); ++I)
					{
						State.Globals[I].swap(Globals[I]);
					}
					State.LocalStatics.swap(LocalStatics);
					Cleanup.WeakTargets.swap(WeakTargets);
					struct FRestoreStorage
					{
						std::vector<FValue>& Active;
						std::vector<FValue>& Values;
						std::shared_ptr<FLocalStaticState>& ActiveStatics;
						std::shared_ptr<FLocalStaticState>& Statics;
						std::vector<FWeakTarget>& ActiveWeak;
						std::vector<FWeakTarget>& BorrowedWeak;
						~FRestoreStorage()
						{
							for (std::size_t I = 0; I < Active.size(); ++I)
							{
								Active[I] = std::move(Values[I]);
							}
							ActiveStatics.swap(Statics);
							ActiveWeak.swap(BorrowedWeak);
						}
					} RestoreStorage{Globals, State.Globals, LocalStatics, State.LocalStatics, WeakTargets, Cleanup.WeakTargets};
					const bool Ok = Cleanup.DiscardExternalValues(*ActiveModule, Retired, Live, ActiveNatives, &State);
					if (!Ok && Failure.empty())
					{
						Failure = Cleanup.GetError().Message;
					}
					const bool Charged = ExecutionGovernance.Charge(
						Cleanup.GetExecutedInstructionCount(), ExecutionLimits.MaxInstructions);
					if (!Charged && !bHasError)
					{
						ReportErrorId("diag.runtime_execution_instruction_limit_exceeded");
					}
					return Ok && Charged;
				},
				Failure);
		}
		catch (const std::bad_alloc&)
		{
			// The registry restores selected roots before unwinding. Return through
			// normal VM teardown so its generation is invalidated while metadata lives.
			Failure = "out of memory";
			return false;
		}
	}

	bool VeroScriptVm::InvalidateNativeValueRoots()
	{
		// A host-serialized foreign-thread callable may use this VM, but cleanup
		// of a borrowed session registry remains an owner-thread safe point.
		// Generation retention itself still rejects foreign-thread access.
		if (!NativeValueRoots || !NativeValueRoots->IsOwnerThread())
		{
			return true;
		}
		std::string Failure;
		const bool Ok = NativeValueRoots->InvalidateGeneration(Failure) && DrainNativeValueRoots(Failure);
		if (!Ok && !bHasError)
		{
			ReportError(Failure.empty() ? "retained native value cleanup failed" : std::move(Failure));
		}
		return Ok;
	}
} // namespace VeroScript
