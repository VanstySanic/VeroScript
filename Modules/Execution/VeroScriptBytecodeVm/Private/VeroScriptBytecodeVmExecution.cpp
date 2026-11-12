// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptBytecodeVmExecution.h"
#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptVmCleanup.h"
#include "VeroScriptVmLocalExpressions.h"
#include "VeroScriptVmIndexedExpressions.h"
#include "VeroScriptVmInternal.h"
#include "VeroScriptVmStringLocals.h"
#include "VeroScriptVmArguments.h"
#include "VeroScriptVariant.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptStandardLibrary.h"
#include <algorithm>
#include <array>
#include <optional>
#include <limits>
#include <cmath>

namespace VeroScript
{
	namespace
	{
		constexpr auto InstructionByteSizes = []()
		{
			std::array<std::uint8_t, 256> Sizes{};
			for (std::size_t Index = 0; Index < Sizes.size(); ++Index)
			{
				Sizes[Index] = static_cast<std::uint8_t>(Bytecode::InstructionSize(static_cast<EOpcode>(Index)));
			}
			return Sizes;
		}();

		bool TryReadLocalScalarIndex(const FValue& Container, const FValue& Key, FValue& Out)
		{
			if (const auto* Array = std::get_if<FDynamicArrayHandle>(&Container); Array && *Array)
			{
				const auto* Index = std::get_if<std::int32_t>(&Key);
				// Closure reference cells also use the dynamic-array handle tag,
				// but always carry their binding in boxed storage.
				return (*Array)->Elements.empty() && Index && *Index >= 0 &&
				    (*Array)->TryGetScalar(static_cast<std::size_t>(*Index), Out);
			}
			const auto* Map = std::get_if<FMapHandle>(&Container);
			if (!Map || !*Map || (*Map)->ScalarEntries.index() != 0 || (*Map)->ValueTypeName != "int") return false;
			const FValue* Stored = nullptr;
			if (const auto* Integer = std::get_if<std::int32_t>(&Key); Integer && (*Map)->KeyTypeName == "int")
			{
				const auto Entry = (*Map)->IntEntries.find(*Integer);
				if (Entry != (*Map)->IntEntries.end())
				{
					Stored = &Entry->second;
				}
			}
			else if (const auto* Text = std::get_if<std::string>(&Key); Text && (*Map)->KeyTypeName == "string")
			{
				const auto Entry = (*Map)->StrEntries.find(*Text);
				if (Entry != (*Map)->StrEntries.end())
				{
					Stored = &Entry->second;
				}
			}
			const auto* Scalar = Stored ? std::get_if<std::int32_t>(Stored) : nullptr;
			if (!Scalar) return false;
			Out = *Scalar;
			return true;
		}

		template <bool bAllowInsert = true>
		bool TryWritePlainIntIndex(FValue& Container, const FValue& Key, std::int32_t Value)
		{
			constexpr std::uint16_t NoField = static_cast<std::uint16_t>(-1);
			if (const auto* Array = std::get_if<FDynamicArrayHandle>(&Container); Array && *Array)
			{
				if ((*Array)->MutationOrigin.FieldIndex != NoField || (*Array)->ElementTypeName != "int") return false;
				const auto* Index = std::get_if<std::int32_t>(&Key);
				if (!Index || *Index < 0) return false;
				auto* Elements = std::get_if<std::vector<std::int32_t>>(&(*Array)->ScalarElements);
				if (!Elements || static_cast<std::size_t>(*Index) >= Elements->size()) return false;
				(*Elements)[static_cast<std::size_t>(*Index)] = Value;
				return true;
			}
			const auto* Map = std::get_if<FMapHandle>(&Container);
			if (!Map || !*Map || (*Map)->MutationOrigin.FieldIndex != NoField ||
				(*Map)->ScalarEntries.index() != 0 || (*Map)->ValueTypeName != "int")
			{
				return false;
			}
			const auto Assign = [Value](auto& Entries, const auto& TypedKey)
			{
				// The canonical path can invoke a destructor when replacing an
				// existing owning value. Only publish direct integer stores here.
				auto Entry = Entries.end();
				if constexpr (bAllowInsert)
				{
					Entry = Entries.try_emplace(TypedKey, Value).first;
				}
				else
				{
					Entry = Entries.find(TypedKey);
					if (Entry == Entries.end()) return false;
				}
				auto* Destination = std::get_if<std::int32_t>(&Entry->second);
				if (!Destination) return false;
				*Destination = Value;
				return true;
			};
			if (const auto* Integer = std::get_if<std::int32_t>(&Key); Integer && (*Map)->KeyTypeName == "int")
			{
				return Assign((*Map)->IntEntries, *Integer);
			}
			if (const auto* Text = std::get_if<std::string>(&Key); Text && (*Map)->KeyTypeName == "string")
			{
				return Assign((*Map)->StrEntries, *Text);
			}
			return false;
		}

