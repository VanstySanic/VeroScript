// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptVmScalarArithmetic.h"

namespace VeroScript::VmLocalExpressions
{
	// The cache is private to one synchronous RunLoop. Its keys borrow the
	// immutable bytecode and cannot survive program replacement or reentry.
	class FCache
	{
	public:
		bool TryExecute(std::int32_t First, const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t& Next, std::vector<FValue>& Locals,
		    std::size_t StackRoom)
		{
			const FEntry& Entry = GetEntry(Module, Function, Next);
			if (Entry.bWide || Entry.PeakDepth == 0 || StackRoom < Entry.PeakDepth || Locals.size() < Function.LocalCount)
			{
				return false;
			}
			auto* const Target = std::get_if<std::int32_t>(&Locals[Entry.Target]);
			if (!Target) return false;

			std::array<std::int32_t, MaxDepth> Values;
			Values[0] = First;
			std::size_t Count = 1;
			for (std::size_t Index = 0; Index < Entry.Count; ++Index)
			{
				const EOpcode Opcode = Entry.Opcodes[Index];
				const std::int32_t Argument = Entry.Arguments[Index];
				if (Opcode == EOpcode::LoadLocal)
				{
					const auto* const Value = std::get_if<std::int32_t>(&Locals[static_cast<std::size_t>(Argument)]);
					if (!Value) return false;
					Values[Count++] = *Value;
				}
				else if (Opcode == EOpcode::ConstI32)
				{
					Values[Count++] = Argument;
				}
				else
				{
					const std::int32_t Right = Values[--Count];
					std::int32_t& Left = Values[Count - 1];
					switch (Opcode)
					{
					case EOpcode::BitAnd:
						{
							Left &= Right;
							break;
						}
					case EOpcode::BitOr:
						{
							Left |= Right;
							break;
						}
					case EOpcode::BitXor:
						{
							Left ^= Right;
							break;
						}
					default:
						{
							if (!VmDetail::TryEvalInt32Arithmetic(Opcode, Left, Right, Left) && !TryShiftInt32(Opcode, Left, Right))
							{
								return false;
							}
							break;
						}
					}
				}
			}
			// Type or arithmetic failures leave the original locals and cursor
			// untouched so the canonical dispatch reports the precise opcode.
			*Target = Values[0];
			Next = Entry.End;
			return true;
		}

		std::uint64_t GetInt32TailSteps(const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t Next)
		{
			const FEntry& Entry = GetEntry(Module, Function, Next);
			return Entry.PeakDepth != 0 && !Entry.bWide ? Entry.Count + 1u : 0;
		}

	private:
		// Extra opcode handling is reached only after ordinary arithmetic declines.
		static bool TryShiftInt32(EOpcode Opcode, std::int32_t& Left, std::int32_t Right) noexcept
		{
			// Match canonical i32 shifts, including negative left operands and
			// truncating left shifts. Reject the count before evaluating any shift.
			if ((Opcode != EOpcode::Shl && Opcode != EOpcode::Shr) || static_cast<std::uint32_t>(Right) > 31u)
			{
				return false;
			}
			Left = Opcode == EOpcode::Shl
			    ? static_cast<std::int32_t>(static_cast<std::uint32_t>(Left) << Right)
			    : static_cast<std::int32_t>(Left >> Right);
			return true;
		}

	public:

		bool TryExecuteWide(const FValue& First, const FBytecodeModule& Module,
		    const FBytecodeFunction& Function, std::size_t& Next, std::vector<FValue>& Locals,
		    std::size_t StackRoom)
		{
			const FEntry& Entry = GetEntry(Module, Function, Next);
			if (Entry.PeakDepth == 0 || StackRoom < Entry.PeakDepth || Locals.size() < Function.LocalCount)
			{
				return false;
			}
			std::array<FNumber, MaxDepth> Values;
			if (!ReadNumber(First, Values[0])) return false;
			std::size_t Count = 1;
			for (std::size_t Index = 0; Index < Entry.Count; ++Index)
			{
				const EOpcode Opcode = Entry.Opcodes[Index];
				const auto Argument = Entry.Arguments[Index];
				switch (Opcode)
				{
				case EOpcode::LoadLocal:
					{
						if (!ReadNumber(Locals[static_cast<std::size_t>(Argument)], Values[Count])) return false;
						++Count;
						break;
					}
				case EOpcode::ConstI32:
					{
						if (Entry.WideIntegerConstants & (std::uint32_t{1} << Index))
						{
							Values[Count++].SetInteger(Module.Constants[static_cast<std::size_t>(Argument)].I64, ENumber::I64);
						}
						else
						{
							Values[Count++].SetInteger(Argument, ENumber::I32);
						}
						break;
					}
				case EOpcode::ConstF64:
					{
						Values[Count++].SetDouble(Module.Constants[static_cast<std::size_t>(Argument)].F64);
						break;
					}
				case EOpcode::CallBuiltin:
					{
						// Decode accepts only the unary ToDouble builtin. Conversion
						// of an actual i32 is exact and has no host hooks.
						FNumber& Number = Values[Count - 1];
						if (Number.Kind != ENumber::I32) return false;
						Number.SetDouble(static_cast<double>(Number.Integer));
						break;
					}
				default:
					{
						const FNumber& Right = Values[--Count];
						if (!EvalWide(Opcode, Values[Count - 1], Right)) return false;
						break;
					}
				}
			}
			FValue& Target = Locals[Entry.Target];
			const FNumber& Result = Values[0];
			if (Result.Kind == ENumber::F64)
			{
				auto* Value = std::get_if<double>(&Target);
				if (!Value) return false;
				*Value = Result.Double;
			}
			else if (Result.Kind == ENumber::I64)
			{
				auto* Value = std::get_if<std::int64_t>(&Target);
				if (!Value) return false;
				*Value = Result.Integer;
			}
			else
			{
				return false;
			}
			Next = Entry.End;
			return true;
		}

	private:
		static constexpr std::size_t CacheSize = 8;
		static constexpr std::size_t MaxInstructions = 24;
		static constexpr std::size_t MaxDepth = 8;
		static_assert(MaxInstructions <= 32);

		enum class ENumber : std::uint8_t { I32, I64, F64 };

		struct FNumber
		{
			ENumber Kind;
			union
			{
				std::int64_t Integer;
				double Double;
			};
			void SetInteger(std::int64_t Value, ENumber Type) noexcept
			{
				Kind = Type;
				Integer = Value;
			}
			void SetDouble(double Value) noexcept
			{
				Kind = ENumber::F64;
				Double = Value;
			}
		};

		static bool ReadNumber(const FValue& Value, FNumber& Out) noexcept
		{
			if (const auto* I32 = std::get_if<std::int32_t>(&Value))
			{
				Out.SetInteger(*I32, ENumber::I32);
				return true;
			}
			if (const auto* I64 = std::get_if<std::int64_t>(&Value))
			{
				Out.SetInteger(*I64, ENumber::I64);
				return true;
			}
			if (const auto* F64 = std::get_if<double>(&Value))
			{
				Out.SetDouble(*F64);
				return true;
			}
			return false;
		}

		static bool EvalWide(EOpcode Opcode, FNumber& Left, const FNumber& Right)
		{
			if (Left.Kind == ENumber::F64 || Right.Kind == ENumber::F64)
			{
				if (Left.Kind != Right.Kind) return false;
				switch (Opcode)
				{
				case EOpcode::Add:
					{
						Left.Double += Right.Double;
						return true;
					}
				case EOpcode::Sub:
					{
						Left.Double -= Right.Double;
						return true;
					}
				case EOpcode::Mul:
					{
						Left.Double *= Right.Double;
						return true;
					}
				case EOpcode::Div:
					{
						if (Right.Double == 0.0) return false;
						Left.Double /= Right.Double;
						return true;
					}
				default:
					return false;
				}
			}
			if (Left.Kind == ENumber::I32 && Right.Kind == ENumber::I32)
			{
				std::int32_t Result = 0;
				if (!VmDetail::TryEvalInt32Arithmetic(Opcode, static_cast<std::int32_t>(Left.Integer),
				        static_cast<std::int32_t>(Right.Integer), Result))
				{
					return false;
				}
				Left.Integer = Result;
				return true;
			}
			std::int64_t Result = 0;
			if (VmDetail::EvalSigned64Arithmetic(Opcode, Left.Integer, Right.Integer, Result)
			    != VmDetail::ESignedArithmeticResult::Ok)
			{
				return false;
			}
			Left.SetInteger(Result, ENumber::I64);
			return true;
		}

