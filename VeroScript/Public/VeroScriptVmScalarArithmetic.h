// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptRuntime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// The bounded evaluator repeatedly uses this small checked operation.
#if defined(_MSC_VER)
#define VEROSCRIPT_VM_ARITHMETIC_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define VEROSCRIPT_VM_ARITHMETIC_INLINE inline __attribute__((always_inline))
#else
#define VEROSCRIPT_VM_ARITHMETIC_INLINE inline
#endif

namespace VeroScript::VmDetail
{
	enum class ESignedArithmeticResult : std::uint8_t
	{
		Ok,
		DivisionByZero,
		Overflow,
		InvalidOpcode,
	};

	inline ESignedArithmeticResult EvalSigned64Arithmetic(const EOpcode Op,
		const std::int64_t L, const std::int64_t R, std::int64_t& Result)
	{
		bool bOverflow = false;
		switch (Op)
		{
		case EOpcode::Add:
			{
				bOverflow = (R > 0 && L > std::numeric_limits<std::int64_t>::max() - R)
					|| (R < 0 && L < std::numeric_limits<std::int64_t>::min() - R);
				if (!bOverflow)
				{
					Result = L + R;
				}
				break;
			}
		case EOpcode::Sub:
			{
				bOverflow = (R < 0 && L > std::numeric_limits<std::int64_t>::max() + R)
					|| (R > 0 && L < std::numeric_limits<std::int64_t>::min() + R);
				if (!bOverflow)
				{
					Result = L - R;
				}
				break;
			}
		case EOpcode::Mul:
			{
				if (L != 0 && R != 0)
				{
					bOverflow =
						(L == -1 && R == std::numeric_limits<std::int64_t>::min())
						|| (R == -1 && L == std::numeric_limits<std::int64_t>::min())
						|| (L > 0 && R > 0
							&& L > std::numeric_limits<std::int64_t>::max() / R)
						|| (L > 0 && R < 0
							&& R < std::numeric_limits<std::int64_t>::min() / L)
						|| (L < 0 && R > 0
							&& L < std::numeric_limits<std::int64_t>::min() / R)
						|| (L < 0 && R < 0
							&& L < std::numeric_limits<std::int64_t>::max() / R);
				}
				if (!bOverflow)
				{
					Result = L * R;
				}
				break;
			}
		case EOpcode::Div:
		case EOpcode::Mod:
			{
				if (R == 0)
				{
					return ESignedArithmeticResult::DivisionByZero;
				}
				bOverflow =
					L == std::numeric_limits<std::int64_t>::min() && R == -1;
				if (!bOverflow)
				{
					Result = Op == EOpcode::Div ? L / R : L % R;
				}
				break;
			}
		default:
			{
				return ESignedArithmeticResult::InvalidOpcode;
			}
		}
		if (bOverflow)
		{
			return ESignedArithmeticResult::Overflow;
		}
		return ESignedArithmeticResult::Ok;
	}

	VEROSCRIPT_VM_ARITHMETIC_INLINE bool TryEvalInt32Arithmetic(const EOpcode Op, const std::int32_t Left,
		const std::int32_t Right, std::int32_t& Value)
	{
		bool Ok = false;
		switch (Op)
		{
		case EOpcode::Add:
			{
				Ok = Runtime::CheckedAdd(Left, Right, Value);
				break;
			}
		case EOpcode::Sub:
			{
				Ok = Runtime::CheckedSub(Left, Right, Value);
				break;
			}
		case EOpcode::Mul:
			{
				Ok = Runtime::CheckedMul(Left, Right, Value);
				break;
			}
		case EOpcode::Div:
		case EOpcode::Mod:
			{
				if (Right != 0 && !(Left == std::numeric_limits<std::int32_t>::min() && Right == -1))
				{
					Value = Op == EOpcode::Div ? Left / Right : Left % Right;
					Ok = true;
				}
				break;
			}
		default:
			{
				break;
			}
		}
		return Ok;
	}

	// Same-tag scalars have no release hooks. Keep mismatched types and owned
	// values on the canonical comparison path, including its diagnostics.
	inline bool TryEvalScalarStackEquality(const EOpcode Op, std::vector<FValue>& Stack,
		const std::size_t Base, const std::size_t MaximumDepth)
	{
		if ((Op != EOpcode::Eq && Op != EOpcode::Ne) || Stack.size() < Base
			|| Stack.size() - Base < 2u || Stack.size() - 1u > MaximumDepth)
		{
			return false;
		}
		auto& Left = Stack[Stack.size() - 2u];
		const auto& Right = Stack.back();
		if (Left.index() != Right.index()) return false;
		bool Equal = false;
		const auto Compare = [&](auto Tag)
		{
			using T = decltype(Tag);
			const auto* L = std::get_if<T>(&Left);
			if (!L) return false;
			Equal = *L == std::get<T>(Right);
			return true;
		};
		if (!(Compare(std::int32_t{}) || Compare(std::int64_t{})
			|| Compare(std::uint32_t{}) || Compare(std::uint64_t{})
			|| Compare(double{}) || Compare(float{}) || Compare(bool{})
			|| Compare(std::int8_t{}) || Compare(std::uint8_t{})
			|| Compare(std::int16_t{}) || Compare(std::uint16_t{})))
		{
			return false;
		}
		Left = Op == EOpcode::Eq ? Equal : !Equal;
		Stack.pop_back();
		return true;
	}

