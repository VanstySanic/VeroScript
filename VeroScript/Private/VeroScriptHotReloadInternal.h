// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"
#include "ExecutionSupport/VeroScriptLocalStaticInternal.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptValue.h"

#include <memory>
#include <string>
#include <vector>

namespace VeroScript
{
	struct FHotReloadLocalStaticSchema
	{
		std::string GlobalName;
		std::uint16_t GlobalSlot = 0;
	};

	struct FHotReloadFunctionSchema
	{
		std::string Name;
		std::string ClosureTypeName;
		std::uint8_t ParamCount = 0;
		std::vector<FBytecodeCapture> Captures;
		std::uint64_t LocalStaticSemanticIdentity = 0;
		std::vector<FHotReloadLocalStaticSchema> LocalStatics;

		FHotReloadFunctionSchema(const FBytecodeFunction& Function, const FBytecodeModule& Module)
			: Name(Function.Name)
			, ClosureTypeName(Function.ClosureTypeName)
			, ParamCount(Function.ParamCount)
			, Captures(Function.Captures)
			, LocalStaticSemanticIdentity(Function.LocalStaticSemanticIdentity)
		{
			for (const auto& Initializer : Function.LocalStaticInitializers)
			{
				LocalStatics.push_back({Module.GlobalNames.at(Initializer.GlobalSlot), Initializer.GlobalSlot});
			}
		}
	};

	/**
	 * Runtime state retained by an opt-in hot-reload session.
	 *
	 * Values keep their shared object/container identity between runs. Schema is a
	 * lightweight copy of the module metadata needed to migrate field layouts and
	 * closure function indices after the bytecode module is replaced.
	 */
	struct FHotReloadState
	{
		bool Empty() const
		{
			return Globals.empty() && ExternalRoots.empty();
		}

		void Clear()
		{
			GlobalNames.clear();
			GlobalTypeNames.clear();
			Globals.clear();
			ExternalRoots.clear();
			LocalStatics.reset();
			Types.clear();
			Functions.clear();
			WeakReferences.reset();
			InvalidateCapturedModule();
			ProgramGeneration = 1;
			bNeedsModuleInitialization = true;
			bExecutionResetRequired = false;
		}

		void InvalidateCapturedModule() noexcept
		{
			CapturedModule.reset();
			CapturedModuleAddress = nullptr;
			bCapturedGlobalsInSlotOrder = false;
		}

		bool HasModuleSchema(const FBytecodeModule& Module,
		    const std::shared_ptr<const FBytecodeModule>& Owner, std::uint64_t Generation) const noexcept
		{
			return ProgramGeneration == Generation && Owner.get() == &Module &&
			    !Owner.owner_before(CapturedModule) &&
			    !CapturedModule.owner_before(Owner) && CapturedModuleAddress == &Module;
		}

		bool HasCapturedModule(const FBytecodeModule& Module,
		    const std::shared_ptr<const FBytecodeModule>& Owner, std::uint64_t Generation) const noexcept
		{
			return !bNeedsModuleInitialization && HasModuleSchema(Module, Owner, Generation);
		}

		// Record ownership after the type and function schemas have been populated.
		void RememberModuleSchema(const FBytecodeModule& Module,
		    const std::shared_ptr<const FBytecodeModule>& Owner) noexcept
		{
			if (Owner.get() == &Module && Owner.use_count() != 0)
			{
				CapturedModule = Owner;
				CapturedModuleAddress = &Module;
			}
		}

		std::vector<std::string> GlobalNames;
		std::vector<std::string> GlobalTypeNames;
		std::vector<FValue> Globals;
		// Exact explicitly registered host roots, pinned only until settlement.
		std::vector<FValue> ExternalRoots;
		std::shared_ptr<FLocalStaticState> LocalStatics;
		std::vector<FBytecodeTypeDesc> Types;
		std::vector<FHotReloadFunctionSchema> Functions;
		std::uint64_t ProgramGeneration = 1;

		// New/reloaded modules run global initialization once before state restore.
		bool bNeedsModuleInitialization = true;
		bool bExecutionResetRequired = false;

		// Opaque VM-owned weak slots share the lifetime of persistent script values.
		std::shared_ptr<void> WeakReferences;
		// Identify an immutable, owned generation without keeping obsolete bytecode alive.
		std::weak_ptr<const FBytecodeModule> CapturedModule;
		const FBytecodeModule* CapturedModuleAddress = nullptr;
		bool bCapturedGlobalsInSlotOrder = false;

	};

