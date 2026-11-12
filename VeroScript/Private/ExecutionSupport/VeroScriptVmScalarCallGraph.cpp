// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmScalarCallGraph.h"
#include "VeroScriptRun.h"
#include "VeroScriptVmScalarArithmetic.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>

namespace VeroScript
{
	namespace
	{
		constexpr std::size_t MaxFunctions = 8;
		constexpr std::size_t MaxInstructions = 64;
		constexpr std::size_t MaxCodeBytes = 512;
		constexpr std::size_t MaxLocals = 8;
		constexpr std::size_t MaxOperands = 16;
		constexpr std::size_t InlineFrames = 48;
		constexpr std::size_t InlineOperands = 384;

		// Every admitted frame has at most eight independent integer slots. Keep
		// these tiny transfers inline instead of spilling interpreter state across
		// a general-purpose memmove call on every recursive script call.
		#if defined(_MSC_VER)
		__forceinline
		#elif defined(__GNUC__) || defined(__clang__)
		inline __attribute__((always_inline))
		#else
		inline
		#endif
		void CopyScalarArguments(std::int32_t* Destination, const std::int32_t* Source,
			const std::uint8_t Count) noexcept
		{
			switch (Count)
			{
			case 8:
				{
					Destination[7] = Source[7];
					[[fallthrough]];
				}
			case 7:
				{
					Destination[6] = Source[6];
					[[fallthrough]];
				}
			case 6:
				{
					Destination[5] = Source[5];
					[[fallthrough]];
				}
			case 5:
				{
					Destination[4] = Source[4];
					[[fallthrough]];
				}
			case 4:
				{
					Destination[3] = Source[3];
					[[fallthrough]];
				}
			case 3:
				{
					Destination[2] = Source[2];
					[[fallthrough]];
				}
			case 2:
				{
					Destination[1] = Source[1];
					[[fallthrough]];
				}
			case 1:
				{
					Destination[0] = Source[0];
					[[fallthrough]];
				}
			case 0:
				{
					break;
				}
			default:
				{
					break;
				}
			}
		}

		template <typename T, std::size_t InlineCount>
		class TScalarScratch
		{
			static_assert(std::is_trivially_copyable_v<T>);

		public:
			T* GetData() noexcept
			{
				return Overflow ? Overflow.get() : Inline.data();
			}

			std::size_t GetCapacity() const noexcept
			{
				return Capacity;
			}

			bool Ensure(std::size_t Required, std::size_t Used, std::size_t Maximum)
			{
				if (Required <= Capacity) return true;
				const auto Size = (std::min)(Maximum, (std::max)(Required, Capacity * 2));
				std::unique_ptr<T[]> Storage(new (std::nothrow) T[Size]);
				if (!Storage) return false;
				std::memcpy(Storage.get(), GetData(), Used * sizeof(T));
				Overflow = std::move(Storage);
				Capacity = Size;
				return true;
			}

		private:
			std::array<T, InlineCount> Inline;
			std::unique_ptr<T[]> Overflow;
			std::size_t Capacity = InlineCount;
		};

		enum class EScalarOpcode : std::uint8_t
		{
			Nop,
			ConstI32,
			LoadLocal,
			StoreLocal,
			Jmp,
			JmpIfLocalGeImm,
			Eq,
			Ne,
			Lt,
			Le,
			Gt,
			Ge,
			BitAnd,
			BitOr,
			BitXor,
			Add,
			Sub,
			Mul,
			Div,
			Mod,
			Call,
			Return,
			SideExit = 255,
		};

