// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"
#include "VeroScriptVmCleanup.h"
#include "VeroScriptVariantInternal.h"
#include "VeroScriptValueGraphInternal.h"
#include "VeroScriptModuleEnvironment.h"
#include "VeroScriptStandardLibrary.h"
#include "VeroScriptJitScalarServices.h"
#include "VeroScriptJitCallServices.h"
#include "VeroScriptJitValueServices.h"
#include "VeroScriptJitFrameServices.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptJit.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptModuleMetaInternal.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptConsoleNative.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace VeroScript
{
	namespace
	{
		template <class T>
		class TWeakSlotLease final
		{
		public:
			TWeakSlotLease(std::vector<T>& InActive, FHotReloadState* State) : Active(InActive)
			{
				if (State)
				{
					if (!State->WeakReferences)
					{
						State->WeakReferences = std::make_shared<std::vector<T>>();
					}
					Persistent = std::static_pointer_cast<std::vector<T>>(State->WeakReferences);
					Active.swap(*Persistent);
				}
			}
			~TWeakSlotLease()
			{
				if (Persistent)
				{
					Active.swap(*Persistent);
				}
			}
			TWeakSlotLease(const TWeakSlotLease&) = delete;
			TWeakSlotLease& operator=(const TWeakSlotLease&) = delete;

		private:
			std::vector<T>& Active;
			std::shared_ptr<std::vector<T>> Persistent;
		};
	}

	bool VeroScriptVm::SetModuleEnvironment(std::shared_ptr<const FModuleEnvironment> Environment, std::string& Failure, bool bUseBindings)
	{
		if (ActiveModule || ActiveJitCallDepth != 0 || !Frames.empty())
		{
			Failure = "module environment changes require an idle VM";
			return false;
		}
		const bool HadEnvironment = static_cast<bool>(ModuleEnvironment);
		const bool Changed = ModuleEnvironment != Environment || bUseEnvironmentBindings != bUseBindings;
		ModuleEnvironment = std::move(Environment);
		if (Changed)
		{
			BoundNativeModule = nullptr;
			BoundNativeOwner.reset();
			BoundNativeStorage.clear();
			NativeBindingSourceSnapshot.clear();
			BoundNativeReferenceSnapshot.clear();
			NativeBindingCacheBytes = 0;
			ActiveNatives = nullptr;
			ActiveNativeIndexSource = nullptr;
			BackendCache.reset();
		}
		bUseEnvironmentBindings = bUseBindings;
		MemoryBindings = bUseBindings && ModuleEnvironment ? ModuleEnvironment->GetDescription().Bindings.get() : nullptr;
		if (ModuleEnvironment || HadEnvironment)
		{
			SetI18nServices(ModuleEnvironment ? ModuleEnvironment->GetDescription().I18nServices : nullptr,
			    ModuleEnvironment ? ModuleEnvironment->GetDescription().I18nLimits : I18n::FI18nLimits{});
		}

		Failure.clear();
		return true;
	}

	bool VeroScriptVm::Execute(
	    const FBytecodeModule& Module,
	    const ERunMode RunMode,
	    const std::vector<std::string_view>& ScriptArgs,
	    const std::vector<FNativeFunction>* const Natives)
	{
		return ExecuteInternal(
		    Module,
		    RunMode,
		    ScriptArgs,
		    Natives,
		    nullptr,
		    nullptr,
		    nullptr);
	}

	bool VeroScriptVm::ExecutePersistent(
	    const FBytecodeModule& Module,
	    const ERunMode RunMode,
	    FHotReloadState& State,
	    FHotReloadRollback* const InitializationRollback,
	    const std::vector<std::string_view>& ScriptArgs,
	    const std::vector<FNativeFunction>* const Natives)
	{
		// Retained initialization ranges remain interpreter-owned. Ordinary
		// functions use the generation-bound backend and Core value services.
		const bool bOk = ExecuteInternal(
		    Module,
		    RunMode,
		    ScriptArgs,
		    Natives,
		    &State,
		    InitializationRollback,
		    nullptr);
		return bOk;
	}

	bool VeroScriptVm::DiscardPersistentState(
	    const FBytecodeModule& Module,
	    FHotReloadState& State,
	    const std::vector<FNativeFunction>* const Natives)
	{
		if (State.bExecutionResetRequired)
		{
			MarkExecutionResetRequired();
		}
		TWeakSlotLease WeakSlotLease(WeakTargets, &State);
		BeginExecutionGovernance();
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		bHasError = false;
		bHasPendingScriptException = false;
		PendingScriptException = std::int32_t{0};
		PendingScriptExceptionLocation = {};
		Error = {};

		Globals = std::move(State.Globals);
		LocalStatics = std::move(State.LocalStatics);
		std::vector<std::function<void(std::vector<FValue>&)>> RetainSurvivingCycles;
		std::unordered_set<const void*> SeenStatics;
		if (LocalStatics)
		{
			for (auto It = LocalStatics->Completed.rbegin(); It != LocalStatics->Completed.rend(); ++It)
			{
				if (*It < Globals.size())
				{
					(void)ValueGraphInternal::Visit(Globals[*It], [&](const FValue& Value, const void*)
					{
						std::visit([&](const auto& Handle)
						{
							using T = std::decay_t<decltype(Handle)>;
							if constexpr (std::is_same_v<T, FObjectHandle> || std::is_same_v<T, FFunctionHandle> || std::is_same_v<T, FScriptVariantHandle> || std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FMapHandle> || std::is_same_v<T, FOrderedMapHandle> || std::is_same_v<T, FQueueHandle> || std::is_same_v<T, FStackHandle> || std::is_same_v<T, FDequeHandle>)
							{
								if (Handle)
								{
									const std::weak_ptr<typename T::element_type> Weak = Handle;
									RetainSurvivingCycles.emplace_back([Weak](std::vector<FValue>& Roots)
									{
										if (auto Live = Weak.lock())
										{
											Roots.emplace_back(std::move(Live));
										}
									});
								}
							}
						}, Value);
						return true;
					}, SeenStatics);
				}
			}
		}
		// Weak snapshots do not suppress last-owner destruction in completion order.
		const bool bStaticsReleased = ReleaseLocalStaticRoots({});
		std::vector<FValue> Roots = Globals;
		for (const auto& Retain : RetainSurvivingCycles)
		{
			Retain(Roots);
		}
		const bool bReleasedAll = DiscardValueRoots(Roots, nullptr);
		const bool bExceptionReleased = ReleasePendingScriptException();
		Globals.clear();
		LocalStatics.reset();
		State.Clear();
		const bool bCleanupOk = !bHasError && ((bStaticsReleased && bReleasedAll && bExceptionReleased) || bExitRequested);
		FinishBackendExecution();
		ActiveModule = nullptr;
		ActiveNatives = nullptr;
		return bCleanupOk;
	}

	bool VeroScriptVm::DiscardRetainedCallables(
	    const FBytecodeModule& Module,
	    std::vector<FFunctionHandle>& Callables,
	    const std::vector<FNativeFunction>* const Natives)
	{
		BeginExecutionGovernance();
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		bHasError = false;
		Error = {};

		std::vector<FValue> Roots;
		Roots.reserve(Callables.size());
		for (FFunctionHandle& Callable : Callables)
		{
			if (Callable)
			{
				Roots.emplace_back(std::move(Callable));
			}
		}
		Callables.clear();
		const bool bReleasedAll = DiscardValueRoots(Roots, nullptr);
		const bool bExceptionReleased = ReleasePendingScriptException();
		const bool bCleanupOk = !bHasError && ((bReleasedAll && bExceptionReleased) || bExitRequested);
		FinishBackendExecution();
		ActiveModule = nullptr;
		ActiveNatives = nullptr;
		return bCleanupOk;
	}

	bool VeroScriptVm::RollbackHotReloadState(
	    const FBytecodeModule& Module,
	    FHotReloadState& State,
	    FHotReloadRollback& Rollback,
	    const std::vector<FNativeFunction>* const Natives)
	{
		if (State.bExecutionResetRequired || Rollback.PreviousState.bExecutionResetRequired)
		{
			MarkExecutionResetRequired();
		}
		TWeakSlotLease WeakSlotLease(WeakTargets, &State);
		BeginExecutionGovernance();
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		bHasError = false;
		Error = {};
		struct FFinishRollbackExecution
		{
			VeroScriptVm& Vm;
			FHotReloadState& State;
			~FFinishRollbackExecution()
			{
				State.bExecutionResetRequired = State.bExecutionResetRequired || Vm.RequiresReset();
				Vm.FinishBackendExecution();
				Vm.ActiveModule = nullptr;
				Vm.ActiveNatives = nullptr;
			}
		} FinishExecution{*this, State};

		std::vector<FValue> Discarded;
		std::vector<std::weak_ptr<FScriptObject>> RestoredObjects;
		RestoredObjects.reserve(Rollback.Objects.size());
		struct FRestoreRollbackFunction
		{
			FFunctionValue* Function = nullptr;
			std::weak_ptr<FFunctionValue> Owner;
			std::uint16_t PreviousIndex = 0;
			std::uint64_t PreviousGeneration = 0;
			std::vector<FValue> PreviousCaptures;
			std::vector<std::size_t> PreviousSlots;
			bool bCleanupStarted = false;
			bool bProjected = false;
			~FRestoreRollbackFunction()
			{
				const auto Live = Owner.lock();
				auto* Target = Live ? Live.get() : bCleanupStarted ? nullptr
				                                                   : Function;
				if (!Target) return;
				for (std::size_t Slot = 0; bProjected && Slot < PreviousSlots.size() && Slot < Target->Captures.size(); ++Slot)
				{
					PreviousCaptures[PreviousSlots[Slot]] = std::move(Target->Captures[Slot]);
				}
				Target->Captures = std::move(PreviousCaptures);
				Target->FunctionIndex = PreviousIndex;
				Target->ProgramGeneration = PreviousGeneration;
			}
		};
		// Cleanup callbacks may publish more identities; stable storage keeps their
		// restoration guards from running while this journal grows.
		std::deque<FRestoreRollbackFunction> FunctionLayouts;

		for (auto& Undo : Rollback.DynamicArrays)
		{
			if (Undo.Target != nullptr)
			{
				auto DiscardedHandle =
				    std::make_shared<FDynamicArray>(std::move(*Undo.Target));
				*Undo.Target = std::move(Undo.Value);
				Discarded.push_back(std::move(DiscardedHandle));
			}
		}
		for (auto& Undo : Rollback.Maps)
		{
			if (Undo.Target != nullptr)
			{
				auto DiscardedHandle = std::make_shared<FMap>(std::move(*Undo.Target));
				*Undo.Target = std::move(Undo.Value);
				Discarded.push_back(std::move(DiscardedHandle));
			}
		}
		for (auto& Undo : Rollback.Sets)
		{
			if (Undo.Target != nullptr)
			{
				*Undo.Target = std::move(Undo.Value);
			}
		}
		for (auto& Undo : Rollback.Queues)
		{
			if (Undo.Target != nullptr)
			{
				auto DiscardedHandle =
				    std::make_shared<FQueue>(std::move(*Undo.Target));
				*Undo.Target = std::move(Undo.Value);
				Discarded.push_back(std::move(DiscardedHandle));
			}
		}
		for (auto& Undo : Rollback.Stacks)
		{
			if (Undo.Target != nullptr)
			{
				auto DiscardedHandle =
				    std::make_shared<FStack>(std::move(*Undo.Target));
				*Undo.Target = std::move(Undo.Value);
				Discarded.push_back(std::move(DiscardedHandle));
			}
		}
		for (auto& Undo : Rollback.OrderedMaps)
		{
			if (Undo.Target != nullptr)
			{
				auto DiscardedHandle =
				    std::make_shared<FOrderedMap>(std::move(*Undo.Target));
				*Undo.Target = std::move(Undo.Value);
				Discarded.push_back(std::move(DiscardedHandle));
			}
		}
		for (auto& Undo : Rollback.Deques)
		{
			if (Undo.Target != nullptr)
			{
				auto DiscardedHandle =
				    std::make_shared<FDeque>(std::move(*Undo.Target));
				*Undo.Target = std::move(Undo.Value);
				Discarded.push_back(std::move(DiscardedHandle));
			}
		}
		for (auto It = Rollback.Objects.rbegin();
		    It != Rollback.Objects.rend();
		    ++It)
		{
			if (It->Object != nullptr)
			{
				for (FValue& Field : It->Object->Fields)
				{
					Discarded.push_back(std::move(Field));
				}
				It->Object->Fields = std::move(It->Fields);
				It->Object->bDestroying = It->bDestroying;
				It->Object->bDestroyed = It->bDestroyed;
				RestoredObjects.push_back(It->Object->weak_from_this());
			}
		}
		for (auto It = Rollback.Functions.rbegin();
		    It != Rollback.Functions.rend();
		    ++It)
		{
			if (It->Function != nullptr)
			{
				std::vector<std::size_t> PreviousSlots;
				std::string Failure;
				if (It->FunctionIndex >= Rollback.PreviousState.Functions.size() || It->Function->FunctionIndex >= Module.Functions.size() || !BuildHotReloadCaptureSlots(Rollback.PreviousState.Functions[It->FunctionIndex], Module.Functions[It->Function->FunctionIndex], PreviousSlots, It->Function->FunctionName, Failure))
				{
					ReportError(Failure.empty() ? "hot reload cleanup callable schema is invalid" : Failure);
					return false;
				}
				FunctionLayouts.emplace_back();
				auto& Layout = FunctionLayouts.back();
				Layout.Function = It->Function;
				Layout.PreviousIndex = It->FunctionIndex;
				Layout.PreviousGeneration = It->ProgramGeneration;
				Layout.PreviousSlots = std::move(PreviousSlots);
				Layout.PreviousCaptures = std::move(It->Captures);
				std::vector<FValue> Captures(Layout.PreviousSlots.size(), std::int32_t{0});
				for (std::size_t Slot = 0; Slot < Captures.size(); ++Slot)
				{
					Captures[Slot] = std::move(Layout.PreviousCaptures[Layout.PreviousSlots[Slot]]);
				}
				for (auto& Capture : It->Function->Captures)
				{
					Discarded.push_back(std::move(Capture));
				}
				// Keep the migrated index/generation until replacement destructors finish.
				It->Function->Captures = std::move(Captures);
				Layout.bProjected = true;
			}
		}

		// Retained globals stay alive in PreviousState / live object identity.
		// Only exclusive failed-init allocations (extracted above) are discarded.
		State = std::move(Rollback.PreviousState);
		Rollback.Clear();

		// Keep-set must be collected after layouts are restored. Walking the
		// pre-restore live graph would mark failed-init allocations as retained.
		// Protect every shared identity (objects, containers, closures) — discarded
		// replacement fields often alias the same handles that were just restored.
		std::unordered_set<const void*> KeepHandles;
		std::function<void(const FValue&)> PrepareSurvivor;
		std::function<bool(const FScriptObject*, std::size_t)> IsSurvivorField;
		const auto CollectKeep =
		    [&KeepHandles, &FunctionLayouts, &PrepareSurvivor, &IsSurvivorField](auto& Self, const FValue& Value) -> void
		{
			if (PrepareSurvivor)
			{
				PrepareSurvivor(Value);
			}
			if (const auto* Box = std::get_if<FScriptVariantHandle>(&Value))
			{
				if (*Box && KeepHandles.insert(Box->get()).second)
				{
					Self(Self, (*Box)->Value);
				}
				return;
			}
			if (const auto* ObjectHandle = std::get_if<FObjectHandle>(&Value))
			{
				if (!*ObjectHandle || !KeepHandles.insert(ObjectHandle->get()).second)
				{
					return;
				}
				for (std::size_t Slot = 0; Slot < (*ObjectHandle)->Fields.size(); ++Slot)
				{
					if (!IsSurvivorField || IsSurvivorField(ObjectHandle->get(), Slot))
					{
						Self(Self, (*ObjectHandle)->Fields[Slot]);
					}
				}
				return;
			}
			if (const auto* NativeArray = std::get_if<FNativeArray>(&Value))
			{
				for (const FValue& Element : NativeArray->Storage().Elements)
				{
					Self(Self, Element);
				}
				return;
			}
			if (const auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&Value))
			{
				if (!*DynamicArray || !KeepHandles.insert(DynamicArray->get()).second)
				{
					return;
				}
				for (const FValue& Element : (*DynamicArray)->Elements)
				{
					Self(Self, Element);
				}
				return;
			}
			if (const auto* MapHandle = std::get_if<FMapHandle>(&Value))
			{
				if (!*MapHandle || !KeepHandles.insert(MapHandle->get()).second)
				{
					return;
				}
				for (const auto& Entry : (*MapHandle)->IntEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->UIntEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->Int64Entries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->UInt64Entries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->StrEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->WStrEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->EnumEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*MapHandle)->HostEntries)
				{
					Self(Self, Entry.second);
				}
				return;
			}
			if (const auto* OrderedMap = std::get_if<FOrderedMapHandle>(&Value))
			{
				if (!*OrderedMap || !KeepHandles.insert(OrderedMap->get()).second)
				{
					return;
				}
				for (const auto& Entry : (*OrderedMap)->IntEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->UIntEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->Int64Entries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->UInt64Entries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->StrEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->WStrEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->EnumEntries)
				{
					Self(Self, Entry.second);
				}
				for (const auto& Entry : (*OrderedMap)->HostEntries)
				{
					Self(Self, Entry.second);
				}
				return;
			}
			if (const auto* QueueHandle = std::get_if<FQueueHandle>(&Value))
			{
				if (!*QueueHandle || !KeepHandles.insert(QueueHandle->get()).second)
				{
					return;
				}
				for (const FValue& Element : (*QueueHandle)->Elements)
				{
					Self(Self, Element);
				}
				return;
			}
			if (const auto* StackHandle = std::get_if<FStackHandle>(&Value))
			{
				if (!*StackHandle || !KeepHandles.insert(StackHandle->get()).second)
				{
					return;
				}
				for (const FValue& Element : (*StackHandle)->Elements)
				{
					Self(Self, Element);
				}
				return;
			}
			if (const auto* DequeHandle = std::get_if<FDequeHandle>(&Value))
			{
				if (!*DequeHandle || !KeepHandles.insert(DequeHandle->get()).second)
				{
					return;
				}
				for (const FValue& Element : (*DequeHandle)->Elements)
				{
					Self(Self, Element);
				}
				return;
			}
			if (const auto* FunctionHandle = std::get_if<FFunctionHandle>(&Value))
			{
				if (!*FunctionHandle || !KeepHandles.insert(FunctionHandle->get()).second)
				{
					return;
				}
				for (auto& Layout : FunctionLayouts)
				{
					if (!Layout.bCleanupStarted && Layout.Function == FunctionHandle->get())
					{
						Layout.Owner = *FunctionHandle;
					}
				}
				for (const FValue& Capture : (*FunctionHandle)->Captures)
				{
					Self(Self, Capture);
				}
				if ((*FunctionHandle)->CapturedThis)
				{
					const FValue CapturedThisValue = (*FunctionHandle)->CapturedThis;
					Self(Self, CapturedThisValue);
				}
			}
		};
		for (const FValue& Global : State.Globals)
		{
			CollectKeep(CollectKeep, Global);
		}
		for (const FValue& Root : State.ExternalRoots)
		{
			CollectKeep(CollectKeep, Root);
		}
		struct FRestoreRollbackObject
		{
			std::weak_ptr<FScriptObject> Object;
			std::vector<FValue> PreviousFields;
			std::vector<FValue> CleanupFields;
			std::vector<std::size_t> PreviousSlots;
			bool bProjected = false;
			~FRestoreRollbackObject()
			{
				if (!bProjected) return;
				if (const auto Live = Object.lock())
				{
					for (std::size_t Slot = 0; Slot < PreviousSlots.size() && Slot < Live->Fields.size(); ++Slot)
					{
						if (PreviousSlots[Slot] < PreviousFields.size())
						{
							PreviousFields[PreviousSlots[Slot]] = std::move(Live->Fields[Slot]);
						}
					}
					Live->Fields = std::move(PreviousFields);
				}
			}
		};
		std::deque<FRestoreRollbackObject> ObjectLayouts;
		std::size_t TemporaryFieldCount = 0;
		for (const auto& Object : RestoredObjects)
		{
			const auto Live = Object.lock();
			if (!Live)
			{
				continue;
			}
			ObjectLayouts.emplace_back();
			auto& Layout = ObjectLayouts.back();
			Layout.Object = Object;
			std::string Failure;
			if (!BuildHotReloadCleanupFields(State, Module, *Live, Layout.PreviousSlots,
			        Layout.CleanupFields, Failure, MemoryBindings, ProgramOwner))
			{
				ReportError(Failure);
				return false;
			}
			TemporaryFieldCount += Layout.CleanupFields.size();
		}

		// Discarded values use replacement metadata, while restored storage still
		// has its previous slots. Project it by name for destructor callbacks.
		const auto MissingSlot = std::numeric_limits<std::size_t>::max();
		std::vector<std::size_t> RestoredSlots(Module.GlobalNames.size(), MissingSlot);
		Globals.assign(Module.GlobalNames.size(), std::int32_t{0});
		if (!State.LocalStatics)
		{
			State.LocalStatics = std::make_shared<FLocalStaticState>();
		}
		State.LocalStatics->States.resize(State.Globals.size(), ELocalStaticInitialization::Uninitialized);
		State.LocalStatics->Completed.reserve(State.Globals.size());
		LocalStatics = std::make_shared<FLocalStaticState>();
		LocalStatics->States.resize(Globals.size(), ELocalStaticInitialization::Uninitialized);
		LocalStatics->Completed.reserve(Globals.size());
		for (std::size_t Index = 0; Index < State.Globals.size(); ++Index)
		{
			const auto Slot = Module.GlobalSlots.find(State.GlobalNames[Index]);
			if (Slot == Module.GlobalSlots.end() || Slot->second >= Globals.size())
			{
				continue;
			}
			RestoredSlots[Slot->second] = Index;
			if (Index < State.LocalStatics->States.size())
			{
				LocalStatics->States[Slot->second] = State.LocalStatics->States[Index];
			}
		}
		for (const auto Slot : State.LocalStatics->Completed)
		{
			if (Slot >= State.GlobalNames.size())
			{
				continue;
			}
			const auto Target = Module.GlobalSlots.find(State.GlobalNames[Slot]);
			if (Target != Module.GlobalSlots.end())
			{
				LocalStatics->Completed.push_back(Target->second);
			}
		}
		struct FRestoreRollbackStorage
		{
			FHotReloadState& State;
			std::vector<FValue>& Globals;
			std::shared_ptr<FLocalStaticState>& Statics;
			const std::vector<std::size_t>& Slots;
			~FRestoreRollbackStorage()
			{
				for (std::size_t Slot = 0; Slot < Slots.size(); ++Slot)
				{
					if (Slots[Slot] >= State.Globals.size())
					{
						continue;
					}
					State.Globals[Slots[Slot]] = std::move(Globals[Slot]);
					State.LocalStatics->States[Slots[Slot]] = Statics->States[Slot];
				}
				State.LocalStatics->Completed.clear();
				for (const auto Slot : Statics->Completed)
				{
					if (Slot < Slots.size() && Slots[Slot] < State.Globals.size())
					{
						State.LocalStatics->Completed.push_back(static_cast<std::uint16_t>(Slots[Slot]));
					}
				}
				Globals.clear();
				Statics.reset();
			}
		} RestoreStorage{State, Globals, LocalStatics, RestoredSlots};
		for (std::size_t Slot = 0; Slot < RestoredSlots.size(); ++Slot)
		{
			if (RestoredSlots[Slot] != MissingSlot)
			{
				Globals[Slot] = std::move(State.Globals[RestoredSlots[Slot]]);
			}
		}
		Discarded.reserve(std::max(Discarded.size(), Globals.size() + TemporaryFieldCount));
		for (auto& Layout : ObjectLayouts)
		{
			if (const auto Live = Layout.Object.lock())
			{
				Layout.PreviousFields = std::move(Live->Fields);
				for (std::size_t Slot = 0; Slot < Layout.PreviousSlots.size(); ++Slot)
				{
					if (Layout.PreviousSlots[Slot] < Layout.PreviousFields.size())
					{
						Layout.CleanupFields[Slot] = std::move(Layout.PreviousFields[Layout.PreviousSlots[Slot]]);
					}
				}
				Live->Fields = std::move(Layout.CleanupFields);
				Layout.bProjected = true;
			}
		}
		bool bReleasedAll = true;
		FScriptError FirstReleaseError;
		const auto RecordReleaseFailure = [&]()
		{
			VmDetail::RecordCleanupFailure(bReleasedAll, FirstReleaseError, bHasError, Error);
		};
		const auto DetachTemporary = [&](FValue& Value)
		{
			const auto* Empty = std::get_if<std::int32_t>(&Value);
			if (!Empty || *Empty != 0)
			{
				Discarded.push_back(std::move(Value));
				Value = std::int32_t{0};
			}
		};
		std::unordered_map<const FScriptObject*, FRestoreRollbackObject*> ObjectLayoutByIdentity;
		for (auto& Layout : ObjectLayouts)
		{
			if (const auto Live = Layout.Object.lock())
			{
				ObjectLayoutByIdentity.emplace(Live.get(), &Layout);
			}
		}
		IsSurvivorField = [&](const FScriptObject* Object, const std::size_t Slot)
		{
			const auto Found = ObjectLayoutByIdentity.find(Object);
			return Found == ObjectLayoutByIdentity.end() || Slot >= Found->second->PreviousSlots.size() || Found->second->PreviousSlots[Slot] != MissingSlot;
		};
		std::unordered_map<const FFunctionValue*, FRestoreRollbackFunction*> FunctionLayoutByIdentity;
		for (auto& Layout : FunctionLayouts)
		{
			if (const auto Live = Layout.Owner.lock())
			{
				FunctionLayoutByIdentity.emplace(Live.get(), &Layout);
			}
		}
		struct FRestoreRollbackVariant
		{
			std::weak_ptr<FScriptVariantStorage> Owner;
			std::shared_ptr<const FVariantSourceStamp> Source;
			~FRestoreRollbackVariant()
			{
				if (const auto Live = Owner.lock())
				{
					Live->Source = std::move(Source);
				}
			}
		};
		std::deque<FRestoreRollbackVariant> VariantLayouts;
		std::unordered_map<const FScriptVariantStorage*, FRestoreRollbackVariant*> VariantLayoutByIdentity;
		bool bDetachSurvivorTemporary = false;
		std::string SurvivorFailure;
		PrepareSurvivor = [&](const FValue& Value)
		{
			// All callbacks still execute against replacement metadata. Enrol new
			// surviving identities now, then restore their old layouts on scope exit.
			if (const auto* Object = std::get_if<FObjectHandle>(&Value); Object && *Object)
			{
				auto Found = ObjectLayoutByIdentity.find(Object->get());
				if (Found != ObjectLayoutByIdentity.end() && Found->second->Object.expired())
				{
					ObjectLayoutByIdentity.erase(Found);
					Found = ObjectLayoutByIdentity.end();
				}
				if (Found == ObjectLayoutByIdentity.end())
				{
					std::vector<std::size_t> PreviousSlots;
					std::size_t PreviousFieldCount = 0;
					std::string Failure;
					if ((*Object)->ProgramOwner != ProgramOwner || !BuildHotReloadFieldSlots(State, Module, **Object, PreviousSlots, PreviousFieldCount, Failure) || (*Object)->Fields.size() != PreviousSlots.size())
					{
						if (SurvivorFailure.empty())
						{
							SurvivorFailure = Failure.empty()
							    ? "hot reload cleanup cannot restore a surviving object"
							    : std::move(Failure);
						}
						return;
					}
					ObjectLayouts.emplace_back();
					auto& Layout = ObjectLayouts.back();
					Layout.Object = *Object;
					Layout.PreviousFields.assign(PreviousFieldCount, std::int32_t{0});
					Layout.PreviousSlots = std::move(PreviousSlots);
					Layout.bProjected = true;
					Found = ObjectLayoutByIdentity.emplace(Object->get(), &Layout).first;
				}
				if (bDetachSurvivorTemporary)
				{
					for (std::size_t Slot = 0; Slot < Found->second->PreviousSlots.size(); ++Slot)
					{
						if (Found->second->PreviousSlots[Slot] == MissingSlot)
						{
							DetachTemporary((*Object)->Fields[Slot]);
						}
					}
				}
			}
			if (const auto* Function = std::get_if<FFunctionHandle>(&Value); Function && *Function)
			{
				auto Found = FunctionLayoutByIdentity.find(Function->get());
				if (Found != FunctionLayoutByIdentity.end() && Found->second->Owner.expired())
				{
					FunctionLayoutByIdentity.erase(Found);
					Found = FunctionLayoutByIdentity.end();
				}
				if (Found == FunctionLayoutByIdentity.end())
				{
					std::string Failure;
					const auto ReplacementIndex = (*Function)->FunctionIndex;
					const auto Previous = std::find_if(State.Functions.begin(), State.Functions.end(),
					    [&](const auto& Candidate)
					{
						return Candidate.Name == (*Function)->FunctionName && Candidate.ClosureTypeName == (*Function)->TypeName;
					});
					std::vector<std::size_t> PreviousSlots;
					if ((*Function)->ProgramOwner != ProgramOwner || (*Function)->ProgramGeneration != ProgramGeneration || ReplacementIndex >= Module.Functions.size() || Previous == State.Functions.end() || Module.Functions[ReplacementIndex].Name != (*Function)->FunctionName || Module.Functions[ReplacementIndex].ClosureTypeName != (*Function)->TypeName || Module.Functions[ReplacementIndex].ParamCount != Previous->ParamCount || (*Function)->Captures.size() != Module.Functions[ReplacementIndex].Captures.size() || !BuildHotReloadCaptureSlots(*Previous, Module.Functions[ReplacementIndex], PreviousSlots, (*Function)->FunctionName, Failure))
					{
						if (SurvivorFailure.empty())
						{
							SurvivorFailure = Failure.empty()
							    ? "hot reload cleanup cannot restore a surviving callable: " + (*Function)->FunctionName
							    : std::move(Failure);
						}
						return;
					}
					FunctionLayouts.emplace_back();
					auto& Layout = FunctionLayouts.back();
					Layout.Function = Function->get();
					Layout.Owner = *Function;
					Layout.PreviousIndex = static_cast<std::uint16_t>(Previous - State.Functions.begin());
					Layout.PreviousGeneration = State.ProgramGeneration;
					Layout.PreviousCaptures.assign(PreviousSlots.size(), std::int32_t{0});
					Layout.PreviousSlots = std::move(PreviousSlots);
					Layout.bCleanupStarted = true;
					Layout.bProjected = true;
					FunctionLayoutByIdentity.emplace(Function->get(), &Layout);
				}
			}
			if (const auto* Box = std::get_if<FScriptVariantHandle>(&Value); Box && *Box && (*Box)->Source)
			{
				auto Found = VariantLayoutByIdentity.find(Box->get());
				if (Found != VariantLayoutByIdentity.end() && Found->second->Owner.expired())
				{
					VariantLayoutByIdentity.erase(Found);
					Found = VariantLayoutByIdentity.end();
				}
				if (Found == VariantLayoutByIdentity.end())
				{
					if ((*Box)->Source->ProgramOwner != ProgramOwner)
					{
						if (SurvivorFailure.empty())
						{
							SurvivorFailure = "hot reload cleanup cannot restore a foreign variant";
						}
						return;
					}
					VariantLayouts.emplace_back();
					auto& Layout = VariantLayouts.back();
					Layout.Owner = *Box;
					auto PreviousSource = std::make_shared<FVariantSourceStamp>();
					PreviousSource->ProgramOwner = ProgramOwner;
					PreviousSource->Generation = State.ProgramGeneration;
					Layout.Source = std::move(PreviousSource);
					auto CleanupSource = std::make_shared<FVariantSourceStamp>();
					CleanupSource->ProgramOwner = ProgramOwner;
					CleanupSource->Generation = ProgramGeneration;
					(*Box)->Source = std::move(CleanupSource);
					VariantLayoutByIdentity.emplace(Box->get(), &Layout);
				}
			}
		};
		const auto CollectSurvivors = [&]()
		{
			SurvivorFailure.clear();
			KeepHandles.clear();
			for (const auto& Global : State.Globals)
			{
				CollectKeep(CollectKeep, Global);
			}
			for (const auto& Root : State.ExternalRoots)
			{
				CollectKeep(CollectKeep, Root);
			}
			for (std::size_t Slot = 0; Slot < Globals.size(); ++Slot)
			{
				if (RestoredSlots[Slot] != MissingSlot)
				{
					CollectKeep(CollectKeep, Globals[Slot]);
				}
			}
			for (std::size_t Index = 0; Index < ObjectLayouts.size(); ++Index)
			{
				for (const auto& Field : ObjectLayouts[Index].PreviousFields)
				{
					CollectKeep(CollectKeep, Field);
				}
			}
		};
		for (auto& Layout : FunctionLayouts)
		{
			Layout.bCleanupStarted = true;
		}
		CollectSurvivors();
		if (!DiscardValueRoots(Discarded, &KeepHandles))
		{
			RecordReleaseFailure();
		}
		if (!ReleasePendingScriptException())
		{
			RecordReleaseFailure();
		}
		// A replacement-only declaration cannot become a root of the old state.
		// Drain such callback-created storage before returning its borrowed roots.
		LocalStatics->bDestroying = true;
		bDetachSurvivorTemporary = true;
		for (;;)
		{
			for (auto& Layout : ObjectLayouts)
			{
				if (const auto Live = Layout.Object.lock(); Live && Layout.bProjected)
				{
					for (std::size_t Slot = 0; Slot < Layout.PreviousSlots.size() && Slot < Live->Fields.size(); ++Slot)
					{
						if (Layout.PreviousSlots[Slot] == MissingSlot)
						{
							DetachTemporary(Live->Fields[Slot]);
						}
					}
				}
			}
			for (auto It = LocalStatics->Completed.rbegin(); It != LocalStatics->Completed.rend(); ++It)
			{
				if (*It < Globals.size() && RestoredSlots[*It] == MissingSlot)
				{
					DetachTemporary(Globals[*It]);
					LocalStatics->States[*It] = ELocalStaticInitialization::Uninitialized;
				}
			}
			for (std::size_t Slot = Globals.size(); Slot > 0; --Slot)
			{
				if (RestoredSlots[Slot - 1] == MissingSlot && (Slot - 1 >= Module.GlobalMetadata.size() || !Module.GlobalMetadata[Slot - 1].bLocalStatic))
				{
					DetachTemporary(Globals[Slot - 1]);
				}
			}
			// This pass also enrols identities published by the preceding cleanup.
			// Detaching new fields before collecting their children keeps temporary
			// cycles out of the surviving old graph, including on the final pass.
			CollectSurvivors();
			if (Discarded.empty())
			{
				// Temporary destructors may replace an intermediate value with one
				// representable by the old module. Reject only the final live graph.
				if (!SurvivorFailure.empty())
				{
					MarkExecutionResetRequired();
					ReportError(SurvivorFailure);
					RecordReleaseFailure();
				}
				break;
			}
			if (!DiscardValueRoots(Discarded, &KeepHandles))
			{
				RecordReleaseFailure();
			}
			if (!ReleasePendingScriptException())
			{
				RecordReleaseFailure();
			}
		}
		return VmDetail::FinishCleanup(bReleasedAll, false, {}, std::move(FirstReleaseError),
		           bExitRequested, bHasError, Error) &&
		    !bHasError;
	}

	bool VeroScriptVm::ExecuteFunction(
	    const FBytecodeModule& Module,
	    const std::uint16_t FunctionIndex,
	    const std::vector<FValue>& Arguments,
	    const std::vector<FNativeFunction>* const Natives,
	    FValue* const OutReturnValue,
	    const std::vector<FValue>* const Captures)
	{
		if (RejectExecutionReuse()) return false;
		bPersistentExecution = false;
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		bNativeRootExecutionActive = true;
		bHasError = false;
		bExitRequested = false;
		RequestedExitCode = 0;
		EntryExitCode = 0;
		bHasPendingScriptException = false;
		PendingScriptException = std::int32_t{0};
		PendingScriptExceptionLocation = {};
		Error = {};
		BeginExecutionGovernance();
		Console::Detail::FScopedRestore ConsoleRestore(OutputStream);
		FMemoryExecutionScope MemoryExecution(*this);
		OperandStack.clear();
		ClearRuntimeFrames();
		SetPreferredJitFunction(FunctionIndex);
		PrepareJitForExecution(Module);
		LiveChannels.clear();
		SpawnTaskChannels.clear();
		WeakTargets.clear();
		EntryModulePath.clear();
		if (Module.MainFunctionIndex >= 0 && static_cast<std::size_t>(Module.MainFunctionIndex) < Module.Functions.size())
		{
			const FBytecodeFunction& MainFunction =
			    Module.Functions[static_cast<std::size_t>(Module.MainFunctionIndex)];
			const std::size_t Sep = MainFunction.Name.find('\x1F');
			EntryModulePath = Sep != std::string::npos
			    ? MainFunction.Name.substr(0, Sep)
			    : std::string(MainFunction.Location.FileLabel);
		}
		Globals.assign(Module.GlobalNames.size(), std::int32_t{0});
		LocalStatics.reset();
		if (Debugger)
		{
			Debugger->AttachVm(this);
		}
		const auto Finish = [this](const bool bOk) -> bool
		{
			struct FRestoreRetention
			{
				bool& Flag;
				bool Previous;
				~FRestoreRetention()
				{
					Flag = Previous;
				}
			} RestoreRetention{bNativeRetentionAllowed, bNativeRetentionAllowed};
			bNativeRetentionAllowed = false;
			if (Executor && (Executor.use_count() == 1 || !bOk || SpawnedTasks.HasActiveWorkers()))
			{
				Executor->AbandonMutexes();
			}
			// Only channels handed to spawned workers are cancellation resources.
			// Closing every channel created by this VM also closed channels that had
			// escaped into returned objects/globals, making them unusable by a later
			// Session call.
			for (auto& TaskChannel : SpawnTaskChannels)
			{
				FChannelHandle& Channel = TaskChannel.second;
				if (Channel)
				{
					Channel->Close();
				}
			}
			LiveChannels.clear();
			SpawnTaskChannels.clear();
			WeakTargets.clear();
			SpawnedTasks.RequestShutdown();
			SpawnedTasks.JoinAll();
			(void)SpawnedTasks.ResetCompleted();
			const bool bNativeRootsReleased = InvalidateNativeValueRoots();
			const bool bFramesReleased = DiscardExecutionAbove(0, 0, {});
			const bool bExceptionReleased = ReleasePendingScriptException();
			const bool bGlobalsReleased = ReleaseProgramGlobals({});
			const bool bCleanupExceptionReleased = ReleasePendingScriptException();
			if (Debugger)
			{
				Debugger->DetachVm();
			}
			FinishBackendExecution();
			ActiveModule = nullptr;
			ActiveNatives = nullptr;
			bNativeRootExecutionActive = false;
			return (bOk || (bExitRequested && !bHasError)) && bFramesReleased && bExceptionReleased &&
			    bGlobalsReleased && bCleanupExceptionReleased && bNativeRootsReleased && !HasError();
		};

		if (bHasError) return Finish(false);

		if (!Module.InitCode.empty())
		{
			FCallFrame InitFrame;
			InitFrame.Function = nullptr;
			InitFrame.InstructionPointer = 0;
			InitFrame.StackBase = 0;
			if (Module.InitLocalCount > 0)
			{
				InitFrame.Locals.assign(Module.InitLocalCount, std::int32_t{0});
			}
			Frames.push_back(std::move(InitFrame));
			if (!Run())
			{
				return Finish(false);
			}
			if (bExitRequested)
			{
				return Finish(true);
			}
			while (!OperandStack.empty())
			{
				FValue Discarded = Pop();
				(void)ReleaseValue(Discarded, {});
			}
		}

		if (FunctionIndex >= Module.Functions.size())
		{
			ReportError("invalid function index");
			return Finish(false);
		}
		const FBytecodeFunction& Target = Module.Functions[FunctionIndex];
		if ((Captures ? Captures->size() : 0) != Target.Captures.size())
		{
			ReportError("worker callable capture schema is invalid");
			return Finish(false);
		}
		if (Captures)
		{
			for (std::size_t Index = 0; Index < Captures->size(); ++Index)
			{
				const std::string& TypeName = Target.Captures[Index].TypeName;
				if (Target.Captures[Index].Mode == EClosureCaptureMode::Reference || !Concurrent::IsChannelPayload((*Captures)[Index]) || !ValueMatchesElementType((*Captures)[Index], TypeName, {}))
				{
					ReportError("worker callable capture type is not transferable");
					return Finish(false);
				}
			}
		}
		std::vector<FValue> WorkerEnvironment;
		if (Captures)
		{
			WorkerEnvironment = *Captures;
			if (!PrepareClosureCaptures(FunctionIndex, WorkerEnvironment)) return Finish(false);
		}
		if (!CallFunctionAt(FunctionIndex, Arguments, nullptr,
		        !Captures || Captures->empty()))
		{
			return Finish(false);
		}
		if (Captures && !Captures->empty())
		{
			if (Frames.empty() || !Frames.back().Function || Frames.back().Function != &Target)
			{
				ReportError("worker callable frame is unavailable");
				return Finish(false);
			}
			FCallFrame& Callee = Frames.back();
			for (std::size_t Index = 0; Index < Captures->size(); ++Index)
			{
				const std::size_t Slot = Target.ParamCount + Index;
				if (Slot >= Callee.Locals.size())
				{
					ReportError("worker callable capture slot out of range");
					return Finish(false);
				}
				Callee.Locals[Slot] = WorkerEnvironment[Index];
			}
		}
		if (!Run())
		{
			return Finish(false);
		}
		if (bExitRequested)
		{
			return Finish(!bHasError);
		}
		if (OutReturnValue)
		{
			if (OperandStack.empty())
			{
				ReportError("function produced no return value");
				return Finish(false);
			}
			if (!ExportFunctionReturn(
			        Module.Functions[FunctionIndex],
			        Pop(),
			        OutReturnValue))
			{
				return Finish(false);
			}
		}
		return Finish(!bHasError);
	}

	bool VeroScriptVm::ExecuteInstanceMethod(
	    const FBytecodeModule& Module,
	    FObjectHandle Object,
	    const std::string_view MethodName,
	    const std::vector<FValue>& Arguments,
	    FHotReloadState* const PersistentState,
	    const std::vector<FNativeFunction>* const Natives,
	    FValue* const OutReturnValue,
	    FHotReloadRollback* const InitializationRollback,
	    std::vector<FValue>* const OutArguments,
	    const bool bBypassEntryInterceptor)
	{
		if (Arguments.size() > 255)
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("instance method supports at most 255 arguments");
			return false;
		}
		if (OutArguments != nullptr && OutArguments->size() != Arguments.size())
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("instance method write-back requires OutArguments size == argument count");
			return false;
		}
		FInstanceMethodCall Call;
		Call.Object = std::move(Object);
		Call.MethodName = std::string(MethodName);
		Call.Arguments = Arguments.empty() ? nullptr : Arguments.data();
		Call.ArgumentCount = static_cast<std::uint8_t>(Arguments.size());
		Call.OutReturn = OutReturnValue;
		Call.OutArguments =
		    (OutArguments != nullptr && !OutArguments->empty()) ? OutArguments->data() : nullptr;
		Call.bBypassEntryInterceptor = bBypassEntryInterceptor;
		const bool bOk = ExecuteInternal(
		    Module,
		    ERunMode::Eval,
		    {},
		    Natives,
		    PersistentState,
		    InitializationRollback,
		    &Call);
		return bOk;
	}

	bool VeroScriptVm::TryResolveInstanceMethod(
	    const FBytecodeModule& Module,
	    const FObjectHandle& Object,
	    const std::string_view MethodName,
	    const std::uint8_t ArgumentCount,
	    std::uint16_t& OutFunctionIndex)
	{
		OutFunctionIndex = static_cast<std::uint16_t>(-1);
		if (!Object || MethodName.empty())
		{
			return false;
		}
		return ModuleMeta::TryResolveInstanceMethodOnType(
		           Module,
		           Object->GetTypeName(),
		           MethodName,
		           ArgumentCount,
		           OutFunctionIndex) == EMethodResolveStatus::Found;
	}

	bool VeroScriptVm::ConstructObject(
	    const FBytecodeModule& Module,
	    const std::string_view TypeName,
	    const std::vector<FValue>& Arguments,
	    FObjectHandle& OutObject,
	    FHotReloadState* const PersistentState,
	    const std::vector<FNativeFunction>* const Natives,
	    FHotReloadRollback* const InitializationRollback)
	{
		if (PersistentState && PersistentState->bExecutionResetRequired)
		{
			MarkExecutionResetRequired();
		}
		if (RejectExecutionReuse()) return false;
		if (TypeName.empty())
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("ConstructObject requires a type name");
			OutObject = {};
			return false;
		}
		if (Arguments.size() > 255)
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("ConstructObject supports at most 255 arguments");
			OutObject = {};
			return false;
		}
		FObjectHandle Constructed;
		FInstanceMethodCall Call;
		Call.ConstructTypeName = std::string(TypeName);
		Call.Arguments = Arguments.empty() ? nullptr : Arguments.data();
		Call.ArgumentCount = static_cast<std::uint8_t>(Arguments.size());
		Call.OutConstructed = &Constructed;
		const bool bOk = ExecuteInternal(
		    Module,
		    ERunMode::Eval,
		    {},
		    Natives,
		    PersistentState,
		    InitializationRollback,
		    &Call);
		OutObject = std::move(Constructed);
		return bOk;
	}

	bool VeroScriptVm::ExecuteFreeFunction(
	    const FBytecodeModule& Module,
	    const std::string_view FunctionName,
	    const std::vector<FValue>& Arguments,
	    FHotReloadState* const PersistentState,
	    const std::vector<FNativeFunction>* const Natives,
	    FValue* const OutReturnValue,
	    FHotReloadRollback* const InitializationRollback,
	    std::vector<FValue>* const OutArguments)
	{
		return ExecuteNamedFreeFunction(Module, FunctionName, Arguments, PersistentState, Natives,
		    OutReturnValue, InitializationRollback, OutArguments, nullptr);
	}

	bool VeroScriptVm::ExecuteNamedFreeFunction(
	    const FBytecodeModule& Module,
	    const std::string_view FunctionName,
	    const std::vector<FValue>& Arguments,
	    FHotReloadState* const PersistentState,
	    const std::vector<FNativeFunction>* const Natives,
	    FValue* const OutReturnValue,
	    FHotReloadRollback* const InitializationRollback,
	    std::vector<FValue>* const OutArguments,
	    ModuleMeta::FFreeFunctionLookupCache* const FreeFunctionLookup)
	{
		if (Arguments.size() > 255)
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("free function supports at most 255 arguments");
			return false;
		}
		if (OutArguments != nullptr && OutArguments->size() != Arguments.size())
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("free function write-back requires OutArguments size == argument count");
			return false;
		}
		FInstanceMethodCall Call;
		Call.bFreeFunction = true;
		Call.FreeFunctionLookup = FreeFunctionLookup;
		Call.MethodName = std::string(FunctionName);
		Call.Arguments = Arguments.empty() ? nullptr : Arguments.data();
		Call.ArgumentCount = static_cast<std::uint8_t>(Arguments.size());
		Call.OutReturn = OutReturnValue;
		Call.OutArguments =
		    (OutArguments != nullptr && !OutArguments->empty()) ? OutArguments->data() : nullptr;
		const bool bOk = ExecuteInternal(
		    Module,
		    ERunMode::Eval,
		    {},
		    Natives,
		    PersistentState,
		    InitializationRollback,
		    &Call);
		return bOk && !bHasError;
	}

	bool VeroScriptVm::ExecuteCallable(
	    const FBytecodeModule& Module,
	    const FFunctionHandle& Callable,
	    const std::vector<FValue>& Arguments,
	    FHotReloadState* const PersistentState,
	    const std::vector<FNativeFunction>* const Natives,
	    FValue* const OutReturnValue,
	    FHotReloadRollback* const InitializationRollback,
	    std::vector<FValue>* const OutArguments)
	{
		if (!Callable)
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("cannot invoke an unbound script callable");
			return false;
		}
		if (Arguments.size() > 255)
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("script callable supports at most 255 arguments");
			return false;
		}
		if (OutArguments != nullptr && OutArguments->size() != Arguments.size())
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("script callable write-back requires OutArguments size == argument count");
			return false;
		}
		FInstanceMethodCall Call;
		Call.Callable = Callable;
		Call.Arguments = Arguments.empty() ? nullptr : Arguments.data();
		Call.ArgumentCount = static_cast<std::uint8_t>(Arguments.size());
		Call.OutReturn = OutReturnValue;
		Call.OutArguments =
		    (OutArguments != nullptr && !OutArguments->empty()) ? OutArguments->data() : nullptr;
		const bool bOk = ExecuteInternal(
		    Module,
		    ERunMode::Eval,
		    {},
		    Natives,
		    PersistentState,
		    InitializationRollback,
		    &Call);
		return bOk;
	}

	bool VeroScriptVm::ExecuteBoundInstanceFunction(
	    const FBytecodeModule& Module,
	    const FObjectHandle& Object,
	    const std::uint16_t FunctionIndex,
	    FHotReloadState* const PersistentState,
	    const std::vector<FNativeFunction>* const Natives,
	    FHotReloadRollback* const InitializationRollback)
	{
		if (!Object || FunctionIndex >= Module.Functions.size())
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("bound instance call has an invalid object or function identity");
			return false;
		}
		const FBytecodeFunction& Function = Module.Functions[FunctionIndex];
		if (!Function.bIsMethod || Function.ParamCount != 0)
		{
			if (!PrepareExecutionRequest(PersistentState)) return false;
			ReportError("bound instance entry must be a zero-argument instance function");
			return false;
		}
		FInstanceMethodCall Call;
		Call.Object = Object;
		Call.BoundFunctionIndex = FunctionIndex;
		const bool bOk = ExecuteInternal(
		    Module,
		    ERunMode::Eval,
		    {},
		    Natives,
		    PersistentState,
		    InitializationRollback,
		    &Call);
		return bOk;
	}

	bool VeroScriptVm::ValidateObjectLayout(
	    const FBytecodeModule& Module,
	    const FObjectHandle& Object,
	    const std::vector<FNativeFunction>* const Natives)
	{
		if (!PrepareExecutionRequest()) return false;
		bHasPendingScriptException = false;
		PendingScriptException = std::int32_t{0};
		PendingScriptExceptionLocation = {};
		if (!Object)
		{
			ReportError("object layout validation requires an object");
			return false;
		}

		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		if (Frames.empty())
		{
			BeginExecutionGovernance();
		}
		const auto Finish = [this](const bool bResult)
		{
			FinishBackendExecution();
			ActiveModule = nullptr;
			ActiveNatives = nullptr;
			return bResult;
		};

		std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
		if (!ModuleMeta::FindTypeIndex(Module, Object->GetTypeName(), TypeIndex) || TypeIndex >= Module.Types.size() || Object->Fields.size() != Module.Types[TypeIndex].Fields.size())
		{
			ReportError("object layout does not match the loaded module: " + Object->GetTypeName());
			return Finish(false);
		}
		const FBytecodeTypeDesc& Type = Module.Types[TypeIndex];
		if (Type.HostBaseTypeName.empty())
		{
			if (Object->GetHostBase())
			{
				ReportError("object has an unexpected host base");
				return Finish(false);
			}
		}
		else if (!Object->GetHostBase() || Object->GetHostBase()->TypeName != Type.HostBaseTypeName)
		{
			ReportError("object host base does not match the loaded module");
			return Finish(false);
		}
		for (std::size_t Index = 0; Index < Type.Fields.size(); ++Index)
		{
			const FValue* const Visible =
			    UnwrapClosureReferenceCell(&Object->Fields[Index]);
			const auto* Empty = Visible ? std::get_if<std::int32_t>(Visible) : nullptr;
			if (Empty && *Empty == 0 && std::any_of(Type.Fields[Index].DeclMeta.begin(), Type.Fields[Index].DeclMeta.end(), [](const auto& Entry)
			{
				return Entry.first == UninitializedPayloadMetaKey;
			}))
			{
				continue;
			}
			if (!Visible || !ValueMatchesElementType(*Visible, Type.Fields[Index].TypeName, {}))
			{
				ReportError("field value type mismatch: " + Type.Fields[Index].Name);
				return Finish(false);
			}
		}
		return Finish(true);
	}

	bool VeroScriptVm::ReadObjectField(
	    const FBytecodeModule& Module,
	    const FObjectHandle& Object,
	    const std::string_view FieldName,
	    FValue& OutValue,
	    const std::vector<FNativeFunction>* const Natives)
	{
		if (!PrepareExecutionRequest()) return false;
		// The output may contain the receiver itself or alias one of its fields.
		// Keep the receiver alive and finish the read before replacing that output.
		const FObjectHandle Receiver = Object;
		if (!Receiver || FieldName.empty())
		{
			ReportError("ReadObjectField requires object and field name");
			OutValue = {};
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		if (!ModuleMeta::FindFieldIndex(Module, Receiver->GetTypeName(), FieldName, FieldIndex))
		{
			ReportErrorId("diag.unknown_field", {}, {FieldName});
			OutValue = {};
			return false;
		}
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		if (Frames.empty())
		{
			BeginExecutionGovernance();
		}
		FValue ReadValue;
		const bool bOk = GetField(*Receiver, FieldIndex, ReadValue, {});
		FinishBackendExecution();
		ActiveModule = nullptr;
		ActiveNatives = nullptr;
		OutValue = std::move(ReadValue);
		return bOk;
	}

	bool VeroScriptVm::WriteObjectField(
	    const FBytecodeModule& Module,
	    const FObjectHandle& Object,
	    const std::string_view FieldName,
	    const FValue& Value,
	    const std::vector<FNativeFunction>* const Natives)
	{
		if (!PrepareExecutionRequest()) return false;
		if (!Object || FieldName.empty())
		{
			ReportError("WriteObjectField requires object and field name");
			return false;
		}
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		if (!ModuleMeta::FindFieldIndex(Module, Object->GetTypeName(), FieldName, FieldIndex))
		{
			ReportErrorId("diag.unknown_field", {}, {FieldName});
			return false;
		}
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		if (Frames.empty())
		{
			BeginExecutionGovernance();
		}
		if (FieldIndex >= Object->Fields.size())
		{
			ReportError("invalid field index");
			FinishBackendExecution();
			ActiveModule = nullptr;
			ActiveNatives = nullptr;
			return false;
		}
		std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
		if (!ModuleMeta::FindTypeIndex(Module, Object->GetTypeName(), TypeIndex) || TypeIndex >= Module.Types.size() || FieldIndex >= Module.Types[TypeIndex].Fields.size() || !ValueMatchesElementType(Value, Module.Types[TypeIndex].Fields[FieldIndex].TypeName, {}))
		{
			ReportError("field value type mismatch: " + std::string(FieldName));
			FinishBackendExecution();
			ActiveModule = nullptr;
			ActiveNatives = nullptr;
			return false;
		}
		FValue* const Storage = ResolveRefStorage(Object->Fields[FieldIndex]);
		if (!Storage)
		{
			ReportError("invalid referenced field storage: " + std::string(FieldName));
			FinishBackendExecution();
			ActiveModule = nullptr;
			ActiveNatives = nullptr;
			return false;
		}
		FValue Destination = FThisFieldRef{Object, FieldIndex};
		const bool bOk = AssignClonedValue(Destination, Value, {});
		if (bOk)
		{
			StampContainerMutationOrigin(Object->Fields.at(FieldIndex), *Object, FieldIndex);
			NotifyFieldMutation(*Object, FieldIndex);
		}
		FinishBackendExecution();
		ActiveModule = nullptr;
		ActiveNatives = nullptr;
		return bOk && !bHasError;
	}

	bool VeroScriptVm::WriteObjectFields(
	    const FBytecodeModule& Module,
	    const FObjectHandle& Object,
	    const std::vector<std::pair<std::string, FValue>>& Fields,
	    const std::vector<FNativeFunction>* const Natives)
	{
		if (!PrepareExecutionRequest()) return false;
		if (!Object)
		{
			ReportError("field write requires object");
			return false;
		}
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		if (Frames.empty())
		{
			BeginExecutionGovernance();
		}
		std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
		if (!ModuleMeta::FindTypeIndex(Module, Object->GetTypeName(), TypeIndex) || TypeIndex >= Module.Types.size() || Object->Fields.size() != Module.Types[TypeIndex].Fields.size())
		{
			ReportError("object layout does not match the loaded module: " + Object->GetTypeName());
			FinishBackendExecution();
			ActiveModule = nullptr;
			ActiveNatives = nullptr;
			return false;
		}

		struct FPreparedField
		{
			std::uint16_t Index = 0;
			FValue Value;
			bool bStructAssignment = false;
		};
		std::vector<FPreparedField> Prepared;
		Prepared.reserve(Fields.size());
		const auto ReleasePrepared = [&]()
		{
			for (FPreparedField& Existing : Prepared)
			{
				(void)ReleaseValue(Existing.Value, {});
			}
			Prepared.clear();
		};
		std::unordered_set<std::uint16_t> Seen;
		for (const auto& Assignment : Fields)
		{
			std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
			if (!ModuleMeta::FindFieldIndex(
			        Module, Object->GetTypeName(), Assignment.first, FieldIndex) ||
			    FieldIndex >= Module.Types[TypeIndex].Fields.size() || !Seen.insert(FieldIndex).second)
			{
				ReportError("unknown or duplicate field assignment: " + Assignment.first);
				ReleasePrepared();
				FinishBackendExecution();
				ActiveModule = nullptr;
				ActiveNatives = nullptr;
				return false;
			}
			if (!ValueMatchesElementType(
			        Assignment.second,
			        Module.Types[TypeIndex].Fields[FieldIndex].TypeName,
			        {}))
			{
				ReportError("field value type mismatch: " + Assignment.first);
				ReleasePrepared();
				FinishBackendExecution();
				ActiveModule = nullptr;
				ActiveNatives = nullptr;
				return false;
			}
			FValue Cloned;
			FValue* const Storage = ResolveRefStorage(Object->Fields[FieldIndex]);
			const auto* Existing = Storage ? std::get_if<FObjectHandle>(Storage) : nullptr;
			const bool StructAssignment = Existing && *Existing && (*Existing)->Kind == EScriptObjectKind::Struct;
			if (StructAssignment)
			{
				const auto Name = MakeMethodFunctionName((*Existing)->GetTypeName(), "@copy_assign", {"const ref " + (*Existing)->GetTypeName()});
				const auto Service = Module.FunctionIndices.find(Name);
				if (Service == Module.FunctionIndices.end() || Module.Functions[Service->second].bDeleted || !Module.Functions[Service->second].UnavailableReason.empty() || Module.Functions[Service->second].Access != EMemberAccess::Public)
				{
					ReportError("field copy assignment is unavailable: " + Assignment.first);
					ReleasePrepared();
					FinishBackendExecution();
					ActiveModule = nullptr;
					ActiveNatives = nullptr;
					return false;
				}
				Cloned = Assignment.second;
			}
			else if (!CloneValueOrReport(Assignment.second, Cloned, {}))
			{
				ReleasePrepared();
				FinishBackendExecution();
				ActiveModule = nullptr;
				ActiveNatives = nullptr;
				return false;
			}
			if (!Storage)
			{
				ReportError("invalid referenced field storage: " + Assignment.first);
				(void)ReleaseValue(Cloned, {});
				ReleasePrepared();
				FinishBackendExecution();
				ActiveModule = nullptr;
				ActiveNatives = nullptr;
				return false;
			}
			Prepared.push_back({FieldIndex, std::move(Cloned), StructAssignment});
		}

		std::vector<FValue> Previous;
		Previous.reserve(Prepared.size());
		for (FPreparedField& Field : Prepared)
		{
			FValue Binding = FThisFieldRef{Object, Field.Index};
			FValue* Storage = ResolveRefStorage(Binding);
			if (!Storage || (Field.bStructAssignment && !AssignClonedValue(Binding, Field.Value, {})))
			{
				ReleasePrepared();
				(void)ReleaseValueList(Previous, {});
				FinishBackendExecution();
				ActiveModule = nullptr;
				ActiveNatives = nullptr;
				return false;
			}
			if (!Field.bStructAssignment)
			{
				Previous.push_back(std::move(*Storage));
				*Storage = std::move(Field.Value);
			}
			StampContainerMutationOrigin(
			    Object->Fields.at(Field.Index), *Object, Field.Index);
			NotifyFieldMutation(*Object, Field.Index);
		}
		const bool bReleased = ReleaseValueList(Previous, {});
		FinishBackendExecution();
		ActiveModule = nullptr;
		ActiveNatives = nullptr;
		return bReleased && !bHasError;
	}

	bool VeroScriptVm::AssignRetainedValue(
	    const FBytecodeModule& Module,
	    FValue& Dest,
	    const FValue& Src,
	    const std::vector<FNativeFunction>* const Natives)
	{
		if (!PrepareExecutionRequest()) return false;
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives))
		{
			ActiveModule = nullptr;
			return false;
		}
		if (Frames.empty())
		{
			BeginExecutionGovernance();
		}
		FValue* const Storage = ResolveRefStorage(Dest);
		const bool bOk = Storage && AssignClonedValue(Dest, Src, {});
		if (!Storage)
		{
			ReportError("invalid retained reference storage");
		}
		FinishBackendExecution();
		ActiveModule = nullptr;
		ActiveNatives = nullptr;
		return bOk;
	}

	VeroScriptVm::FDebugCallFrameView VeroScriptVm::DebugGetFrame(const std::size_t Index) const
	{
		FDebugCallFrameView View;
		if (Index >= Frames.size())
		{
			return View;
		}
		const FCallFrame& Frame = Frames[Index];
		View.Function = Frame.Function;
		View.InstructionPointer = Frame.InstructionPointer;
		View.Locals = &Frame.Locals;
		return View;
	}

	bool VeroScriptVm::DebugSetLocal(
	    const std::size_t FrameIndex,
	    const std::size_t Slot,
	    FValue Value)
	{
		if (FrameIndex >= Frames.size())
		{
			return false;
		}
		FCallFrame& Frame = Frames[FrameIndex];
		if (Slot >= Frame.Locals.size())
		{
			return false;
		}
		if (Frame.Function && Slot >= Frame.Function->ParamCount && Slot - Frame.Function->ParamCount < Frame.Function->Captures.size() && !Frame.Function->Captures[Slot - Frame.Function->ParamCount].bWritable)
		{
			return false;
		}
		FValue& Binding = Frame.Locals[Slot];
		FValue* const Storage = ResolveRefStorage(Binding);
		if (!Storage || !AssignValue(*Storage, std::move(Value), {}))
		{
			return false;
		}
		if (Storage != &Binding)
		{
			NotifyReferenceMutation(Binding);
		}
		return !bHasError;
	}

	bool VeroScriptVm::DebugAssignValue(FValue& Target, FValue Value)
	{
		FValue* const Storage = ResolveRefStorage(Target);
		return Storage && AssignValue(*Storage, std::move(Value), {});
	}

	bool VeroScriptVm::DebugCommitObjectFieldMutation(
	    const FObjectHandle& Object,
	    const std::uint16_t FieldIndex)
	{
		if (!Object || FieldIndex >= Object->Fields.size())
		{
			return false;
		}
		FValue* const Storage = ResolveRefStorage(Object->Fields[FieldIndex]);
		if (!Storage)
		{
			return false;
		}
		StampContainerMutationOrigin(*Storage, *Object, FieldIndex);
		NotifyFieldMutation(*Object, FieldIndex);
		return !bHasError;
	}

	bool VeroScriptVm::DebugCommitContainerMutation(
	    const FValue& Container,
	    const EMutationKind Kind)
	{
		NotifyContainerMutation(Container, Kind);
		return !bHasError;
	}

	bool VeroScriptVm::ExecuteResolvedFreeFunction(
	    const FBytecodeModule& Module,
	    const std::uint16_t FunctionIndex,
	    const std::vector<FValue>& Arguments,
	    FHotReloadState* const PersistentState,
	    const std::vector<FNativeFunction>* const Natives,
	    FValue* const OutReturnValue,
	    FHotReloadRollback* const InitializationRollback,
	    std::vector<FValue>* const OutArguments)
	{
		if (FunctionIndex >= Module.Functions.size() || Module.Functions[FunctionIndex].bIsMethod)
		{
			ReportError("resolved free function has an invalid identity");
			return false;
		}
		if (Arguments.size() > 255)
		{
			ReportError("free function supports at most 255 arguments");
			return false;
		}
		if (OutArguments != nullptr && OutArguments->size() != Arguments.size())
		{
			ReportError("free function write-back requires OutArguments size == argument count");
			return false;
		}
		FInstanceMethodCall Call;
		Call.bFreeFunction = true;
		Call.bResolvedFreeFunction = true;
		Call.BoundFunctionIndex = FunctionIndex;
		Call.Arguments = Arguments.empty() ? nullptr : Arguments.data();
		Call.ArgumentCount = static_cast<std::uint8_t>(Arguments.size());
		Call.OutReturn = OutReturnValue;
		Call.OutArguments =
		    (OutArguments != nullptr && !OutArguments->empty()) ? OutArguments->data() : nullptr;
		const bool bOk = ExecuteInternal(
		    Module,
		    ERunMode::Eval,
		    {},
		    Natives,
		    PersistentState,
		    InitializationRollback,
		    &Call);
		return bOk && !bHasError;
	}

	bool VeroScriptVm::ExecuteInternal(
	    const FBytecodeModule& Module,
	    const ERunMode RunMode,
	    const std::vector<std::string_view>& ScriptArgs,
	    const std::vector<FNativeFunction>* const Natives,
	    FHotReloadState* const PersistentState,
	    FHotReloadRollback* const InitializationRollback,
	    const FInstanceMethodCall* const InstanceCall)
	{
		if (!PrepareExecutionRequest(PersistentState)) return false;
		bPersistentExecution = PersistentState != nullptr;
		TWeakSlotLease WeakSlotLease(WeakTargets, PersistentState);
		std::string DependencyError;
		const std::vector<FExternalDependency> EmptyDependencies;
		if (!CheckExternalDependencies(Module.RequiredExternalDependencies,
		        ModuleEnvironment ? ModuleEnvironment->GetDescription().AvailableDependencies : EmptyDependencies, DependencyError))
		{
			ReportError(DependencyError);
			return false;
		}
		if (bUseEnvironmentBindings && ModuleEnvironment && ModuleEnvironment->GetDescription().Bindings && Natives &&
		    Natives != &ModuleEnvironment->GetDescription().Bindings->GetNativeFunctions())
		{
			const auto& Expected = ModuleEnvironment->GetDescription().Bindings->GetNativeFunctions();
			std::unordered_map<std::string, const FNativeFunction*> Supplied;
			for (const auto& F : *Natives)
			{
				if (!Supplied.emplace(F.Name, &F).second)
				{
					ReportError("duplicate runtime native declaration");
					return false;
				}
			}

			for (const auto& F : Expected)
			{
				const auto I = Supplied.find(F.Name);
				if (I == Supplied.end() || !StandardLibrary::NativeFunctionsEquivalent(F, *I->second))
				{
					ReportError("runtime native table disagrees with the module environment: " + F.Name);
					return false;
				}
			}
			for (const auto& F : *Natives)
			{
				if (std::any_of(Expected.begin(), Expected.end(),
				        [&](const auto& E)
				{
					return E.Name == F.Name;
				}))
				{
					continue;
				}

				const auto& Standard = StandardLibrary::GetBindingCatalog().GetNativeFunctions();
				const auto I = std::find_if(Standard.begin(), Standard.end(),
				    [&](const auto& E)
				{
					return E.Name == F.Name;
				});
				if (I == Standard.end() || !StandardLibrary::NativeFunctionsEquivalent(F, *I))
				{
					ReportError("unexpected runtime native declaration: " + F.Name);
					return false;
				}
			}
		}
		ActiveModule = &Module;
		if (!SetActiveNatives(Natives ? Natives : bUseEnvironmentBindings && ModuleEnvironment && ModuleEnvironment->GetDescription().Bindings ? &ModuleEnvironment->GetDescription().Bindings->GetNativeFunctions()
		                                                                                                                                  : nullptr))
		{
			ActiveModule = nullptr;
			return false;
		}
		bNativeRootExecutionActive = true;
		bHasError = false;
		bExitRequested = false;
		RequestedExitCode = 0;
		EntryExitCode = 0;
		bHasPendingScriptException = false;
		PendingScriptException = std::int32_t{0};
		PendingScriptExceptionLocation = {};
		Error = {};
		BeginExecutionGovernance();
		Console::Detail::FScopedRestore ConsoleRestore(OutputStream);
		FMemoryExecutionScope MemoryExecution(*this);
		OperandStack.clear();
		ClearRuntimeFrames();
		// Warm capacity for typical scripts (avoids early realloc in hot loops/calls).
		if (OperandStack.capacity() < 64)
		{
			OperandStack.reserve(64);
		}
		if (Frames.capacity() < 32)
		{
			Frames.reserve(32);
		}
		if (FreeFrames.capacity() < 16)
		{
			FreeFrames.reserve(16);
		}
		PrepareJitForExecution(Module);
		LiveChannels.clear();
		SpawnTaskChannels.clear();
		if (!PersistentState)
		{
			WeakTargets.clear();
		}
		EntryModulePath.clear();
		if (Module.MainFunctionIndex >= 0 && static_cast<std::size_t>(Module.MainFunctionIndex) < Module.Functions.size())
		{
			const FBytecodeFunction& MainFunction =
			    Module.Functions[static_cast<std::size_t>(Module.MainFunctionIndex)];
			const std::size_t Sep = MainFunction.Name.find('\x1F');
			EntryModulePath = Sep != std::string::npos
			    ? MainFunction.Name.substr(0, Sep)
			    : std::string(MainFunction.Location.FileLabel);
		}
		const bool bUseCapturedGlobals = PersistentState && !InitializationRollback && Globals.empty() &&
		    PersistentState->HasCapturedModule(Module, JitProgramOwner, ProgramGeneration) &&
		    PersistentState->bCapturedGlobalsInSlotOrder &&
		    PersistentState->Globals.size() == Module.GlobalNames.size();
		if (!bUseCapturedGlobals)
		{
			Globals.assign(Module.GlobalNames.size(), std::int32_t{0});
		}
		LocalStatics.reset();
		LocalStatics = PersistentState ? PersistentState->LocalStatics : nullptr;
		if (PersistentState && PersistentState->bNeedsModuleInitialization && LocalStatics)
		{
			// Initialization may fail before the staged state is installed.
			LocalStatics = std::make_shared<FLocalStaticState>(*LocalStatics);
		}
		HotReloadRetainedGlobalSlots.assign(Globals.size(), false);
		HotReloadSkipInitByte.clear();
		bHotReloadModuleInitializing = false;
		bDemandGlobalInitializing = false;
		if (Debugger)
		{
			Debugger->AttachVm(this);
		}
		bool bPersistentStateInstalled = false;
		const auto Finish =
		    [this, &Module, PersistentState, &bPersistentStateInstalled](
		        const bool bOk) -> bool
		{
			struct FRestoreRetention
			{
				bool& Flag;
				bool Previous;
				~FRestoreRetention()
				{
					Flag = Previous;
				}
			} RestoreRetention{bNativeRetentionAllowed, bNativeRetentionAllowed};
			bNativeRetentionAllowed = false;
			bHotReloadModuleInitializing = false;
			bDemandGlobalInitializing = false;
			if (Executor && (Executor.use_count() == 1 || !bOk || SpawnedTasks.HasActiveWorkers()))
			{
				Executor->AbandonMutexes();
			}
			HotReloadRetainedGlobalSlots.clear();
			HotReloadSkipInitByte.clear();
			// Unblock workers waiting on channel_recv before joining outstanding tasks.
			for (auto& TaskChannel : SpawnTaskChannels)
			{
				FChannelHandle& Channel = TaskChannel.second;
				if (Channel)
				{
					Channel->Close();
				}
			}
			LiveChannels.clear();
			SpawnTaskChannels.clear();
			if (!PersistentState)
			{
				WeakTargets.clear();
			}
			SpawnedTasks.RequestShutdown();
			SpawnedTasks.JoinAll();
			(void)SpawnedTasks.ResetCompleted();
			const bool bNativeRootsReleased =
			    (PersistentState && bPersistentStateInstalled && !bExitRequested && NativeRootCleanup) || InvalidateNativeValueRoots();
			// Release frames/operands while ActiveModule is still valid so failure
			// paths run the same language destructors as ordinary frame returns.
			const bool bFramesReleased = DiscardExecutionAbove(0, 0, {});
			const bool bExceptionReleased = ReleasePendingScriptException();
			if (PersistentState != nullptr && bPersistentStateInstalled)
			{
				PersistentState->LocalStatics = LocalStatics;
				CaptureHotReloadState(*PersistentState, Module, Globals, ProgramGeneration, JitProgramOwner);
			}
			else if (PersistentState && LocalStatics == PersistentState->LocalStatics && LocalStatics)
			{
				LocalStatics = std::make_shared<FLocalStaticState>(*LocalStatics);
			}
			const bool bGlobalsReleased = ReleaseProgramGlobals({});
			const bool bCleanupExceptionReleased = ReleasePendingScriptException();
			if (PersistentState && bPersistentStateInstalled)
			{
				PersistentState->bExecutionResetRequired = RequiresReset();
			}
			if (Debugger)
			{
				Debugger->DetachVm();
			}
			FinishBackendExecution();
			ActiveModule = nullptr;
			ActiveNatives = nullptr;
			bNativeRootExecutionActive = false;
			// Global cleanup can request exit after an uncaught script exception.
			// Report the final control state; a retained runtime fault still fails.
			return (bOk || (bExitRequested && !bHasError)) && bFramesReleased && bExceptionReleased &&
			    bGlobalsReleased && bCleanupExceptionReleased && bNativeRootsReleased && !HasError();
		};

		if (bHasError) return Finish(false);

		if (PersistentState != nullptr && !bUseCapturedGlobals)
		{
			if (PersistentState->GlobalNames.size() != PersistentState->Globals.size())
			{
				ReportError("hot reload state has inconsistent global metadata");
				return Finish(false);
			}
			for (std::size_t Index = 0;
			    Index < PersistentState->Globals.size();
			    ++Index)
			{
				const auto Slot =
				    Module.GlobalSlots.find(PersistentState->GlobalNames[Index]);
				if (Slot == Module.GlobalSlots.end() || Slot->second >= Globals.size())
				{
					ReportError(
					    "hot reload retained global is missing: " + PersistentState->GlobalNames[Index]);
					return Finish(false);
				}
				if (Slot->second < Module.GlobalMetadata.size() && Module.GlobalMetadata[Slot->second].bConstexpr && !Module.GlobalMetadata[Slot->second].bLocalStatic)
				{
					continue;
				} // The new generation initializes its newly compiled constant.
				if (!AssignValue(
				        Globals[Slot->second],
				        PersistentState->Globals[Index],
				        {}))
				{
					return Finish(false);
				}
				HotReloadRetainedGlobalSlots[Slot->second] = true;
			}
		}

		const bool bRunModuleInitialization = PersistentState == nullptr || PersistentState->bNeedsModuleInitialization;
		if (bRunModuleInitialization && !Module.InitCode.empty())
		{
			bHotReloadModuleInitializing =
			    PersistentState != nullptr && !PersistentState->Empty();
			if (bHotReloadModuleInitializing)
			{
				// Innermost-range-wins skip map. Init ranges are laminar (nested or
				// disjoint). Fill outermost spans first, then nested ones, so the
				// innermost retained/non-retained decision sticks for every byte:
				//   retained ⊃ non-retained          → punch (run new nested init)
				//   non-retained ⊃ retained          → keep skip (no reverse re-run)
				//   retained ⊃ non-retained ⊃ retained → inner retained stays skipped
				HotReloadSkipInitByte.assign(Module.InitCode.size(), 0);
				std::vector<const FBytecodeGlobalInitRange*> Ordered;
				Ordered.reserve(Module.GlobalInitRanges.size());
				for (const FBytecodeGlobalInitRange& Range : Module.GlobalInitRanges)
				{
					if (Range.EndOffset <= Range.BeginOffset || Range.EndOffset > Module.InitCode.size())
					{
						continue;
					}
					Ordered.push_back(&Range);
				}
				std::stable_sort(
				    Ordered.begin(),
				    Ordered.end(),
				    [](const FBytecodeGlobalInitRange* Lhs, const FBytecodeGlobalInitRange* Rhs)
				{
					const std::size_t LhsSpan = Lhs->EndOffset - Lhs->BeginOffset;
					const std::size_t RhsSpan = Rhs->EndOffset - Rhs->BeginOffset;
					if (LhsSpan != RhsSpan)
					{
						return LhsSpan > RhsSpan;
					}
					if (Lhs->BeginOffset != Rhs->BeginOffset)
					{
						return Lhs->BeginOffset < Rhs->BeginOffset;
					}
					return Lhs->EndOffset > Rhs->EndOffset;
				});
				for (const FBytecodeGlobalInitRange* const Range : Ordered)
				{
					const bool bRetained =
					    Range->GlobalSlot < HotReloadRetainedGlobalSlots.size() && HotReloadRetainedGlobalSlots[Range->GlobalSlot];
					std::fill(
					    HotReloadSkipInitByte.begin() + Range->BeginOffset,
					    HotReloadSkipInitByte.begin() + Range->EndOffset,
					    static_cast<std::uint8_t>(bRetained ? 1 : 0));
				}
			}
			DemandGlobalInitializers.clear();
			DemandGlobalStates.clear();
			DemandGlobalCode.clear();
			DemandGlobalStarts.clear();
			for (const auto& Range : Module.GlobalInitRanges)
			{
				if (!Range.bDemandInitialized || Range.GlobalSlot >= Globals.size() || Range.EndOffset <= Range.BeginOffset)
				{
					continue;
				}
				if (DemandGlobalInitializers.empty())
				{
					DemandGlobalInitializers.resize(Globals.size(), nullptr);
					DemandGlobalStates.resize(Globals.size(), 0);
					DemandGlobalCode.resize(Globals.size());
				}
				DemandGlobalInitializers[Range.GlobalSlot] = &Range;
				DemandGlobalStarts.emplace(Range.BeginOffset, Range.GlobalSlot);
				if (Range.GlobalSlot < HotReloadRetainedGlobalSlots.size() && HotReloadRetainedGlobalSlots[Range.GlobalSlot])
				{
					DemandGlobalStates[Range.GlobalSlot] = 2;
				}
			}
			bDemandGlobalInitializing = !DemandGlobalStarts.empty();
			bool bInitHandledByJit = false;
			bool bInitOk = true;
			if (!bDemandGlobalInitializing && RunMode == ERunMode::EvalOnce && PersistentState == nullptr && InstanceCall == nullptr)
			{
				bInitOk = TryExecuteOneShotInitJit(Module, bInitHandledByJit);
			}
			if (!bInitHandledByJit)
			{
				FCallFrame InitFrame;
				InitFrame.Function = nullptr;
				InitFrame.InstructionPointer = 0;
				InitFrame.StackBase = 0;
				if (Module.InitLocalCount > 0)
				{
					InitFrame.Locals.assign(Module.InitLocalCount, std::int32_t{0});
				}
				Frames.push_back(std::move(InitFrame));
				bInitOk = Run();
			}
			if (!bInitOk)
			{
				return Finish(false);
			}
			if (bExitRequested)
			{
				return Finish(true);
			}
			while (!OperandStack.empty())
			{
				FValue Discarded = Pop();
				(void)ReleaseValue(Discarded, {});
			}
			bHotReloadModuleInitializing = false;
			bDemandGlobalInitializing = false;
			HotReloadSkipInitByte.clear();
		}

		if (PersistentState != nullptr)
		{
			// Globals now own the live values. Keeping the previous snapshot here
			// would add artificial shared_ptr owners and suppress last-owner script
			// destructors when main replaces a global. Initialization succeeded, so
			// its one-shot rollback journal is no longer needed either.
			if (bUseCapturedGlobals)
			{
				// An unchanged owned generation can transfer the slot vector as-is.
				// Until this point, preparation failure still leaves State untouched.
				Globals.swap(PersistentState->Globals);
			}
			else
			{
				PersistentState->Globals.clear();
			}
			if (InitializationRollback != nullptr)
			{
				InitializationRollback->Clear();
			}
			bPersistentStateInstalled = true;
		}

		if (InstanceCall != nullptr)
		{
			auto CloneEmbedCallArgs = [&](
			                              const FBytecodeFunction& Target,
			                              std::vector<FValue>& OutCloned,
			                              const FValue*& OutArgs) -> bool
			{
				OutArgs = InstanceCall->Arguments;
				if (InstanceCall->ArgumentCount == 0)
				{
					return true;
				}
				if (InstanceCall->Arguments == nullptr)
				{
					ReportError("embed call has null argument pointer");
					return false;
				}
				OutCloned.resize(InstanceCall->ArgumentCount);
				std::vector<std::string> ParameterTypes;
				std::string ReturnType;
				(void)Runtime::ParseFunctionType(Target.ClosureTypeName, ParameterTypes, ReturnType);
				for (std::uint8_t Index = 0; Index < InstanceCall->ArgumentCount; ++Index)
				{
					const auto Flags = Index < Target.ParamFlags.size() ? Target.ParamFlags[Index] : 0;
					const bool Reference = (Flags & BytecodeParamFlagRef) != 0;
					if (Reference && (Flags & BytecodeParamFlagConstRef) != 0)
					{
						OutCloned[Index] = InstanceCall->Arguments[Index];
					}
					else if (Reference)
					{
						// Host ref/out staging is a representation snapshot. It must not
						// execute a language copy constructor merely to stage write-back.
						if (!Runtime::TryCloneValue(InstanceCall->Arguments[Index], OutCloned[Index]))
						{
							ReportError("failed to stage reference argument");
							return false;
						}
					}
					else if (!CopyLanguageValue(InstanceCall->Arguments[Index], OutCloned[Index], {}, false,
								 Index < ParameterTypes.size() ? ParameterTypes[Index] : std::string{}))
					{
						return false;
					}
				}
				OutArgs = OutCloned.data();
				return true;
			};
			auto ReleaseEmbedCallArgs = [&](std::vector<FValue>& Cloned)
			{
				for (FValue& Value : Cloned)
				{
					(void)ReleaseValue(Value, {});
				}
				Cloned.clear();
			};
			auto CoerceEmbedCallArgs = [&Module](
			                               const FBytecodeFunction& Target,
			                               std::vector<FValue>& InOutArguments,
			                               std::string& OutError) -> bool
			{
				OutError.clear();
				std::vector<std::string> ParamTypes;
				std::string ReturnType;
				(void)ModuleMeta::DecodeMethodParamTypeNames(Target.Name, ParamTypes);
				if (ParamTypes.size() != InOutArguments.size())
				{
					ParamTypes.clear();
					(void)Runtime::ParseFunctionType(
					    Target.ClosureTypeName, ParamTypes, ReturnType);
				}
				if (ParamTypes.size() != InOutArguments.size())
				{
					OutError = "script entry has invalid parameter type metadata: " + Target.Name;
					return false;
				}
				for (std::size_t Index = 0; Index < ParamTypes.size(); ++Index)
				{
					if (StandardLibrary::IsDiscreteArgument(Module, Target, Index) && !Runtime::IsIntegerValue(InOutArguments[Index]))
					{
						OutError = "standard-library count/base/timeout requires an integer argument";
						return false;
					}
					const std::string& Expected = ParamTypes[Index];
					if (!Runtime::IsPrimitiveValueTypeName(Expected))
					{
						continue;
					}
					const bool bReferenceParam = Index < Target.ParamFlags.size() && (Target.ParamFlags[Index] & (BytecodeParamFlagRef | BytecodeParamFlagConstRef)) != 0;
					if (bReferenceParam)
					{
						if (!Runtime::ValueMatchesPrimitiveTypeName(
						        InOutArguments[Index], Expected))
						{
							OutError = "reference argument type mismatch at index " + std::to_string(Index) + ": " + Target.Name;
							return false;
						}
						continue;
					}
					std::string ConversionError;
					if (!Runtime::CoerceValueToPrimitiveTypeName(
					        Expected, InOutArguments[Index], ConversionError))
					{
						OutError = "argument type mismatch at index " + std::to_string(Index) + ": " + Target.Name;
						if (!ConversionError.empty())
						{
							OutError += " (" + ConversionError + ')';
						}
						return false;
					}
				}
				return true;
			};
			// Writable `ref` parameter (ref bit set, const-ref bit clear).
			const auto IsWritableRefParam =
			    [](const FBytecodeFunction& Function, const std::size_t ParamIndex) -> bool
			{
				return ParamIndex < Function.ParamFlags.size() && (Function.ParamFlags[ParamIndex] & BytecodeParamFlagRef) != 0 && (Function.ParamFlags[ParamIndex] & BytecodeParamFlagConstRef) == 0;
			};
			/**
			 * Run a resolved embed entry call (free function or instance method).
			 * When the host requested argument write-back (OutArguments) and the target
			 * has writable `ref` parameters, the cloned ref arguments are staged in an
			 * anchor frame below the callee and passed as FLocalRef aliases — the same
			 * mechanism script-to-script `ref` calls use — so both in-place mutation and
			 * whole-value reassignment (`Param = T()`) are captured. The entry frame is
			 * forced through the interpreter in that case (JIT native rejects ref-param
			 * functions anyway); nested calls may still JIT. Nothing is written back on
			 * failure or exit.
			 */
			auto RunEmbedEntryCall = [&](
			                             const std::uint16_t FunctionIndex,
			                             const FObjectHandle& ThisObject,
			                             const bool bInterceptInstanceCall,
			                             bool& bOutExitRequested) -> bool
			{
				bOutExitRequested = false;
				if (FunctionIndex >= Module.Functions.size())
				{
					ReportError("invalid function index");
					return false;
				}
				const FBytecodeFunction& Target = Module.Functions[FunctionIndex];
				bool bWriteBackRefs = false;
				if (InstanceCall->OutArguments != nullptr)
				{
					for (std::uint8_t Index = 0; Index < InstanceCall->ArgumentCount; ++Index)
					{
						if (IsWritableRefParam(Target, Index))
						{
							bWriteBackRefs = true;
							break;
						}
					}
				}
				std::vector<FValue> ClonedArgs;
				const FValue* CallArgs = nullptr;
				if (!CloneEmbedCallArgs(Target, ClonedArgs, CallArgs))
				{
					ReleaseEmbedCallArgs(ClonedArgs);
					return false;
				}
				std::string ArgumentError;
				if (!CoerceEmbedCallArgs(Target, ClonedArgs, ArgumentError))
				{
					ReleaseEmbedCallArgs(ClonedArgs);
					ReportError(std::move(ArgumentError));
					return false;
				}
				CallArgs = ClonedArgs.empty() ? nullptr : ClonedArgs.data();
				std::size_t AnchorIndex = static_cast<std::size_t>(-1);
				std::vector<FValue> AliasArgs;
				if (bWriteBackRefs)
				{
					AnchorIndex = Frames.size();
					FCallFrame Anchor;
					Anchor.Function = nullptr;
					Anchor.InstructionPointer = 0;
					Anchor.StackBase = OperandStack.size();
					Anchor.Locals.assign(InstanceCall->ArgumentCount, FValue{std::int32_t{0}});
					AliasArgs.assign(InstanceCall->ArgumentCount, FValue{std::int32_t{0}});
					for (std::uint8_t Index = 0; Index < InstanceCall->ArgumentCount; ++Index)
					{
						if (IsWritableRefParam(Target, Index))
						{
							Anchor.Locals[Index] = std::move(ClonedArgs[Index]);
							ClonedArgs[Index] = std::int32_t{0};
							FLocalRef Ref;
							Ref.FrameIndex = static_cast<std::uint32_t>(AnchorIndex);
							Ref.Slot = static_cast<std::uint16_t>(Index);
							AliasArgs[Index] = Ref;
						}
						else
						{
							// Move so ClonedArgs does not keep a second owner.
							AliasArgs[Index] = std::move(ClonedArgs[Index]);
							ClonedArgs[Index] = std::int32_t{0};
						}
					}
					Frames.push_back(std::move(Anchor));
					CallArgs = AliasArgs.data();
				}
				const auto CleanupAnchor = [&]()
				{
					// Only pop when the anchor is still the top frame (normal return /
					// early failure). On exit, callee frames may sit above the
					// anchor; prefer CleanupAnchor while the anchor is alone so
					// remaining ref slots still go through ReleaseValue.
					if (AnchorIndex == static_cast<std::size_t>(-1) || Frames.size() != AnchorIndex + 1)
					{
						return;
					}
					(void)DiscardExecutionAbove(AnchorIndex, OperandStack.size(), {});
				};
				const auto ReleaseAliasArgOwners = [&](const bool bFrameOwnsCopies)
				{
					if (!bWriteBackRefs)
					{
						return;
					}
					// CallFunctionAt copies AliasArgs into the callee frame. By-value /
					// const-ref slots must not stay owned by AliasArgs, or frame
					// teardown sees use_count > 1 and skips script destructors.
					for (std::uint8_t Index = 0; Index < InstanceCall->ArgumentCount; ++Index)
					{
						if (IsWritableRefParam(Target, Index))
						{
							continue;
						}
						if (bFrameOwnsCopies)
						{
							AliasArgs[Index] = std::int32_t{0};
							continue;
						}
						FValue Orphan = std::move(AliasArgs[Index]);
						AliasArgs[Index] = std::int32_t{0};
						(void)ReleaseValue(Orphan, {});
					}
				};
				const std::size_t DepthBefore = Frames.size();
				const std::size_t StackBefore = OperandStack.size();
				const bool bCallOk =
				    bInterceptInstanceCall && InstanceCallInterceptor
				    ? InvokeResolvedInstanceMethod(
				          FunctionIndex,
				          ThisObject,
				          CallArgs,
				          InstanceCall->ArgumentCount,
				          {},
				          false,
				          /*bAllowJit=*/!bWriteBackRefs)
				    : CallFunctionAt(
				          FunctionIndex,
				          CallArgs,
				          InstanceCall->ArgumentCount,
				          ThisObject,
				          /*bAllowJit=*/!bWriteBackRefs);
				ReleaseAliasArgOwners(bCallOk);
				ReleaseEmbedCallArgs(ClonedArgs);
				if (!bCallOk || !JitFinishNestedCall(DepthBefore, StackBefore))
				{
					CleanupAnchor();
					return false;
				}
				if (bExitRequested)
				{
					// No write-back on exit; still release the anchor frame when intact.
					CleanupAnchor();
					bOutExitRequested = true;
					return true;
				}
				if (!ExportFunctionReturn(
				        Target,
				        Pop(),
				        InstanceCall->OutReturn))
				{
					CleanupAnchor();
					return false;
				}
				if (bWriteBackRefs)
				{
					if (InstanceCall->OutArguments == nullptr)
					{
						ReportError("ref argument write-back requires an output buffer");
						CleanupAnchor();
						return false;
					}
					for (std::uint8_t Index = 0; Index < InstanceCall->ArgumentCount; ++Index)
					{
						if (IsWritableRefParam(Target, Index))
						{
							FValue Next = std::move(Frames[AnchorIndex].Locals[Index]);
							Frames[AnchorIndex].Locals[Index] = std::int32_t{0};
							// AssignValue runs script destructors on the previous host slot
							// (raw FValue move-assign would skip them).
							if (!AssignValue(InstanceCall->OutArguments[Index], std::move(Next), {}))
							{
								CleanupAnchor();
								return false;
							}
							if (bExitRequested)
							{
								CleanupAnchor();
								bOutExitRequested = true;
								return true;
							}
						}
					}
				}
				CleanupAnchor();
				return !bHasError;
			};

			if (InstanceCall->Callable)
			{
				if (InstanceCall->ArgumentCount > 0 && InstanceCall->Arguments == nullptr)
				{
					ReportError("script callable has null argument pointer");
					return Finish(false);
				}
				std::vector<FValue> MutableArguments;
				MutableArguments.reserve(InstanceCall->ArgumentCount);
				for (std::uint8_t Index = 0; Index < InstanceCall->ArgumentCount; ++Index)
				{
					MutableArguments.push_back(InstanceCall->Arguments[Index]);
				}
				FValue ReturnValue = std::int32_t{0};
				std::string InvokeError;
				const bool bInvoked = InvokeCallable(
				    InstanceCall->Callable,
				    MutableArguments,
				    ReturnValue,
				    InvokeError);
				if (!bInvoked)
				{
					for (FValue& Value : MutableArguments)
					{
						(void)ReleaseValue(Value, {});
					}
					ReportError(InvokeError.empty()
					        ? "script callable invocation failed"
					        : std::move(InvokeError));
					return Finish(false);
				}
				const FBytecodeFunction& Target =
				    Module.Functions[InstanceCall->Callable->FunctionIndex];
				if (InstanceCall->OutArguments != nullptr)
				{
					for (std::uint8_t Index = 0; Index < InstanceCall->ArgumentCount; ++Index)
					{
						if (IsWritableRefParam(Target, Index) && !AssignValue(InstanceCall->OutArguments[Index], MutableArguments[Index], {}))
						{
							for (FValue& Value : MutableArguments)
							{
								(void)ReleaseValue(Value, {});
							}
							(void)ReleaseValue(ReturnValue, {});
							return Finish(false);
						}
					}
				}
				for (FValue& Value : MutableArguments)
				{
					(void)ReleaseValue(Value, {});
				}
				if (InstanceCall->OutReturn != nullptr && !Target.bReturnsVoid)
				{
					*InstanceCall->OutReturn = std::move(ReturnValue);
				}
				else
				{
					(void)ReleaseValue(ReturnValue, {});
				}
				if (bExitRequested)
				{
					return Finish(true);
				}
			}
			else if (InstanceCall->bResolvedFreeFunction)
			{
				if (InstanceCall->BoundFunctionIndex >= Module.Functions.size())
				{
					ReportError("resolved free function has an invalid identity");
					return Finish(false);
				}
				const FBytecodeFunction& Target =
				    Module.Functions[InstanceCall->BoundFunctionIndex];
				if (Target.bIsMethod || Target.ParamCount != InstanceCall->ArgumentCount)
				{
					ReportError("resolved free function identity no longer matches its call");
					return Finish(false);
				}
				bool bEntryExitRequested = false;
				if (!RunEmbedEntryCall(
				        InstanceCall->BoundFunctionIndex,
				        nullptr,
				        false,
				        bEntryExitRequested))
				{
					return Finish(false);
				}
				if (bEntryExitRequested)
				{
					return Finish(true);
				}
			}
			else if (InstanceCall->BoundFunctionIndex != static_cast<std::uint16_t>(-1))
			{
				if (!InstanceCall->Object || InstanceCall->BoundFunctionIndex >= Module.Functions.size())
				{
					ReportError("bound instance call has an invalid identity");
					return Finish(false);
				}
				const FBytecodeFunction& Target =
				    Module.Functions[InstanceCall->BoundFunctionIndex];
				if (!Target.bIsMethod || Target.ParamCount != 0)
				{
					ReportError("bound instance entry must be a zero-argument instance function");
					return Finish(false);
				}
				const std::size_t DepthBefore = Frames.size();
				const std::size_t StackBefore = OperandStack.size();
				if (!CallFunctionAt(
				        InstanceCall->BoundFunctionIndex,
				        nullptr,
				        0,
				        InstanceCall->Object) ||
				    !JitFinishNestedCall(DepthBefore, StackBefore))
				{
					return Finish(false);
				}
				if (bExitRequested)
				{
					return Finish(true);
				}
				FValue Discarded = Pop();
				(void)ReleaseValue(Discarded, {});
			}
			else if (!InstanceCall->ConstructTypeName.empty())
			{
				if (InstanceCall->OutConstructed == nullptr)
				{
					ReportError("ConstructObject missing output handle");
					return Finish(false);
				}
				std::uint16_t TypeIndex = static_cast<std::uint16_t>(-1);
				const std::string& Wanted = InstanceCall->ConstructTypeName;
				bool bAmbiguousType = false;
				if (!ModuleMeta::FindTypeIndex(Module, Wanted, TypeIndex, &bAmbiguousType))
				{
					ReportError(
					    bAmbiguousType
					        ? ("ambiguous type name: " + Wanted)
					        : ("unknown type: " + Wanted));
					return Finish(false);
				}
				if (InstanceCall->ArgumentCount > 0 && InstanceCall->Arguments == nullptr)
				{
					ReportError("ConstructObject has null argument pointer");
					return Finish(false);
				}
				std::uint16_t CtorIndex = static_cast<std::uint16_t>(-1);
				const EMethodResolveStatus CtorStatus = ModuleMeta::TryResolveConstructorOnType(
				    Module,
				    Module.Types[TypeIndex].Name,
				    InstanceCall->ArgumentCount,
				    CtorIndex,
				    InstanceCall->Arguments);
				if (CtorStatus == EMethodResolveStatus::Ambiguous)
				{
					ReportError(
					    "ambiguous constructor for type: " + Wanted + " with " + std::to_string(InstanceCall->ArgumentCount) + " argument(s)");
					return Finish(false);
				}
				const bool bHasCtor = CtorStatus == EMethodResolveStatus::Found;
				// A declared constructor suppresses the implicit storage fallback,
				// including a zero-argument call with no matching overload.
				const bool bHasDeclaredCtor = !bHasCtor && std::any_of(Module.Functions.begin(), Module.Functions.end(), [&](const FBytecodeFunction& Function)
				{
					return Function.bIsMethod && Function.OwnerTypeIndex == TypeIndex && MethodLinkageBareLeaf(Function.Name) == ModuleMeta::TypeSimpleName(Module.Types[TypeIndex].Name);
				});
				if (!bHasCtor && (InstanceCall->ArgumentCount > 0 || bHasDeclaredCtor))
				{
					ReportError(
					    "unknown constructor for type: " + Wanted + " with " + std::to_string(InstanceCall->ArgumentCount) + " argument(s)");
					return Finish(false);
				}
				if (bHasCtor && Module.Functions[CtorIndex].Access != EMemberAccess::Public)
				{
					ReportError("constructor is inaccessible: " + Wanted);
					return Finish(false);
				}
				if (bHasCtor && Module.Functions[CtorIndex].SpecialMemberKind == ESpecialMemberKind::CopyConstructor)
				{
					FValue Result;
					if (!CopyLanguageValue(InstanceCall->Arguments[0], Result, {}, true, Module.Types[TypeIndex].Name))
					{
						return Finish(false);
					}
					const auto* Object = std::get_if<FObjectHandle>(&Result);
					if (!Object || !*Object)
					{
						ReportError("invalid copy construction result");
						return Finish(false);
					}
					*InstanceCall->OutConstructed = *Object;
					return Finish(true);
				}
				FObjectHandle Object = CreateObject(TypeIndex, {}, bHasCtor);
				if (!Object) return Finish(false);
				if (bHasCtor)
				{
					std::vector<FValue> ClonedArgs;
					const FValue* CallArgs = nullptr;
					if (!CloneEmbedCallArgs(Module.Functions[CtorIndex], ClonedArgs, CallArgs))
					{
						ReleaseEmbedCallArgs(ClonedArgs);
						return Finish(false);
					}
					std::string ArgumentError;
					if (!CoerceEmbedCallArgs(
					        Module.Functions[CtorIndex], ClonedArgs, ArgumentError))
					{
						ReleaseEmbedCallArgs(ClonedArgs);
						ReportError(std::move(ArgumentError));
						return Finish(false);
					}
					CallArgs = ClonedArgs.empty() ? nullptr : ClonedArgs.data();
					const std::size_t DepthBefore = Frames.size();
					const std::size_t StackBefore = OperandStack.size();
					const bool bCallOk = CallFunctionAt(
					    CtorIndex,
					    CallArgs,
					    InstanceCall->ArgumentCount,
					    Object);
					ReleaseEmbedCallArgs(ClonedArgs);
					if (!bCallOk)
					{
						(void)DestroyObject(Object, {});
						return Finish(false);
					}
					if (!JitFinishNestedCall(DepthBefore, StackBefore))
					{
						(void)DestroyObject(Object, {});
						return Finish(false);
					}
					if (bExitRequested)
					{
						*InstanceCall->OutConstructed = std::move(Object);
						return Finish(true);
					}
					FValue Discarded = Pop();
					(void)ReleaseValue(Discarded, {});
				}
				*InstanceCall->OutConstructed = std::move(Object);
			}
			else if (InstanceCall->bFreeFunction)
			{
				if (InstanceCall->MethodName.empty())
				{
					ReportError("free function call requires a name");
					return Finish(false);
				}
				if (InstanceCall->ArgumentCount > 0 && InstanceCall->Arguments == nullptr)
				{
					ReportError("free function call has null argument pointer");
					return Finish(false);
				}
				std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
				const EMethodResolveStatus Status =
				    InstanceCall->FreeFunctionLookup && JitProgramOwner.get() == &Module
				    ? InstanceCall->FreeFunctionLookup->Resolve(JitProgramOwner, ProgramGeneration,
				          InstanceCall->MethodName, InstanceCall->ArgumentCount, FunctionIndex, InstanceCall->Arguments)
				    : ModuleMeta::TryResolveFreeFunction(Module, InstanceCall->MethodName,
				          InstanceCall->ArgumentCount, FunctionIndex, InstanceCall->Arguments);
				if (Status == EMethodResolveStatus::Ambiguous)
				{
					ReportError("ambiguous function: " + InstanceCall->MethodName);
					return Finish(false);
				}
				if (Status != EMethodResolveStatus::Found)
				{
					ReportError("unknown function: " + InstanceCall->MethodName);
					return Finish(false);
				}
				bool bEntryExitRequested = false;
				if (!RunEmbedEntryCall(
				        FunctionIndex,
				        nullptr,
				        false,
				        bEntryExitRequested))
				{
					return Finish(false);
				}
				if (bEntryExitRequested)
				{
					return Finish(true);
				}
			}
			else
			{
				if (!InstanceCall->Object)
				{
					ReportError("instance method call requires object");
					return Finish(false);
				}
				if (InstanceCall->ArgumentCount > 0 && InstanceCall->Arguments == nullptr)
				{
					ReportError("instance method call has null argument pointer");
					return Finish(false);
				}
				std::uint16_t FunctionIndex = static_cast<std::uint16_t>(-1);
				const EMethodResolveStatus MethodStatus =
				    ModuleMeta::TryResolveInstanceMethodOnType(
				        Module,
				        InstanceCall->Object->GetTypeName(),
				        InstanceCall->MethodName,
				        InstanceCall->ArgumentCount,
				        FunctionIndex,
				        InstanceCall->Arguments);
				if (MethodStatus == EMethodResolveStatus::Ambiguous)
				{
					ReportError("ambiguous method: " + InstanceCall->MethodName);
					return Finish(false);
				}
				if (MethodStatus != EMethodResolveStatus::Found)
				{
					ReportError("unknown method: " + InstanceCall->MethodName);
					return Finish(false);
				}
				bool bEntryExitRequested = false;
				if (!RunEmbedEntryCall(
				        FunctionIndex,
				        InstanceCall->Object,
				        !InstanceCall->bBypassEntryInterceptor,
				        bEntryExitRequested))
				{
					return Finish(false);
				}
				if (bEntryExitRequested)
				{
					return Finish(true);
				}
			}
		}
		else if (RunMode == ERunMode::Script && Module.MainFunctionIndex >= 0)
		{
			if (static_cast<std::size_t>(Module.MainFunctionIndex) >= Module.Functions.size())
			{
				ReportErrorId("diag.main_function_index_out_of_range");
				return Finish(false);
			}
			const std::uint16_t MainIndex =
			    static_cast<std::uint16_t>(Module.MainFunctionIndex);
			const FBytecodeFunction& MainFunction = Module.Functions[MainIndex];
			std::vector<std::string> MainParamTypes;
			std::string MainReturnType;
			if (!Runtime::ParseFunctionType(
			        MainFunction.ClosureTypeName,
			        MainParamTypes,
			        MainReturnType) ||
			    MainParamTypes.size() != MainFunction.ParamCount || MainFunction.ParamFlags.size() != MainFunction.ParamCount || (MainReturnType != "void" && MainReturnType != "int") || MainFunction.bReturnsVoid != (MainReturnType == "void"))
			{
				ReportError("main must return void or int", MainFunction.Location);
				return Finish(false);
			}
			if (MainFunction.ParamCount > 1)
			{
				ReportError("main must take at most one parameter", MainFunction.Location);
				return Finish(false);
			}
			std::vector<FValue> MainArguments;
			if (MainFunction.ParamCount == 1)
			{
				if (MainParamTypes[0] != "string[]" || MainFunction.ParamFlags[0] != 0)
				{
					ReportError("main parameter must be string[]", MainFunction.Location);
					return Finish(false);
				}
				MainArguments.push_back(Runtime::CreateScriptArgsArray(ScriptArgs));
			}
			if (!CallFunctionAt(MainIndex, MainArguments, nullptr))
			{
				return Finish(false);
			}
			if (!Run())
			{
				return Finish(false);
			}
			if (!bExitRequested)
			{
				if (OperandStack.empty())
				{
					ReportError("main produced no return value", MainFunction.Location);
					return Finish(false);
				}
				FValue MainReturn = Pop();
				if (MainReturnType == "int")
				{
					const std::int32_t* const ExitCode = std::get_if<std::int32_t>(&MainReturn);
					if (!ExitCode)
					{
						(void)ReleaseValue(MainReturn, MainFunction.Location);
						ReportError("int main returned a non-int value", MainFunction.Location);
						return Finish(false);
					}
					EntryExitCode = static_cast<int>(*ExitCode);
				}
				(void)ReleaseValue(MainReturn, MainFunction.Location);
			}
		}

		return Finish(!bHasError);
	}

	bool VeroScriptVm::TryExecuteOneShotInitJit(const FBytecodeModule& Module, bool& OutHandled)
	{
		OutHandled = false;
		if (HasExecutionGovernance() || !JitBackendFactory || GetEffectiveExecutionBackend() == EExecutionBackend::Vm || Module.InitCode.empty() || Module.Functions.size() >= 65535)
		{
			return true;
		}
		try
		{
			auto EvalOwner = std::make_shared<FBytecodeModule>(Module);
			auto& EvalModule = *EvalOwner;
			FBytecodeFunction InitFunction;
			InitFunction.Name = "<eval-init>";
			InitFunction.ModulePath = EvalModule.EntryModulePath;
			InitFunction.bReturnsVoid = true;
			InitFunction.ParamCount = 0;
			InitFunction.LocalCount = EvalModule.InitLocalCount;
			InitFunction.Code = EvalModule.InitCode;
			// Return consumes an operand in the interpreter even for a void body.
			// InitCode has no return operand, so synthesize a discarded scalar value.
			InitFunction.Code.push_back(static_cast<std::uint8_t>(EOpcode::ConstBool));
			InitFunction.Code.push_back(0);
			InitFunction.Code.push_back(static_cast<std::uint8_t>(EOpcode::Return));
			InitFunction.DebugLines = EvalModule.InitDebugLines;
			FDebugLine ReturnDebugLine;
			for (auto It = InitFunction.DebugLines.rbegin();
			    It != InitFunction.DebugLines.rend();
			    ++It)
			{
				if (It->Line != 0)
				{
					ReturnDebugLine = *It;
					break;
				}
			}
			InitFunction.DebugLines.resize(InitFunction.Code.size());
			InitFunction.DebugLines.back() = ReturnDebugLine;
			InitFunction.ClosureTypeName = "fn()->void";
			InitFunction.Location.FileLabel = EvalModule.EntryModulePath.empty()
			    ? std::string_view{"<eval>"}
			    : std::string_view{EvalModule.EntryModulePath};

			const std::uint16_t InitFunctionIndex =
			    static_cast<std::uint16_t>(EvalModule.Functions.size());
			EvalModule.Functions.push_back(std::move(InitFunction));

			FJitPolicy EvalPolicy = JitPolicy;
			const auto SavedCache = BackendCache;
			const auto Used = SavedCache ? SavedCache->GetResourceUsage() : FJitResourceUsage{};
			const auto Previous = SavedCache ? SavedCache->GetStats() : FJitStats{};
			if (EvalPolicy.MaxCompileAttempts != 0)
			{
				if (Previous.CompileAttempts >= EvalPolicy.MaxCompileAttempts) return true;
				EvalPolicy.MaxCompileAttempts -= Previous.CompileAttempts;
			}
			if (Used.ExecutableBytes >= JitResourceLimits.ExecutableBytes || Used.RetainedBytes >= JitResourceLimits.RetainedBytes)
			{
				return true;
			}
			const FJitResourceLimits Limits{JitResourceLimits.ExecutableBytes - Used.ExecutableBytes,
			    JitResourceLimits.RetainedBytes - Used.RetainedBytes};
			if (FJitBackendCache::MinimumRetainedBytes(EvalModule.Functions.size()) > Limits.RetainedBytes) return true;
			std::string ErrorMessage;
			FJitProgramView Program{EvalOwner.get(), EvalOwner,
			    {AllocateJitProgramIdentity(), ProgramGeneration, JitNativeBindingsGeneration}};
			auto EvalCache = FJitBackendCache::Create(JitBackendFactory, std::move(Program), EvalPolicy, Limits,
			    JitFrameServiceCapability | JitValueServiceCapability | JitCallServiceCapability | JitScalarServiceCapability, ErrorMessage);
			if (!EvalCache)
			{
				OutHandled = true;
				ReportError(ErrorMessage);
				return false;
			}
			EvalCache->BeginRun();
			const auto Result = EvalCache->EnsureCompiled(InitFunctionIndex);
			if (Result == EJitCompilationResult::Failed)
			{
				OutHandled = true;
				ReportError(std::string(EvalCache->GetLastError()));
				return false;
			}
			if (Result != EJitCompilationResult::Ready)
			{
				if (SavedCache)
				{
					SavedCache->AccumulateTransientStatistics(EvalCache->GetStats());
				}
				return true;
			}
			const FBytecodeModule* const SavedModule = ActiveModule;
			ActiveModule = &EvalModule;
			BackendCache = std::move(EvalCache);
			const auto InvocationCache = BackendCache;
			const auto Restore = [&]() noexcept
			{
				// Host callbacks may invalidate the cache while this invocation is live.
				// Retain its own lease for cleanup, and never undo that invalidation by
				// restoring the outer generation or dereference the cleared VM cache.
				if (BackendCache == InvocationCache)
				{
					if (SavedCache)
					{
						SavedCache->AccumulateTransientStatistics(InvocationCache->GetStats());
					}
					BackendCache = SavedCache;
				}
				ActiveModule = SavedModule;
				// Helpers may have populated caches with addresses from EvalModule.
				ResetModuleLookupCaches();
			};
			bool Ok = false;
			try
			{
				OutHandled = TryDispatchBackendCall(InitFunctionIndex, nullptr, 0, {}, Ok);
			}
			catch (...)
			{
				Restore();
				throw;
			}
			Restore();
			return !OutHandled || Ok;
		}
		catch (const std::bad_alloc&)
		{
			OutHandled = true;
			ReportError("out of memory during one-shot evaluation");
			return false;
		}
	}

	bool VeroScriptVm::PrepareExecutionRequest(const FHotReloadState* const PersistentState)
	{
		if (PersistentState && PersistentState->bExecutionResetRequired)
		{
			MarkExecutionResetRequired();
		}
		if (RejectExecutionReuse()) return false;
		if (!ActiveModule && Frames.empty() && !bBorrowedExecutionCleanup)
		{
			// A completed interruption belongs to the preceding request. Retire it
			// before validation can report an independent failure as a cleanup error.
			ExecutionCleanup.reset();
			bHasError = false;
			Error = {};
		}
		return true;
	}
}