		bool TryWriteLocalIntIndex(const std::vector<std::uint8_t>& Code, std::size_t& Next,
		    std::vector<FValue>& Locals, std::uint16_t ContainerSlot, std::uint16_t KeySlot)
		{
			// Assignment statements save the RHS in a temporary, store the
			// returned container into its original slot, then discard the RHS.
			constexpr auto LocalSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
			constexpr auto SetOffset = 4u * LocalSize;
			constexpr auto ContainerStore = SetOffset + Bytecode::InstructionSize(EOpcode::SetIndex);
			constexpr auto ResultLoad = ContainerStore + LocalSize;
			constexpr auto PopOffset = ResultLoad + LocalSize;
			constexpr auto TailSize = PopOffset + Bytecode::InstructionSize(EOpcode::Pop);
			if (Code.size() - Next < TailSize) return false;
			const auto* const Tail = Code.data() + Next;
			if (static_cast<EOpcode>(Tail[LocalSize]) != EOpcode::LoadLocal ||
				static_cast<EOpcode>(Tail[2u * LocalSize]) != EOpcode::StoreLocal ||
				static_cast<EOpcode>(Tail[3u * LocalSize]) != EOpcode::LoadLocal ||
				static_cast<EOpcode>(Tail[SetOffset]) != EOpcode::SetIndex ||
				static_cast<EOpcode>(Tail[ContainerStore]) != EOpcode::StoreLocal ||
				Bytecode::ReadU16LE(Tail + ContainerStore + 1u) != ContainerSlot ||
				static_cast<EOpcode>(Tail[ResultLoad]) != EOpcode::LoadLocal ||
				static_cast<EOpcode>(Tail[PopOffset]) != EOpcode::Pop)
			{
				return false;
			}
			const auto ValueSlot = Bytecode::ReadU16LE(Tail + LocalSize + 1u);
			const auto TempSlot = Bytecode::ReadU16LE(Tail + 2u * LocalSize + 1u);
			if (ValueSlot >= Locals.size() || TempSlot >= Locals.size() || TempSlot == KeySlot ||
				Bytecode::ReadU16LE(Tail + 3u * LocalSize + 1u) != TempSlot ||
				Bytecode::ReadU16LE(Tail + ResultLoad + 1u) != TempSlot)
			{
				return false;
			}
			const auto* Value = std::get_if<std::int32_t>(&Locals[ValueSlot]);
			auto* Temp = std::get_if<std::int32_t>(&Locals[TempSlot]);
			const auto* Array = std::get_if<FDynamicArrayHandle>(&Locals[ContainerSlot]);
			// An insertion can allocate. Keep its original temporary and store
			// ordering on the canonical path.
			if (!Value || !Temp || (Array && (!*Array || !(*Array)->Elements.empty())) ||
				!TryWritePlainIntIndex<false>(Locals[ContainerSlot], Locals[KeySlot], *Value))
			{
				return false;
			}
			*Temp = *Value;
			Next += TailSize;
			return true;
		}