		// The verifier translates the public bytecode once. Dense case values
		// let dispatch index its jump table without an opcode remapping load.
		constexpr auto ScalarOpcodes = []()
		{
			std::array<EScalarOpcode, 256> Result{};
			for (auto& Opcode : Result)
			{
				Opcode = EScalarOpcode::SideExit;
			}
			Result[static_cast<std::uint8_t>(EOpcode::Nop)] = EScalarOpcode::Nop;
			Result[static_cast<std::uint8_t>(EOpcode::ConstI32)] = EScalarOpcode::ConstI32;
			Result[static_cast<std::uint8_t>(EOpcode::LoadLocal)] = EScalarOpcode::LoadLocal;
			Result[static_cast<std::uint8_t>(EOpcode::StoreLocal)] = EScalarOpcode::StoreLocal;
			Result[static_cast<std::uint8_t>(EOpcode::Jmp)] = EScalarOpcode::Jmp;
			Result[static_cast<std::uint8_t>(EOpcode::JmpIfLocalGeImm)] = EScalarOpcode::JmpIfLocalGeImm;
			Result[static_cast<std::uint8_t>(EOpcode::Eq)] = EScalarOpcode::Eq;
			Result[static_cast<std::uint8_t>(EOpcode::Ne)] = EScalarOpcode::Ne;
			Result[static_cast<std::uint8_t>(EOpcode::Lt)] = EScalarOpcode::Lt;
			Result[static_cast<std::uint8_t>(EOpcode::Le)] = EScalarOpcode::Le;
			Result[static_cast<std::uint8_t>(EOpcode::Gt)] = EScalarOpcode::Gt;
			Result[static_cast<std::uint8_t>(EOpcode::Ge)] = EScalarOpcode::Ge;
			Result[static_cast<std::uint8_t>(EOpcode::BitAnd)] = EScalarOpcode::BitAnd;
			Result[static_cast<std::uint8_t>(EOpcode::BitOr)] = EScalarOpcode::BitOr;
			Result[static_cast<std::uint8_t>(EOpcode::BitXor)] = EScalarOpcode::BitXor;
			Result[static_cast<std::uint8_t>(EOpcode::Add)] = EScalarOpcode::Add;
			Result[static_cast<std::uint8_t>(EOpcode::Sub)] = EScalarOpcode::Sub;
			Result[static_cast<std::uint8_t>(EOpcode::Mul)] = EScalarOpcode::Mul;
			Result[static_cast<std::uint8_t>(EOpcode::Div)] = EScalarOpcode::Div;
			Result[static_cast<std::uint8_t>(EOpcode::Mod)] = EScalarOpcode::Mod;
			Result[static_cast<std::uint8_t>(EOpcode::Call)] = EScalarOpcode::Call;
			Result[static_cast<std::uint8_t>(EOpcode::Return)] = EScalarOpcode::Return;
			return Result;
		}();

		struct FScalarInstruction
		{
			EScalarOpcode Opcode;
			std::uint8_t Slot = 0;
			std::uint16_t Target = 0;
			std::int32_t Value = 0;
		};

		struct FScalarFunction
		{
			std::vector<FScalarInstruction> Instructions;
			std::uint8_t ParamCount = 0;
			std::uint8_t LocalCount = 0;
			std::uint8_t OperandDepth = 0;
		};

		bool IsSupportedFunction(const FBytecodeFunction& Function)
		{
			return !Function.bDeleted && Function.UnavailableReason.empty()
			    && !Function.bIsMethod && !Function.bReturnsVoid && !Function.bReturnsRef
			    && !BytecodeFunctionHasRefParams(Function) && Function.Captures.empty()
			    && Function.ExceptionRegions.empty() && Function.LocalCount <= MaxLocals
			    && Function.LocalCount >= Function.ParamCount && !Function.Code.empty()
			    && Function.Code.size() <= MaxCodeBytes;
		}

		bool HasBranchTarget(const EScalarOpcode Opcode)
		{
			return Opcode == EScalarOpcode::Jmp || Opcode == EScalarOpcode::JmpIfLocalGeImm
			    || (Opcode >= EScalarOpcode::Eq && Opcode <= EScalarOpcode::Ge);
		}

	}