		struct FEntry
		{
			const FBytecodeFunction* Function = nullptr;
			std::size_t Begin;
			std::size_t End;
			std::array<std::int32_t, MaxInstructions> Arguments;
			std::array<EOpcode, MaxInstructions> Opcodes;
			std::uint16_t Target;
			std::uint8_t Count;
			std::uint8_t PeakDepth;
			bool bWide;
			std::uint32_t WideIntegerConstants;
		};

		const FEntry& GetEntry(const FBytecodeModule& Module, const FBytecodeFunction& Function,
		    std::size_t Begin)
		{
			const std::size_t Slot = (reinterpret_cast<std::uintptr_t>(&Function) / alignof(FBytecodeFunction) + Begin)
			    % CacheSize;
			FEntry& Entry = Entries[Slot];
			if (Entry.Function != &Function || Entry.Begin != Begin)
			{
				Decode(Entry, Module, Function, Begin);
			}
			return Entry;
		}

		static void Decode(FEntry& Entry, const FBytecodeModule& Module, const FBytecodeFunction& Function,
		    std::size_t Begin)
		{
			Entry.Function = &Function;
			Entry.Begin = Begin;
			Entry.PeakDepth = 0;
			Entry.bWide = false;
			Entry.WideIntegerConstants = 0;
			const auto& Code = Function.Code;
			std::size_t Cursor = Begin;
			std::size_t Depth = 1;
			std::size_t PeakDepth = Depth;
			for (std::size_t Index = 0; Index < MaxInstructions && Cursor < Code.size(); ++Index)
			{
				const auto Opcode = static_cast<EOpcode>(Code[Cursor]);
				const std::size_t Size = Bytecode::InstructionSize(Opcode);
				if (Size == 0 || Size > Code.size() - Cursor) return;
				if (Opcode == EOpcode::StoreLocal)
				{
					const auto Target = Bytecode::ReadU16LE(Code.data() + Cursor + 1);
					if (Depth != 1 || Target >= Function.LocalCount) return;
					Entry.Target = Target;
					Entry.Count = static_cast<std::uint8_t>(Index);
					Entry.End = Cursor + Size;
					Entry.PeakDepth = static_cast<std::uint8_t>(PeakDepth);
					return;
				}

				std::int32_t Argument = 0;
				switch (Opcode)
				{
				case EOpcode::LoadLocal:
				case EOpcode::ConstI32:
				case EOpcode::ConstF64:
					{
						if (Depth == MaxDepth) return;
						++Depth;
						if (Depth > PeakDepth)
						{
							PeakDepth = Depth;
						}
						const auto Operand = Bytecode::ReadU16LE(Code.data() + Cursor + 1);
						if (Opcode == EOpcode::LoadLocal)
						{
							if (Operand >= Function.LocalCount) return;
							Argument = Operand;
						}
						else
						{
							if (Operand >= Module.Constants.size()) return;
							const auto& Constant = Module.Constants[Operand];
							if (Opcode == EOpcode::ConstI32 && Constant.Kind == EConstantKind::I32)
							{
								Argument = Constant.I32;
							}
							else if (Opcode == EOpcode::ConstI32 && Constant.Kind == EConstantKind::I64)
							{
								Entry.WideIntegerConstants |= std::uint32_t{1} << Index;
								Argument = Operand;
								Entry.bWide = true;
							}
							else if (Opcode == EOpcode::ConstF64 && Constant.Kind == EConstantKind::F64)
							{
								Argument = Operand;
								Entry.bWide = true;
							}
							else
							{
								return;
							}
						}
						break;
					}
				case EOpcode::CallBuiltin:
					{
						if (Depth == 0 || Code[Cursor + 1] != static_cast<std::uint8_t>(EBuiltinId::ToDouble)
						    || Code[Cursor + 2] != 1)
						{
							return;
						}
						Entry.bWide = true;
						break;
					}
				case EOpcode::Add:
				case EOpcode::Sub:
				case EOpcode::Mul:
				case EOpcode::Div:
				case EOpcode::Mod:
				case EOpcode::BitAnd:
				case EOpcode::BitOr:
				case EOpcode::BitXor:
				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						if (Depth < 2) return;
						--Depth;
						break;
					}
				default:
					{
						return;
					}
				}
				Entry.Opcodes[Index] = Opcode;
				Entry.Arguments[Index] = Argument;
				Cursor += Size;
			}
		}

		std::array<FEntry, CacheSize> Entries;
	};
}