		// Keep the metered evaluator out of the general instruction dispatcher.
#if defined(_MSC_VER)
		__declspec(noinline)
#elif defined(__GNUC__)
		__attribute__((noinline))
#endif
		bool TryFuseMeteredLocalArithmetic(VmLocalExpressions::FCache& Expressions,
		    const FVmExecutionView& State, FVmCallFrame& Frame, std::int32_t First,
		    std::size_t MaxStackDepth)
		{
			const auto& Function = *Frame.Function;
			const auto Next = Frame.InstructionPointer;
			const auto StackSize = Frame.StackBase;
			if (State.GetExecutionCleanup() || StackSize > MaxStackDepth) return false;
			const auto& Module = *State.GetActiveModule();
			const auto Steps = Expressions.GetInt32TailSteps(Module, Function, Next);
			auto& Governance = State.GetExecutionGovernance();
			auto Budget = Governance;
			// Charge only after the side-effect-free probe succeeds. On a quota
			// boundary or failed probe the ordinary VM visits each remaining step.
			if (!Steps || !Budget.Charge(Steps, State.GetExecutionLimits().MaxInstructions) ||
				!Expressions.TryExecute(First, Module, Function, Frame.InstructionPointer,
					Frame.Locals, MaxStackDepth - StackSize))
			{
				return false;
			}
			Governance.ExecutedInstructions = Budget.ExecutedInstructions;
			return true;
		}
	}

	// Every successful hot fusion uses this small bookkeeping step.
#if defined(_MSC_VER)
	__forceinline
#elif defined(__GNUC__)
	__attribute__((always_inline)) inline
#endif
	void FBytecodeVmExecution::CompleteFusion(FCallFrame& Frame, const EOpcode LastOpcode) noexcept
	{
		State.GetActiveInstructionStart() = Frame.InstructionPointer - InstructionByteSizes[static_cast<std::uint8_t>(LastOpcode)];
		if (Frame.Function && !Frame.Function->ExceptionRegions.empty())
		{
			Frame.LastInstructionOffset = State.GetActiveInstructionStart();
		}
	}

	// Keep the container lookahead outside the scalar instruction dispatcher.
#if defined(_MSC_VER)
	__declspec(noinline)
#elif defined(__GNUC__)
	__attribute__((noinline))
#endif
	bool FBytecodeVmExecution::TryFuseLocalScalarIndex(const std::vector<std::uint8_t>& Code,
	    FCallFrame& Frame, const std::uint16_t Slot)
	{
		const auto Next = Frame.InstructionPointer;
		constexpr auto LoadSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
		constexpr auto TailSize = LoadSize + Bytecode::InstructionSize(EOpcode::GetIndexCopy);
		const auto StackSize = State.GetOperandStack().size();
		if (Code.size() - Next < TailSize || static_cast<EOpcode>(Code[Next]) != EOpcode::LoadLocal ||
			StackSize > MaxOperandStackDepth - 2u)
		{
			return false;
		}
		const auto KeySlot = Bytecode::ReadU16LE(Code.data() + Next + 1u);
		if (KeySlot >= Frame.Locals.size()) return false;
		if (static_cast<EOpcode>(Code[Next + LoadSize]) == EOpcode::GetIndexCopy)
		{
			FValue Result;
			if (!TryReadLocalScalarIndex(Frame.Locals[Slot], Frame.Locals[KeySlot], Result)) return false;
			Frame.InstructionPointer += TailSize;
			CompleteFusion(Frame, EOpcode::GetIndexCopy);
			Push(std::move(Result));
			return true;
		}
		if (StackSize > MaxOperandStackDepth - 3u || State.IsCopyingLanguageValue() ||
			!State.GetActiveContainerCopyReads().empty() ||
			!TryWriteLocalIntIndex(Code, Frame.InstructionPointer, Frame.Locals, Slot, KeySlot))
		{
			return false;
		}
		CompleteFusion(Frame, EOpcode::Pop);
		return true;
	}