	struct FVmScalarCallGraph
	{
		std::array<FScalarFunction, MaxFunctions> Functions;
		// VM-local cache entry. Suppress descendant probes during canonical
		// replay, but let a later VM entry with different arguments try again.
		mutable std::optional<std::uint64_t> FailedRun;
	};

	std::shared_ptr<const FVmScalarCallGraph> BuildScalarCallGraph(
	    const FBytecodeModule& Module, const std::uint16_t FunctionIndex)
	{
		if (FunctionIndex >= Module.Functions.size()) return {};
		FVmScalarCallGraph Graph;
		std::array<std::uint16_t, MaxFunctions> FunctionIndices;
		FunctionIndices[0] = FunctionIndex;
		std::size_t FunctionCount = 1;
		bool bHasControlFlow = false;
		bool bHasThrow = false;
		for (std::size_t Index = 0; Index < FunctionCount; ++Index)
		{
			const auto& Function = Module.Functions[FunctionIndices[Index]];
			if (!IsSupportedFunction(Function)) return {};
			auto& Plan = Graph.Functions[Index];
			Plan.ParamCount = Function.ParamCount;
			Plan.LocalCount = static_cast<std::uint8_t>(Function.LocalCount);
			const auto& Code = Function.Code;
			const auto* const Data = Code.data();
			std::array<std::int16_t, MaxCodeBytes> Depths;
			std::fill_n(Depths.begin(), Code.size(), std::int16_t{-1});
			Depths[0] = 0;
			std::array<std::uint16_t, MaxInstructions> Offsets;
			bool bHasReturn = false;
			for (std::size_t Ip = 0; Ip < Code.size();)
			{
				if (Plan.Instructions.size() == MaxInstructions) return {};
				const auto BytecodeOpcode = static_cast<EOpcode>(Data[Ip]);
				const auto Opcode = ScalarOpcodes[Data[Ip]];
				const auto Size = Bytecode::InstructionSize(BytecodeOpcode);
				if (!Size || Size > Code.size() - Ip) return {};
				std::size_t Next = Ip + Size;
				const auto Depth = Depths[Ip];
				if (Depth < 0) return {};
				std::int16_t After = Depth;
				FScalarInstruction Instruction{Opcode};
				switch (Opcode)
				{
				case EScalarOpcode::Nop:
					{
						break;
					}
				case EScalarOpcode::ConstI32:
					{
						const auto ConstantIndex = Bytecode::ReadU16LE(Data + Ip + 1);
						if (ConstantIndex >= Module.Constants.size() || Module.Constants[ConstantIndex].Kind != EConstantKind::I32)
						{
							return {};
						}
						Instruction.Value = Module.Constants[ConstantIndex].I32;
						++After;
						break;
					}
				case EScalarOpcode::LoadLocal:
				case EScalarOpcode::StoreLocal:
				case EScalarOpcode::JmpIfLocalGeImm:
					{
						const auto Slot = Bytecode::ReadU16LE(Data + Ip + 1);
						if (Slot >= Function.LocalCount) return {};
						Instruction.Slot = static_cast<std::uint8_t>(Slot);
						if (Opcode == EScalarOpcode::JmpIfLocalGeImm)
						{
							Instruction.Value = Bytecode::ReadI32LE(Data + Ip + 3);
						}
						else
						{
							After += Opcode == EScalarOpcode::LoadLocal ? 1 : -1;
						}
						break;
					}
				case EScalarOpcode::Add:
				case EScalarOpcode::Sub:
				case EScalarOpcode::Mul:
				case EScalarOpcode::Div:
				case EScalarOpcode::Mod:
				case EScalarOpcode::BitAnd:
				case EScalarOpcode::BitOr:
				case EScalarOpcode::BitXor:
					{
						if (Depth < 2) return {};
						--After;
						break;
					}
				case EScalarOpcode::Eq:
				case EScalarOpcode::Ne:
				case EScalarOpcode::Lt:
				case EScalarOpcode::Le:
				case EScalarOpcode::Gt:
				case EScalarOpcode::Ge:
					{
						// Booleans remain transient: only a comparison immediately
						// consumed by a branch can enter this integer-only graph.
						if (Depth < 2 || Code.size() - Next < 5 || static_cast<EOpcode>(Data[Next]) != EOpcode::JmpIfFalse)
						{
							return {};
						}
						Next += 5;
						After -= 2;
						break;
					}
				case EScalarOpcode::Jmp:
					{
						break;
					}
				case EScalarOpcode::Call:
					{
						const auto Callee = Bytecode::ReadU16LE(Data + Ip + 1);
						const auto ArgCount = Data[Ip + 3];
						if (Callee >= Module.Functions.size() || Module.Functions[Callee].ParamCount != ArgCount || Depth < ArgCount)
						{
							return {};
						}
						const auto End = FunctionIndices.begin() + FunctionCount;
						const auto Found = std::find(FunctionIndices.begin(), End, Callee);
						Instruction.Slot = static_cast<std::uint8_t>(Found - FunctionIndices.begin());
						if (Found == End)
						{
							if (FunctionCount == MaxFunctions) return {};
							FunctionIndices[FunctionCount++] = Callee;
						}
						After = static_cast<std::int16_t>(Depth - ArgCount + 1);
						bHasControlFlow = true;
						break;
					}
				case EScalarOpcode::Return:
					{
						if (Depth != 1) return {};
						bHasReturn = true;
						break;
					}
				case EScalarOpcode::SideExit:
					{
						if (BytecodeOpcode != EOpcode::Throw || Depth < 1) return {};
						bHasThrow = true;
						break;
					}
				default:
					{
						return {};
					}
				}
				if (After < 0 || static_cast<std::size_t>(After) > MaxOperands) return {};
				Plan.OperandDepth = (std::max)(Plan.OperandDepth, static_cast<std::uint8_t>(After));
				const auto AddEdge = [&](const std::size_t Target)
				{
					if (Target >= Code.size() || (Depths[Target] >= 0 && Depths[Target] != After)) return false;
					Depths[Target] = After;
					return true;
				};
				if (HasBranchTarget(Opcode))
				{
					bHasControlFlow = true;
					// A forward-only graph gives one topological stack-height pass.
					// Script loops retain the regular VM and its existing fusions.
					const auto Target = static_cast<std::int64_t>(Next) + Bytecode::ReadI32LE(Data + Next - 4);
					if (Target < static_cast<std::int64_t>(Next) || !AddEdge(static_cast<std::size_t>(Target))) return {};
					Instruction.Target = static_cast<std::uint16_t>(Target);
				}
				if (Opcode != EScalarOpcode::Return && Opcode != EScalarOpcode::SideExit && Opcode != EScalarOpcode::Jmp && !AddEdge(Next)) return {};
				Offsets[Plan.Instructions.size()] = static_cast<std::uint16_t>(Ip);
				Plan.Instructions.push_back(Instruction);
				Ip = Next;
			}
			if (!bHasReturn) return {};
			for (auto& Instruction : Plan.Instructions)
			{
				if (HasBranchTarget(Instruction.Opcode))
				{
					const auto End = Offsets.begin() + Plan.Instructions.size();
					const auto Target = std::lower_bound(Offsets.begin(), End, Instruction.Target);
					if (Target == End || *Target != Instruction.Target) return {};
					Instruction.Target = static_cast<std::uint16_t>(Target - Offsets.begin());
				}
			}
		}
		if (!bHasControlFlow) return {};
		if (bHasThrow)
		{
			// A throw side exit may replay the pure prefix. Do not speculate
			// through recursion, where each replayed frame could probe it again.
			std::array<unsigned int, MaxFunctions> Reachable{};
			for (std::size_t Index = 0; Index < FunctionCount; ++Index)
			{
				for (const auto& Instruction : Graph.Functions[Index].Instructions)
				{
					if (Instruction.Opcode == EScalarOpcode::Call)
					{
						Reachable[Index] |= 1u << Instruction.Slot;
					}
				}
			}
			for (std::size_t Through = 0; Through < FunctionCount; ++Through)
			{
				for (std::size_t Index = 0; Index < FunctionCount; ++Index)
				{
					if (Reachable[Index] & (1u << Through))
					{
						Reachable[Index] |= Reachable[Through];
					}
				}
			}
			for (std::size_t Index = 0; Index < FunctionCount; ++Index)
			{
				if (Reachable[Index] & (1u << Index)) return {};
			}
		}
		return std::make_shared<const FVmScalarCallGraph>(std::move(Graph));
	}

