// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptVmLocalExpressions.h"
#include "VeroScriptExecutionServices.h"

namespace VeroScript::VmIndexedExpressions
{
	enum class EFusionResult : std::uint8_t { None, Store, Branch };
	struct FConditionalBranch
	{
		std::size_t After;
		std::int32_t Offset;
		bool Condition;
	};

	// Used only by the enhanced dispatcher. The ordinary scalar cache and its
	// layout remain unchanged. No borrowed data survives this synchronous run.
	class FCache
	{
	public:
		FCache() = default;
		explicit FCache(const FVmExecutionView& InState, bool InEnabled = true)
		    : State(&InState), bEnabled(InEnabled) {}
		void BindState(const FVmExecutionView& InState, bool InEnabled = true) noexcept
		{
			State = &InState;
			bEnabled = InEnabled;
		}

		EFusionResult TryExecute(std::int32_t First, const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t& Next, std::vector<FValue>& Locals,
		    std::size_t StackRoom, FConditionalBranch& Branch)
		{
			if (Function.LocalCount > Function.ParamCount + Function.Captures.size())
			{
				if (Scalar.TryExecute(First, Module, Function, Next, Locals, StackRoom)) return EFusionResult::Store;
				if (bEnabled && TryExecuteIndexed(First, Module, Function, Next, Locals, StackRoom)) return EFusionResult::Store;
			}
			return TryExecuteConditional(First, Module, Function, Next, Locals, StackRoom, Branch)
			    ? EFusionResult::Branch : EFusionResult::None;
		}
		bool TryExecuteWide(const FValue& First, const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t& Next, std::vector<FValue>& Locals,
		    std::size_t StackRoom)
		{
			return Scalar.TryExecuteWide(First, Module, Function, Next, Locals, StackRoom);
		}
		std::uint64_t GetInt32TailSteps(const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t Next)
		{
			return Scalar.GetInt32TailSteps(Module, Function, Next);
		}

		static bool CanFuseInFunction(const FBytecodeModule& Module, const FBytecodeFunction& Function,
		    const std::uint8_t* LocalKinds, std::size_t LocalCount);
		bool TryExecuteConditional(std::int32_t First, const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t Begin, const std::vector<FValue>& Locals,
		    std::size_t StackRoom, FConditionalBranch& Branch);

	private:
		static constexpr std::size_t MaxInstructions = 24;
		static constexpr std::size_t MaxDepth = 8;
		static constexpr std::size_t CacheSize = 8;
		enum class EEntryKind : std::uint8_t { None, IndexedStore, PureCondition };
		struct FEntry
		{
			const FBytecodeFunction* Function = nullptr;
			std::size_t Begin = 0;
			std::size_t End = 0;
			std::array<std::int32_t, MaxInstructions> Arguments;
			std::array<EOpcode, MaxInstructions> Opcodes;
			union
			{
				std::uint16_t Target = 0;
				std::int32_t BranchOffset;
			};
			std::uint8_t Count = 0;
			std::uint8_t PeakDepth = 0;
			EOpcode Comparison = EOpcode::Nop;
			EEntryKind Kind = EEntryKind::None;
		};
		static void DecodeConditional(FEntry& Entry, const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t Begin);
		static void Decode(FEntry& Entry, const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t Begin);
		FEntry& GetEntry(const FBytecodeModule& Module, const FBytecodeFunction& Function, const std::size_t Begin)
		{
			const auto Slot = (reinterpret_cast<std::uintptr_t>(&Function) / alignof(FBytecodeFunction) + Begin) % CacheSize;
			auto& Entry = Entries[Slot];
			if (Entry.Function != &Function || Entry.Begin != Begin)
			{
				Decode(Entry, Module, Function, Begin);
				const auto Mask = static_cast<std::uint8_t>(1u << Slot);
				if (Entry.Kind == EEntryKind::None && !(ConditionalDecodeAttemptsMask & Mask))
				{
					// A collision may evict a condition, but cannot repeatedly scan
					// its target prefix: at most one condition attempt per slot/run.
					ConditionalDecodeAttemptsMask |= Mask;
					DecodeConditional(Entry, Module, Function, Begin);
				}
			}
			return Entry;
		}
		bool TryExecuteIndexed(std::int32_t First, const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t& Next, std::vector<FValue>& Locals,
		    std::size_t StackRoom);
		bool Execute(std::int32_t First, const FBytecodeFunction& Function,
		    std::size_t& Next, std::vector<FValue>& Locals, std::size_t StackRoom, const FEntry& Entry);

		VmLocalExpressions::FCache Scalar;
		std::array<FEntry, CacheSize> Entries;
		const FVmExecutionView* State = nullptr;
		bool bEnabled = true;
		std::uint8_t ConditionalDecodeAttemptsMask = 0;
	};
}