	// Keep string lookahead and result materialization out of the dispatch
	// function so unrelated scalar loops retain their register allocation.
	template <bool bAllowWideAppend>
#if defined(_MSC_VER)
	__declspec(noinline)
#if defined(_M_X64)
	__declspec(code_seg(".vstext"))
#endif
#elif defined(__GNUC__)
	__attribute__((noinline))
#endif
	FBytecodeVmExecution::ETextReadFusion FBytecodeVmExecution::TryFuseTextRead(FValue& Value, FCallFrame& Frame,
	    const std::vector<std::uint8_t>& Code, const bool AllowWrapper)
	{
		const auto StackSize = State.GetOperandStack().size();
		const auto StackRoom = StackSize <= MaxOperandStackDepth ? MaxOperandStackDepth - StackSize : 0u;
		std::int32_t Character = 0;
		bool Wrapper = false;
		auto Next = Frame.InstructionPointer;
		if (VmStringLocals::TryFuseCharCode(Value, *State.GetActiveModule(), Code,
		        Next, Frame.Locals, StackRoom, AllowWrapper, Character, Wrapper))
		{
			if (Wrapper && (State.GetInstanceCallInterceptor() || !State.CanEnterAdditionalCall())) return ETextReadFusion::None;
			Frame.InstructionPointer = Next;
			CompleteFusion(Frame, Wrapper ? EOpcode::Call : EOpcode::CallBuiltin);
			Push(Character);
			return State.GetErrorFlag() ? ETextReadFusion::Failed : ETextReadFusion::Complete;
		}
		if constexpr (bAllowWideAppend)
		{
			Next = Frame.InstructionPointer;
			if (Code.size() - Next >= Bytecode::InstructionSize(EOpcode::LoadLocal) &&
				static_cast<EOpcode>(Code[Next]) == EOpcode::LoadLocal && std::holds_alternative<std::string>(Value))
			{
				const auto Source = Bytecode::ReadU16LE(Code.data() + Next + 1u);
				if (Source < Frame.Locals.size() && std::holds_alternative<std::u16string>(Frame.Locals[Source]))
				{
					return TryFuseWideTextAppend(Value, Frame, Code, Source);
				}
			}
		}
		return ETextReadFusion::None;
	}

#if defined(_MSC_VER)
	__declspec(noinline)
#if defined(_M_X64)
	__declspec(code_seg(".vstext"))
#endif
#elif defined(__GNUC__)
	__attribute__((noinline))
#endif
	FBytecodeVmExecution::ETextReadFusion FBytecodeVmExecution::TryFuseWideTextAppend(
		FValue& Value, FCallFrame& Frame, const std::vector<std::uint8_t>& Code, const std::uint16_t SourceSlot)
	{
		const auto Next = Frame.InstructionPointer;
		constexpr auto CallOffset = Bytecode::InstructionSize(EOpcode::LoadLocal);
		constexpr auto AddOffset = CallOffset + Bytecode::InstructionSize(EOpcode::CallBuiltin);
		constexpr auto StoreOffset = AddOffset + Bytecode::InstructionSize(EOpcode::Add);
		constexpr auto TailSize = StoreOffset + Bytecode::InstructionSize(EOpcode::StoreLocal);
		const auto Size = State.GetOperandStack().size();
		if (Next > Code.size() || Code.size() - Next < TailSize || Size > MaxOperandStackDepth - 2u ||
			SourceSlot >= Frame.Locals.size() || !std::holds_alternative<std::string>(Value) ||
			!std::holds_alternative<std::u16string>(Frame.Locals[SourceSlot]) ||
			static_cast<EOpcode>(Code[Next]) != EOpcode::LoadLocal || Bytecode::ReadU16LE(Code.data() + Next + 1u) != SourceSlot ||
			static_cast<EOpcode>(Code[Next + CallOffset]) != EOpcode::CallBuiltin ||
			static_cast<EBuiltinId>(Code[Next + CallOffset + 1u]) != EBuiltinId::ToString || Code[Next + CallOffset + 2u] != 1u ||
			static_cast<EOpcode>(Code[Next + AddOffset]) != EOpcode::Add ||
			static_cast<EOpcode>(Code[Next + StoreOffset]) != EOpcode::StoreLocal ||
			Bytecode::ReadU16LE(Code.data() + Next + StoreOffset + 1u) >= Frame.Locals.size() ||
			&Frame.Locals[Bytecode::ReadU16LE(Code.data() + Next + StoreOffset + 1u)] != &Value)
		{
			return ETextReadFusion::None;
		}
		// Exact primitive conversion cannot reenter script or change frame/local
		// ownership. Preserve its original snapshot and canonical UTF-16 codec.
		const auto SourceUnits = std::get<std::u16string>(Frame.Locals[SourceSlot]).size();
		State.GetActiveInstructionStart() = Frame.LastInstructionOffset = Next;
		Frame.InstructionPointer = Next + CallOffset;
		Push(Frame.Locals[SourceSlot]);
		if (State.GetErrorFlag()) return ETextReadFusion::Failed;
		State.GetActiveInstructionStart() = Frame.LastInstructionOffset = Next + CallOffset;
		Frame.InstructionPointer = Next + AddOffset;
		if (!Services.CallBuiltin(EBuiltinId::ToString, 1u)) return ETextReadFusion::Failed;
		auto Suffix = Pop();
		if (State.GetErrorFlag()) return ETextReadFusion::Failed;
		State.GetActiveInstructionStart() = Frame.LastInstructionOffset = Next + AddOffset;
		Frame.InstructionPointer = Next + StoreOffset;
		auto& Target = std::get<std::string>(Value);
		auto& Converted = std::get<std::string>(Suffix);
		static_assert(std::is_nothrow_move_assignable_v<std::string>);
		// Equal unit/byte lengths prove ASCII (including NUL). Do not retain a
		// codec's multibyte growth overcapacity, or replace a sufficient reserve.
		if (Target.empty() && Target.capacity() < Converted.size() && Converted.size() == SourceUnits)
		{
			Target = std::move(Converted);
		}
		else
		{
			Target.append(Converted);
		}
		Frame.InstructionPointer = Next + TailSize;
		CompleteFusion(Frame, EOpcode::StoreLocal);
		return ETextReadFusion::Complete;
	}

#if defined(_MSC_VER)
	__declspec(noinline)
#if defined(_M_X64)
	__declspec(code_seg(".vstext"))
#endif
#elif defined(__GNUC__)
	__attribute__((noinline))
#endif
	bool FBytecodeVmExecution::TryFuseTextIdentity(const FValue& Value, FCallFrame& Frame,
	    const std::vector<std::uint8_t>& Code, const std::uint16_t Slot)
	{
		if (State.GetInstanceCallInterceptor() || !State.CanEnterAdditionalCall()) return false;
		const auto StackSize = State.GetOperandStack().size();
		const auto StackRoom = StackSize <= MaxOperandStackDepth ? MaxOperandStackDepth - StackSize : 0u;
		bool IdentityInPlace = false;
		if (!VmStringLocals::TryFuseIdentity(*State.GetActiveModule(), Code,
				Frame.InstructionPointer, Slot, StackRoom, IdentityInPlace))
		{
			return false;
		}
		// A by-value result still owns its one required snapshot. A self assignment
		// cannot observe an unchanged local store.
		if (!IdentityInPlace)
		{
			Push(Value);
		}
		CompleteFusion(Frame, IdentityInPlace ? EOpcode::StoreLocal : EOpcode::Call);
		return true;
	}