	struct FHotReloadObjectUndo
	{
		FScriptObject* Object = nullptr;
		std::vector<FValue> Fields;
		bool bDestroying = false;
		bool bDestroyed = false;
	};

	struct FHotReloadFunctionUndo
	{
		FFunctionValue* Function = nullptr;
		std::uint16_t FunctionIndex = 0;
		std::uint64_t ProgramGeneration = 1;
		std::vector<FValue> Captures;
	};

	template <typename TValue>
	struct THotReloadHandleUndo
	{
		TValue* Target = nullptr;
		TValue Value;
	};

	/**
	 * One-shot undo journal retained until the new module completes initialization.
	 * It intentionally shares object handles with PreviousState; object/closure
	 * mutations are restored in place so host-visible script identity is preserved.
	 */
	struct FHotReloadRollback
	{
		bool Empty() const
		{
			return PreviousState.Empty() && Objects.empty() && Functions.empty()
				&& DynamicArrays.empty() && Maps.empty() && Sets.empty()
				&& Queues.empty() && Stacks.empty() && OrderedMaps.empty()
				&& Deques.empty();
		}

		void Clear()
		{
			PreviousState.Clear();
			Objects.clear();
			Functions.clear();
			DynamicArrays.clear();
			Maps.clear();
			Sets.clear();
			Queues.clear();
			Stacks.clear();
			OrderedMaps.clear();
			Deques.clear();
		}

		FHotReloadState PreviousState;
		std::vector<FHotReloadObjectUndo> Objects;
		std::vector<FHotReloadFunctionUndo> Functions;
		std::vector<THotReloadHandleUndo<FDynamicArray>> DynamicArrays;
		std::vector<THotReloadHandleUndo<FMap>> Maps;
		std::vector<THotReloadHandleUndo<FSet>> Sets;
		std::vector<THotReloadHandleUndo<FQueue>> Queues;
		std::vector<THotReloadHandleUndo<FStack>> Stacks;
		std::vector<THotReloadHandleUndo<FOrderedMap>> OrderedMaps;
		std::vector<THotReloadHandleUndo<FDeque>> Deques;

	};

	/**
	 * Transactionally migrate State to NewModule.
	 *
	 * Existing object and container handles retain identity. Fields are matched by
	 * name, removed fields are released, and new fields receive language defaults.
	 * The function performs a complete validation/planning pass before mutating any
	 * retained object. On failure State remains unchanged.
	 *
	 * HostBindings (optional) lets script-enum-style int fields typed as host enums
	 * migrate and default correctly; host enums are not stored in Module.Enums.
	 */
	bool MigrateHotReloadState(
		FHotReloadState& State,
		const FBytecodeModule& NewModule,
		FHotReloadRollback* OutRollback,
		std::string& OutError,
		const IHostBindingCatalog* HostBindings = nullptr,
		std::uint64_t NewProgramGeneration = 0,
		std::shared_ptr<const void> ProgramOwner = {});

	// Build the replacement slots used while restored objects receive cleanup callbacks.
	bool BuildHotReloadFieldSlots(
		const FHotReloadState& PreviousState,
		const FBytecodeModule& Module,
		const FScriptObject& Object,
		std::vector<std::size_t>& PreviousSlots,
		std::size_t& PreviousFieldCount,
		std::string& OutError);

	bool BuildHotReloadCleanupFields(
		const FHotReloadState& PreviousState,
		const FBytecodeModule& Module,
		const FScriptObject& Object,
		std::vector<std::size_t>& PreviousSlots,
		std::vector<FValue>& Fields,
		std::string& OutError,
		const IHostBindingCatalog* HostBindings,
		std::shared_ptr<const void> ProgramOwner);

	bool BuildHotReloadCaptureSlots(
		const FHotReloadFunctionSchema& Previous,
		const FBytecodeFunction& Replacement,
		std::vector<std::size_t>& PreviousSlots,
		const std::string& Path,
		std::string& OutError);

	// Capture module schema and move live global values into State.
	void CaptureHotReloadState(
		FHotReloadState& State,
		const FBytecodeModule& Module,
		std::vector<FValue>& Globals,
		std::uint64_t ProgramGeneration,
		const std::shared_ptr<const FBytecodeModule>& ModuleOwner);
}