	// Failure leaves operands intact for the canonical arithmetic diagnostic.
	inline bool TryEvalScalarStackArithmetic(const EOpcode Op, std::vector<FValue>& Stack,
		const std::size_t Base, const std::size_t MaximumDepth)
	{
		if (Stack.size() < Base || Stack.size() - Base < 2u || Stack.size() - 1u > MaximumDepth)
		{
			return false;
		}
		auto& LeftValue = Stack[Stack.size() - 2u];
		const auto& RightValue = Stack.back();
		const auto* Left = std::get_if<std::int32_t>(&LeftValue);
		const auto* Right = std::get_if<std::int32_t>(&RightValue);
		if (Left && Right)
		{
			std::int32_t Value = 0;
			if (!TryEvalInt32Arithmetic(Op, *Left, *Right, Value)) return false;
			*std::get_if<std::int32_t>(&LeftValue) = Value;
		}
		else if ((std::holds_alternative<std::int64_t>(LeftValue) && (Right || std::holds_alternative<std::int64_t>(RightValue)))
			|| (Left && std::holds_alternative<std::int64_t>(RightValue)))
		{
			const auto L = Left ? static_cast<std::int64_t>(*Left) : std::get<std::int64_t>(LeftValue);
			const auto R = Right ? static_cast<std::int64_t>(*Right) : std::get<std::int64_t>(RightValue);
			std::int64_t Value = 0;
			if (EvalSigned64Arithmetic(Op, L, R, Value) != ESignedArithmeticResult::Ok) return false;
			LeftValue = Value;
		}
		else if (const auto* L = std::get_if<double>(&LeftValue))
		{
			const auto* R = std::get_if<double>(&RightValue);
			if (!R) return false;
			double Value = 0.0;
			switch (Op)
			{
			case EOpcode::Add:
				{
					Value = *L + *R;
					break;
				}
			case EOpcode::Sub:
				{
					Value = *L - *R;
					break;
				}
			case EOpcode::Mul:
				{
					Value = *L * *R;
					break;
				}
			case EOpcode::Div:
				{
					if (*R == 0.0) return false;
					Value = *L / *R;
					break;
				}
			default:
				{
					return false;
				}
			}
			*std::get_if<double>(&LeftValue) = Value;
		}
		else
		{
			return false;
		}
		Stack.pop_back();
		return true;
	}

	// Commit a pure integer expression only after every read and checked operation
	// succeeds. Debugging and governed execution retain individual opcode visits.
	inline bool TryEvalLocalExpression(const std::int32_t First, const FBytecodeModule& Module,
		const std::vector<std::uint8_t>& Code, std::size_t& Next, std::vector<FValue>& Locals,
		const std::size_t StackRoom)
	{
		if (StackRoom == 0u) return false;
		// Reject call arguments and return expressions before doing arithmetic that
		// the ordinary VM would otherwise have to repeat after a failed probe.
		std::size_t End = Next;
		std::size_t Depth = 1u;
		bool bHasStore = false;
		for (unsigned Step = 0; Step < 24u && End < Code.size(); ++Step)
		{
			const auto Op = static_cast<EOpcode>(Code[End]);
			const auto Size = Bytecode::InstructionSize(Op);
			if (Size == 0u || Size > Code.size() - End) return false;
			if (Op == EOpcode::StoreLocal)
			{
				bHasStore = Depth == 1u;
				break;
			}
			if (Op == EOpcode::LoadLocal || Op == EOpcode::ConstI32)
			{
				if (Depth >= 8u || Depth >= StackRoom) return false;
				++Depth;
			}
			else if (Op == EOpcode::Add || Op == EOpcode::Sub || Op == EOpcode::Mul
				|| Op == EOpcode::Div || Op == EOpcode::Mod)
			{
				if (Depth < 2u) return false;
				--Depth;
			}
			else
			{
				return false;
			}
			End += Size;
		}
		if (!bHasStore) return false;
		std::array<std::int32_t, 8> Values;
		Values[0] = First;
		std::size_t Count = 1u;
		std::size_t Cursor = Next;
		for (unsigned Step = 0; Step < 24u && Cursor < Code.size(); ++Step)
		{
			const auto Op = static_cast<EOpcode>(Code[Cursor]);
			const auto Size = Bytecode::InstructionSize(Op);
			if (Size == 0u || Size > Code.size() - Cursor) return false;
			if (Op == EOpcode::LoadLocal || Op == EOpcode::ConstI32)
			{
				if (Count == Values.size() || Count >= StackRoom) return false;
				const auto Index = Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
				if (Op == EOpcode::LoadLocal)
				{
					if (Index >= Locals.size()) return false;
					const auto* Value = std::get_if<std::int32_t>(&Locals[Index]);
					if (!Value) return false;
					Values[Count++] = *Value;
				}
				else
				{
					if (Index >= Module.Constants.size() || Module.Constants[Index].Kind != EConstantKind::I32) return false;
					Values[Count++] = Module.Constants[Index].I32;
				}
			}
			else if (Op == EOpcode::StoreLocal)
			{
				const auto Slot = Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
				if (Count != 1u || Slot >= Locals.size()) return false;
				auto* Target = std::get_if<std::int32_t>(&Locals[Slot]);
				if (!Target) return false;
				*Target = Values[0];
				Next = Cursor + Size;
				return true;
			}
			else
			{
				if (Count < 2u || !TryEvalInt32Arithmetic(Op, Values[Count - 2u], Values[Count - 1u], Values[Count - 2u])) return false;
				--Count;
			}
			Cursor += Size;
		}
		return false;
	}
}

#undef VEROSCRIPT_VM_ARITHMETIC_INLINE