	bool TryExecuteScalarCallGraph(const FVmScalarCallGraph& Graph, const FValue* Arguments,
	    const std::size_t CallRoom, const std::size_t OperandRoom, const std::uint64_t RunSerial,
	    FValue& OutResult)
	{
		const auto& First = Graph.Functions[0];
		if (Graph.FailedRun == RunSerial || !CallRoom || OperandRoom < First.OperandDepth) return false;
		struct FFrame
		{
			const FScalarInstruction* Code;
			const FScalarInstruction* ReturnCursor;
			std::array<std::int32_t, MaxLocals> Locals;
		};
		// Keep shallow calls below a stack page. Deep recursion grows only its
		// compact storage, leaving the canonical VM untouched until it succeeds.
		TScalarScratch<FFrame, InlineFrames> Frames;
		TScalarScratch<std::int32_t, InlineOperands> Operands;
		const auto FrameRoom = (std::min)(CallRoom, VEROSCRIPT_DEFAULT_MAX_CALL_DEPTH);
		auto* Begin = Frames.GetData();
		FFrame* Current = Begin;
		const auto* Last = Begin + (std::min)(FrameRoom, Frames.GetCapacity()) - 1;
		auto* OperandBegin = Operands.GetData();
		auto* Top = OperandBegin;
		const auto* Limit = OperandBegin + (std::min)(OperandRoom, Operands.GetCapacity());
		Current->Code = First.Instructions.data();
		const auto* Cursor = Current->Code;
		for (std::size_t I = 0; I < First.ParamCount; ++I)
		{
			const auto* Value = std::get_if<std::int32_t>(&Arguments[I]);
			if (!Value) return false;
			Current->Locals[I] = *Value;
		}
		if (First.ParamCount != First.LocalCount)
		{
			std::fill(Current->Locals.begin() + First.ParamCount, Current->Locals.begin() + First.LocalCount, 0);
		}
		const auto BranchUnless = [&](const bool bCondition, const std::uint16_t Target)
		{
			Top -= 2;
			if (!bCondition)
			{
				Cursor = Current->Code + Target;
			}
		};
		const auto Arithmetic = [&](const EOpcode Opcode)
		{
			--Top;
			return VmDetail::TryEvalInt32Arithmetic(Opcode, Top[-1], *Top, Top[-1]);
		};
		for (;;)
		{
			const auto& Instruction = *Cursor++;
			switch (Instruction.Opcode)
			{
			case EScalarOpcode::Nop:
				{
					continue;
				}
			case EScalarOpcode::ConstI32:
				{
					*Top++ = Instruction.Value;
					continue;
				}
			case EScalarOpcode::LoadLocal:
				{
					*Top++ = Current->Locals[Instruction.Slot];
					continue;
				}
			case EScalarOpcode::StoreLocal:
				{
					Current->Locals[Instruction.Slot] = *--Top;
					continue;
				}
			case EScalarOpcode::Jmp:
				{
					Cursor = Current->Code + Instruction.Target;
					continue;
				}
			case EScalarOpcode::JmpIfLocalGeImm:
				{
					if (Current->Locals[Instruction.Slot] >= Instruction.Value)
					{
						Cursor = Current->Code + Instruction.Target;
					}
					continue;
				}
			case EScalarOpcode::Eq:
				{
					BranchUnless(Top[-2] == Top[-1], Instruction.Target);
					continue;
				}
			case EScalarOpcode::Ne:
				{
					BranchUnless(Top[-2] != Top[-1], Instruction.Target);
					continue;
				}
			case EScalarOpcode::Lt:
				{
					BranchUnless(Top[-2] < Top[-1], Instruction.Target);
					continue;
				}
			case EScalarOpcode::Le:
				{
					BranchUnless(Top[-2] <= Top[-1], Instruction.Target);
					continue;
				}
			case EScalarOpcode::Gt:
				{
					BranchUnless(Top[-2] > Top[-1], Instruction.Target);
					continue;
				}
			case EScalarOpcode::Ge:
				{
					BranchUnless(Top[-2] >= Top[-1], Instruction.Target);
					continue;
				}
			case EScalarOpcode::BitAnd:
				{
					--Top;
					Top[-1] &= *Top;
					continue;
				}
			case EScalarOpcode::BitOr:
				{
					--Top;
					Top[-1] |= *Top;
					continue;
				}
			case EScalarOpcode::BitXor:
				{
					--Top;
					Top[-1] ^= *Top;
					continue;
				}
			case EScalarOpcode::Add:
				{
					if (Arithmetic(EOpcode::Add))
					{
						continue;
					}
					break;
				}
			case EScalarOpcode::Sub:
				{
					if (Arithmetic(EOpcode::Sub))
					{
						continue;
					}
					break;
				}
			case EScalarOpcode::Mul:
				{
					if (Arithmetic(EOpcode::Mul))
					{
						continue;
					}
					break;
				}
			case EScalarOpcode::Div:
				{
					if (Arithmetic(EOpcode::Div))
					{
						continue;
					}
					break;
				}
			case EScalarOpcode::Mod:
				{
					if (Arithmetic(EOpcode::Mod))
					{
						continue;
					}
					break;
				}
			case EScalarOpcode::Call:
				{
					const auto& Callee = Graph.Functions[Instruction.Slot];
					Top -= Callee.ParamCount;
					if (Current == Last || Limit - Top < Callee.OperandDepth)
					{
						const auto FrameCount = static_cast<std::size_t>(Current - Begin) + 1;
						const auto OperandCount = static_cast<std::size_t>(Top - OperandBegin);
						const auto NeedOperands = OperandCount + Callee.OperandDepth;
						if (FrameCount == FrameRoom || NeedOperands > OperandRoom ||
						    !Frames.Ensure(FrameCount + 1, FrameCount, FrameRoom) ||
						    !Operands.Ensure(NeedOperands, OperandCount + Callee.ParamCount, OperandRoom))
						{
							Graph.FailedRun = RunSerial;
							return false;
						}
						Begin = Frames.GetData();
						Current = Begin + FrameCount - 1;
						Last = Begin + (std::min)(FrameRoom, Frames.GetCapacity()) - 1;
						OperandBegin = Operands.GetData();
						Top = OperandBegin + OperandCount;
						Limit = OperandBegin + (std::min)(OperandRoom, Operands.GetCapacity());
					}
					Current->ReturnCursor = Cursor;
					++Current;
					Current->Code = Callee.Instructions.data();
					Cursor = Current->Code;
					CopyScalarArguments(Current->Locals.data(), Top, Callee.ParamCount);
					if (Callee.ParamCount != Callee.LocalCount)
					{
						std::fill(Current->Locals.begin() + Callee.ParamCount,
							Current->Locals.begin() + Callee.LocalCount, 0);
					}
					continue;
				}
			case EScalarOpcode::Return:
				{
					if (Current == Begin)
					{
						OutResult = Top[-1];
						return true;
					}
					--Current;
					Cursor = Current->ReturnCursor;
					continue;
				}
			default:
				{
					// Keep explicit throws on the cold side exit. The VM replays
					// the verified pure prefix and unwinds at the original opcode.
					return false;
				}
			}
			Graph.FailedRun = RunSerial;
			return false;
		}
	}
}