	template <bool bInstrumented, bool bGoverned, bool bBatchArithmetic, bool bGlobalInitializing>
	bool FBytecodeVmExecution::RunLoop()
	{
#define VEROSCRIPT_VM_ENHANCED_DISPATCH 0
#include "VeroScriptBytecodeVmDispatch.inl"
#undef VEROSCRIPT_VM_ENHANCED_DISPATCH
	}

#if defined(_MSC_VER) && defined(_M_X64)
	__declspec(code_seg(".vstext"))
#endif
	bool FBytecodeVmExecution::RunEnhancedLoop(const bool bEnableIndexedExpressions)
	{
		constexpr bool bInstrumented = false;
		constexpr bool bGoverned = false;
		constexpr bool bBatchArithmetic = false;
		constexpr bool bGlobalInitializing = false;
#define VEROSCRIPT_VM_ENHANCED_DISPATCH 1
#include "VeroScriptBytecodeVmDispatch.inl"
#undef VEROSCRIPT_VM_ENHANCED_DISPATCH
	}
	template bool FBytecodeVmExecution::RunLoop<false, false>();
	template bool FBytecodeVmExecution::RunLoop<false, true>();
	template bool FBytecodeVmExecution::RunLoop<false, true, true>();
	template bool FBytecodeVmExecution::RunLoop<true, false>();
	template bool FBytecodeVmExecution::RunLoop<true, true>();
	template bool FBytecodeVmExecution::RunLoop<false, false, false, true>();
	template bool FBytecodeVmExecution::RunLoop<false, true, false, true>();
}
