// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVmStringLocals.h"

#include "VeroScriptBytecode.h"
#include "VeroScriptRuntime.h"

#include <array>
#include <new>
#include <stdexcept>

namespace VeroScript::VmStringLocals
{
	namespace
	{
		constexpr unsigned MaxAppendOperations = 24u;

		template <bool bNewText>
		bool HasAppendLocalShape(const std::vector<std::uint8_t> &Code, const std::size_t Begin,
		    const std::uint16_t Slot, const FBytecodeModule& Module, const std::vector<FValue>& Locals)
		{
			std::size_t Count = 0;
			std::size_t Cursor = Begin;
			bool IntegerInputs = true;
			bool HasFormatting = false;
			for (unsigned Step = 0; Step < MaxAppendOperations && Cursor < Code.size(); ++Step)
			{
				const auto Op = static_cast<EOpcode>(Code[Cursor]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (Size == 0u || Size > Code.size() - Cursor) return false;
				if (Op == EOpcode::Add && Count == 1u && Code.size() - Cursor >= 4u && static_cast<EOpcode>(Code[Cursor + 1u]) == EOpcode::StoreLocal && Bytecode::ReadU16LE(Code.data() + Cursor + 2u) == Slot)
				{
					return true;
				}
				if (Op == EOpcode::LoadLocal || Op == EOpcode::ConstI32 || Op == EOpcode::ConstStr)
				{
					if constexpr (bNewText)
					{
						const auto Index = Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
						if (Op == EOpcode::LoadLocal)
						{
							if (Index >= Locals.size()) return false;
							IntegerInputs = IntegerInputs && (std::holds_alternative<std::int32_t>(Locals[Index]) ||
							    std::holds_alternative<std::int64_t>(Locals[Index]));
						}
						else if (Op == EOpcode::ConstI32)
						{
							if (Index >= Module.Constants.size()) return false;
							IntegerInputs = IntegerInputs && (Module.Constants[Index].Kind == EConstantKind::I32 ||
							    Module.Constants[Index].Kind == EConstantKind::I64);
						}
						else
						{
							IntegerInputs = false;
						}
						if (HasFormatting && !IntegerInputs) return false;
					}
					++Count;
				}
				else if (bNewText && Count >= 1u && Op == EOpcode::CallBuiltin &&
				    static_cast<EBuiltinId>(Code[Cursor + 1u]) == EBuiltinId::ToString &&
				    Code[Cursor + 2u] == 1u)
				{
					// Integer formatting has no callback, mutation or capability gate.
					// Reject unsupported inputs before the value probe copies any text.
					if (!IntegerInputs) return false;
					HasFormatting = true;
				}
				else if (Count >= 2u && (Op == EOpcode::GetIndexCopy || Op == EOpcode::Add || Op == EOpcode::Sub || Op == EOpcode::Mul || Op == EOpcode::Mod))
				{
					if (Op == EOpcode::GetIndexCopy)
					{
						IntegerInputs = false;
						if (HasFormatting) return false;
					}
					--Count;
				}
				else
				{
					return false;
				}
				Cursor += Size;
			}
			return false;
		}

		// Probe a small, read-only RHS without exposing an intermediate lhs copy.
		// Only intrinsic integer formatting calls are admitted; no references,
		// writes, branches, callbacks or user-owned values are evaluated.
		// Failed arithmetic/index probes leave storage and IP untouched so the VM
		// reports the ordinary error at its original instruction.
		template <bool bNewText>
		bool TryAppendLocal(FValue &Target, const FBytecodeModule &Module,
		    const std::vector<std::uint8_t> &Code, std::size_t &Next,
		    const std::uint16_t Slot, const std::vector<FValue> &Locals,
		    const std::size_t StackRoom)
		{
			std::array<FValue, 8> Values;
			std::size_t Count = 0;
			std::size_t Cursor = Next;
			for (unsigned Step = 0; Step < MaxAppendOperations && Cursor < Code.size(); ++Step)
			{
				const auto Op = static_cast<EOpcode>(Code[Cursor]);
				const auto Size = Bytecode::InstructionSize(Op);
				if (Size == 0u || Size > Code.size() - Cursor) return false;
				if (Op == EOpcode::Add && Count == 1u && Code.size() - Cursor >= 4u &&
				    static_cast<EOpcode>(Code[Cursor + 1u]) == EOpcode::StoreLocal &&
				    Bytecode::ReadU16LE(Code.data() + Cursor + 2u) == Slot)
				{
					if (auto *Text = std::get_if<std::string>(&Target))
					{
						const auto *Suffix = std::get_if<std::string>(&Values[0]);
						if (!Suffix) return false;
						Runtime::AppendString(*Text, *Suffix);
					}
					else if (auto *Text = std::get_if<std::u16string>(&Target))
					{
						const auto *Suffix = std::get_if<std::u16string>(&Values[0]);
						if (!Suffix) return false;
						Runtime::AppendString(*Text, *Suffix);
					}
					else
					{
						return false;
					}
					Next = Cursor + 4u;
					return true;
				}
				if (Op == EOpcode::LoadLocal || Op == EOpcode::ConstI32 || Op == EOpcode::ConstStr)
				{
					if (Count == Values.size() || StackRoom <= Count + 1u) return false;
					const auto Index = Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
					if (Op == EOpcode::LoadLocal)
					{
						if (Index >= Locals.size()) return false;
						const auto &Value = Locals[Index];
						if (!std::holds_alternative<std::int32_t>(Value) &&
						    !(bNewText && std::holds_alternative<std::int64_t>(Value)) &&
						    !std::holds_alternative<std::string>(Value) &&
						    !std::holds_alternative<std::u16string>(Value) &&
						    !std::holds_alternative<FDynamicArrayHandle>(Value))
						{
							return false;
						}
						Values[Count++] = Value;
					}
					else
					{
						if (Index >= Module.Constants.size()) return false;
						const auto &Constant = Module.Constants[Index];
						if ((Op == EOpcode::ConstI32 && Constant.Kind != EConstantKind::I32 && !(bNewText && Constant.Kind == EConstantKind::I64)) ||
						    (Op == EOpcode::ConstStr && Constant.Kind != EConstantKind::Str &&
						        Constant.Kind != EConstantKind::WStr))
						{
							return false;
						}
						if ((Constant.Kind == EConstantKind::Str &&
						        Constant.StringIndex >= Module.StringPool.size()) ||
						    (Constant.Kind == EConstantKind::WStr &&
						        Constant.StringIndex >= Module.WStringPool.size()))
						{
							return false;
						}
						// The shape and pool bounds above already prove these exact
						// alternatives. Materializing them needs no Core service call.
						if (Constant.Kind == EConstantKind::I32)
						{
							Values[Count++] = Constant.I32;
						}
						else if (Constant.Kind == EConstantKind::I64)
						{
							Values[Count++] = Constant.I64;
						}
						else if (Constant.Kind == EConstantKind::Str)
						{
							Values[Count++] = Module.StringPool[Constant.StringIndex];
						}
						else
						{
							Values[Count++] = Module.WStringPool[Constant.StringIndex];
						}
					}
				}
				else if (bNewText && Op == EOpcode::CallBuiltin && Count >= 1u &&
				    static_cast<EBuiltinId>(Code[Cursor + 1u]) == EBuiltinId::ToString &&
				    Code[Cursor + 2u] == 1u)
				{
					// Match Core's integer ValueToString formatting exactly. Other
					// conversions retain the ordinary builtin and snapshot semantics.
					auto& Value = Values[Count - 1u];
					if (const auto* Integer = std::get_if<std::int32_t>(&Value))
					{
						Value = std::to_string(*Integer);
					}
					else if (const auto* Integer = std::get_if<std::int64_t>(&Value))
					{
						Value = std::to_string(*Integer);
					}
					else
					{
						return false;
					}
				}
				else if (Op == EOpcode::GetIndexCopy && Count >= 2u)
				{
					const auto *Array = std::get_if<FDynamicArrayHandle>(&Values[Count - 2u]);
					const auto *Index = std::get_if<std::int32_t>(&Values[Count - 1u]);
					if (!Array || !*Array || !Index || *Index < 0 ||
					    ((*Array)->ElementTypeName != "string" && (*Array)->ElementTypeName != "wstring"))
					{
						return false;
					}
					FValue Element;
					if (!(*Array)->TryGetScalar(static_cast<std::size_t>(*Index), Element)) return false;
					Values[Count - 2u] = std::move(Element);
					Values[--Count] = std::int32_t{0};
				}
				else if (Count >= 2u)
				{
					const auto *Left = std::get_if<std::int32_t>(&Values[Count - 2u]);
					const auto *Right = std::get_if<std::int32_t>(&Values[Count - 1u]);
					if (!Left || !Right) return false;
					std::int32_t Result = 0;
					bool Ok = false;
					switch (Op)
					{
					case EOpcode::Add:
						{
							Ok = Runtime::CheckedAdd(*Left, *Right, Result);
							break;
						}
					case EOpcode::Sub:
						{
							Ok = Runtime::CheckedSub(*Left, *Right, Result);
							break;
						}
					case EOpcode::Mul:
						{
							Ok = Runtime::CheckedMul(*Left, *Right, Result);
							break;
						}
					case EOpcode::Mod:
						{
							if (*Right == 0 ||
							    (*Left == std::numeric_limits<std::int32_t>::min() && *Right == -1))
							{
								return false;
							}
							Result = *Left % *Right;
							Ok = true;
							break;
						}
					default:
						return false;
					}
					if (!Ok) return false;
					Values[Count - 2u] = Result;
					Values[--Count] = std::int32_t{0};
				}
				else
				{
					return false;
				}
				Cursor += Size;
			}
			return false;
		}
	} // namespace

	bool TryFuseLength(const FValue &Value, const std::vector<std::uint8_t> &Code, std::size_t &Next,
	    std::int64_t &OutLength) noexcept
	{
		if (Next > Code.size() || Code.size() - Next < 3u ||
		    static_cast<EOpcode>(Code[Next]) != EOpcode::CallBuiltin ||
		    static_cast<EBuiltinId>(Code[Next + 1u]) != EBuiltinId::Len || Code[Next + 2u] != 1u ||
		    !Runtime::TryGetStringLength(Value, OutLength))
		{
			return false;
		}
		Next += 3u;
		return true;
	}

	template <bool bNewText>
	EFusionResult TryFuseLoadImpl(FValue &Value, const FBytecodeModule &Module,
	    const std::vector<std::uint8_t> &Code, std::size_t &Next,
	    const std::uint16_t Slot, const std::vector<FValue> &Locals,
	    const std::size_t StackRoom, std::int64_t &OutLength)
	{
		if (Next > Code.size() || StackRoom == 0u) return EFusionResult::None;
		if (TryFuseLength(Value, Code, Next, OutLength))
		{
			return EFusionResult::Length;
		}
		// A completely consumed literal RHS has no observable intermediate value.
		// The caller excludes governed/debug execution and non-string local storage.
		if (Code.size() - Next >= 7u && StackRoom >= 2u &&
		    static_cast<EOpcode>(Code[Next]) == EOpcode::ConstStr &&
		    static_cast<EOpcode>(Code[Next + 3u]) == EOpcode::Add &&
		    static_cast<EOpcode>(Code[Next + 4u]) == EOpcode::StoreLocal &&
		    Bytecode::ReadU16LE(Code.data() + Next + 5u) == Slot)
		{
			const auto Index = Bytecode::ReadU16LE(Code.data() + Next + 1u);
			if (Index < Module.Constants.size() &&
			    Runtime::TryAppendStringConstant(Value, Module, Module.Constants[Index]))
			{
				Next += 7u;
				return EFusionResult::Append;
			}
		}
		// A complete local RHS has no intervening call, branch or write. Borrow
		// it until append commits instead of copying a potentially long string
		// into the generic expression's temporary FValue stack. Self-append keeps
		// the same standard-string alias guarantees as the constant path above.
		if (Code.size() - Next >= 7u && StackRoom >= 2u &&
		    static_cast<EOpcode>(Code[Next]) == EOpcode::LoadLocal &&
		    static_cast<EOpcode>(Code[Next + 3u]) == EOpcode::Add &&
		    static_cast<EOpcode>(Code[Next + 4u]) == EOpcode::StoreLocal &&
		    Bytecode::ReadU16LE(Code.data() + Next + 5u) == Slot)
		{
			const auto Source = Bytecode::ReadU16LE(Code.data() + Next + 1u);
			if (Source < Locals.size())
			{
				if (auto* Text = std::get_if<std::string>(&Value))
				{
					if (const auto* Suffix = std::get_if<std::string>(&Locals[Source]))
					{
						Runtime::AppendString(*Text, *Suffix);
						Next += 7u;
						return EFusionResult::Append;
					}
				}
				else if (auto* Text = std::get_if<std::u16string>(&Value))
				{
					if (const auto* Suffix = std::get_if<std::u16string>(&Locals[Source]))
					{
						Runtime::AppendString(*Text, *Suffix);
						Next += 7u;
						return EFusionResult::Append;
					}
				}
			}
		}
		if constexpr (bNewText)
		{
			// ToString is already an identity for an owned narrow string. Consume
			// only its complete local-to-local append tail; no call, reference or
			// mutation can intervene before the final assignment.
			constexpr auto CallOffset = Bytecode::InstructionSize(EOpcode::LoadLocal);
			constexpr auto AddOffset = CallOffset + Bytecode::InstructionSize(EOpcode::CallBuiltin);
			constexpr auto StoreOffset = AddOffset + Bytecode::InstructionSize(EOpcode::Add);
			constexpr auto TailSize = StoreOffset + Bytecode::InstructionSize(EOpcode::StoreLocal);
			if (Code.size() - Next >= TailSize && StackRoom >= 2u &&
				static_cast<EOpcode>(Code[Next]) == EOpcode::LoadLocal)
			{
				const auto SourceSlot = Bytecode::ReadU16LE(Code.data() + Next + 1u);
				auto* Target = std::get_if<std::string>(&Value);
				const auto* Source = SourceSlot < Locals.size() ? std::get_if<std::string>(&Locals[SourceSlot]) : nullptr;
				if (Target && Source &&
					static_cast<EOpcode>(Code[Next + CallOffset]) == EOpcode::CallBuiltin &&
					static_cast<EBuiltinId>(Code[Next + CallOffset + 1u]) == EBuiltinId::ToString && Code[Next + CallOffset + 2u] == 1u &&
					static_cast<EOpcode>(Code[Next + AddOffset]) == EOpcode::Add &&
					static_cast<EOpcode>(Code[Next + StoreOffset]) == EOpcode::StoreLocal &&
					Bytecode::ReadU16LE(Code.data() + Next + StoreOffset + 1u) == Slot)
				{
					if (Source->size() > Target->max_size() - Target->size()) return EFusionResult::None;
					try {Runtime::AppendString(*Target, *Source);}
					catch (const std::bad_alloc &)
					{
						return EFusionResult::None;
					}
					catch (const std::length_error &)
					{
						return EFusionResult::None;
					}
					// append handles self-aliasing and leaves the target unchanged on
					// failure. Declines keep the original instruction and diagnostics.
					Next += TailSize;
					return EFusionResult::Append;
				}
			}
		}
		// Most string reads are not append statements. Reject their opcode shape
		// before constructing temporary FValues or copying any RHS operands.
		if (StackRoom >= 2u && HasAppendLocalShape<bNewText>(Code, Next, Slot, Module, Locals) && TryAppendLocal<bNewText>(Value, Module, Code, Next, Slot, Locals, StackRoom))
		{
			return EFusionResult::Append;
		}
		return EFusionResult::None;
	}

	bool TryFuseCharCode(const FValue& Value, const FBytecodeModule& Module,
	    const std::vector<std::uint8_t>& Code, std::size_t& Next,
	    const std::vector<FValue>& Locals, const std::size_t StackRoom,
	    const bool AllowWrapper, std::int32_t& OutCode, bool& OutWrapper) noexcept
	{
		const auto* Text = std::get_if<std::string>(&Value);
		if (!Text || StackRoom < 2u || Next >= Code.size()) return false;
		std::size_t Cursor = Next;
		const auto Op = static_cast<EOpcode>(Code[Cursor]);
		if (Op != EOpcode::LoadLocal && Op != EOpcode::ConstI32) return false;
		constexpr std::size_t LoadSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
		if (Code.size() - Cursor < LoadSize) return false;
		const auto Index = Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
		std::int64_t Position = 0;
		if (Op == EOpcode::LoadLocal)
		{
			if (Index >= Locals.size()) return false;
			if (const auto *I32 = std::get_if<std::int32_t>(&Locals[Index]))
			{
				Position = *I32;
			}
			else if (const auto *I64 = std::get_if<std::int64_t>(&Locals[Index]))
			{
				Position = *I64;
			}
			else
			{
				return false;
			}
		}
		else
		{
			if (Index >= Module.Constants.size()) return false;
			const auto& Constant = Module.Constants[Index];
			if (Op == EOpcode::ConstI32 && Constant.Kind == EConstantKind::I32)
			{
				Position = Constant.I32;
			}
			else if (Op == EOpcode::ConstI32 && Constant.Kind == EConstantKind::I64)
			{
				Position = Constant.I64;
			}
			else
			{
				return false;
			}
		}
		Cursor += LoadSize;
		// Parameter coercion is itself pure for these two exact integer kinds.
		constexpr std::size_t BuiltinSize = Bytecode::InstructionSize(EOpcode::CallBuiltin);
		if (Code.size() - Cursor >= BuiltinSize && static_cast<EOpcode>(Code[Cursor]) == EOpcode::CallBuiltin &&
		    static_cast<EBuiltinId>(Code[Cursor + 1u]) == EBuiltinId::ToInt64 && Code[Cursor + 2u] == 1u)
		{
			Cursor += BuiltinSize;
		}
		if (Code.size() - Cursor < BuiltinSize) return false;
		std::size_t End = Cursor;
		bool Wrapper = false;
		if (static_cast<EOpcode>(Code[Cursor]) == EOpcode::CallBuiltin)
		{
			if (static_cast<EBuiltinId>(Code[Cursor + 1u]) != EBuiltinId::CharCode || Code[Cursor + 2u] != 2u) return false;
			End += BuiltinSize;
		}
		else if (AllowWrapper && static_cast<EOpcode>(Code[Cursor]) == EOpcode::Call &&
		    Code.size() - Cursor >= Bytecode::InstructionSize(EOpcode::Call) && Code[Cursor + 3u] == 2u)
		{
			const auto FunctionIndex = Bytecode::ReadU16LE(Code.data() + Cursor + 1u);
			if (FunctionIndex >= Module.Functions.size()) return false;
			const auto& Function = Module.Functions[FunctionIndex];
			constexpr auto BodySize = 2u * LoadSize + BuiltinSize + Bytecode::InstructionSize(EOpcode::Return);
			if (Function.ParamCount != 2u || Function.LocalCount != 2u || Function.bIsMethod ||
				Function.bReturnsVoid || Function.bReturnsRef || Function.bDeleted ||
				!Function.UnavailableReason.empty() || !Function.Captures.empty() ||
				!Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function) ||
				Function.Code.size() < BodySize || Function.Code.size() > BodySize + 4u)
			{
				return false;
			}
			const auto& Body = Function.Code;
			const auto First = Body.size() - BodySize;
			for (std::size_t Offset = 0; Offset < First; ++Offset)
			{
				if (static_cast<EOpcode>(Body[Offset]) != EOpcode::Nop) return false;
			}
			if (static_cast<EOpcode>(Body[First]) != EOpcode::LoadLocal ||
				Bytecode::ReadU16LE(Body.data() + First + 1u) != 0u ||
				static_cast<EOpcode>(Body[First + LoadSize]) != EOpcode::LoadLocal ||
				Bytecode::ReadU16LE(Body.data() + First + LoadSize + 1u) != 1u ||
				static_cast<EOpcode>(Body[First + 2u * LoadSize]) != EOpcode::CallBuiltin ||
				static_cast<EBuiltinId>(Body[First + 2u * LoadSize + 1u]) != EBuiltinId::CharCode ||
				Body[First + 2u * LoadSize + 2u] != 2u ||
				static_cast<EOpcode>(Body.back()) != EOpcode::Return)
			{
				return false;
			}
			End += Bytecode::InstructionSize(EOpcode::Call);
			Wrapper = true;
		}
		else
		{
			return false;
		}
		// No call, reference or mutation can intervene between reading the text
		// and its index. Invalid indices retain the canonical error and location.
		if (Position < 0 || static_cast<std::uint64_t>(Position) >= Text->size()) return false;
		OutCode = static_cast<std::int32_t>(static_cast<unsigned char>((*Text)[static_cast<std::size_t>(Position)]));
		OutWrapper = Wrapper;
		Next = End;
		return true;
	}

	bool TryFuseIdentity(const FBytecodeModule& Module, const std::vector<std::uint8_t>& Code,
	    std::size_t& Next, const std::uint16_t Slot, const std::size_t StackRoom, bool& OutInPlace) noexcept
	{
		constexpr auto CallSize = Bytecode::InstructionSize(EOpcode::Call);
		constexpr auto LoadSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
		constexpr auto BodySize = LoadSize + Bytecode::InstructionSize(EOpcode::Return);
		if (!StackRoom || Next > Code.size() || Code.size() - Next < CallSize ||
			static_cast<EOpcode>(Code[Next]) != EOpcode::Call || Code[Next + 3u] != 1u)
		{
			return false;
		}
		const auto FunctionIndex = Bytecode::ReadU16LE(Code.data() + Next + 1u);
		if (FunctionIndex >= Module.Functions.size()) return false;
		const auto& Function = Module.Functions[FunctionIndex];
		if (Function.ParamCount != 1u || Function.LocalCount != 1u || Function.bIsMethod ||
			Function.bReturnsVoid || Function.bReturnsRef || Function.bDeleted ||
			!Function.UnavailableReason.empty() || !Function.Captures.empty() ||
			!Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function) ||
			Function.Code.size() < BodySize || Function.Code.size() > BodySize + 4u)
		{
			return false;
		}
		const auto& Body = Function.Code;
		const auto First = Body.size() - BodySize;
		for (std::size_t Offset = 0; Offset < First; ++Offset)
		{
			if (static_cast<EOpcode>(Body[Offset]) != EOpcode::Nop) return false;
		}
		if (static_cast<EOpcode>(Body[First]) != EOpcode::LoadLocal ||
			Bytecode::ReadU16LE(Body.data() + First + 1u) != 0u ||
			static_cast<EOpcode>(Body.back()) != EOpcode::Return)
		{
			return false;
		}
		auto End = Next + CallSize;
		OutInPlace = Code.size() - End >= LoadSize && static_cast<EOpcode>(Code[End]) == EOpcode::StoreLocal &&
		    Bytecode::ReadU16LE(Code.data() + End + 1u) == Slot;
		if (OutInPlace)
		{
			End += LoadSize;
		}
		Next = End;
		return true;
	}

	// A forwarding leaf cannot observe its local values after their final
	// load. Keep the canonical builtin and return (including their errors).
	template <bool bNewText>
	bool CanTransferTextArgumentImpl(const FValue& Value, const FBytecodeFunction& Function,
	    std::size_t Next, std::size_t StackRoom) noexcept
	{
		const auto& Code = Function.Code;
		constexpr auto LoadSize = Bytecode::InstructionSize(EOpcode::LoadLocal);
		// The final value load of an identity leaf has no remaining observer.
		// Transfer its owned string instead of copying it once more on return.
		constexpr auto IdentitySize = LoadSize + Bytecode::InstructionSize(EOpcode::Return);
		if (bNewText && Function.ParamCount == 1u && Function.LocalCount == 1u && !Function.bIsMethod &&
		    !Function.bReturnsVoid && !Function.bReturnsRef && Function.Captures.empty() &&
		    Function.ExceptionRegions.empty() && !BytecodeFunctionHasRefParams(Function) &&
		    Code.size() >= IdentitySize && Code.size() <= IdentitySize + 4u &&
		    Next == Code.size() - 1u && StackRoom >= 1u)
		{
			const auto First = Code.size() - IdentitySize;
			bool PrefixIsEmpty = true;
			for (std::size_t Offset = 0; Offset < First; ++Offset)
			{
				PrefixIsEmpty = PrefixIsEmpty && static_cast<EOpcode>(Code[Offset]) == EOpcode::Nop;
			}
			if (PrefixIsEmpty && static_cast<EOpcode>(Code[First]) == EOpcode::LoadLocal &&
				Bytecode::ReadU16LE(Code.data() + First + 1u) == 0u &&
				static_cast<EOpcode>(Code.back()) == EOpcode::Return)
			{
				return true;
			}
		}
		constexpr auto CallSize = Bytecode::InstructionSize(EOpcode::CallBuiltin);
		constexpr auto BodySize = 2u * LoadSize + CallSize + Bytecode::InstructionSize(EOpcode::Return);
		if (Function.ParamCount != 2u || Function.LocalCount != 2u || Code.size() < BodySize ||
		    Code.size() > BodySize + 4u || Function.bIsMethod || Function.bReturnsRef ||
		    !Function.Captures.empty() || !Function.ExceptionRegions.empty() || BytecodeFunctionHasRefParams(Function))
		{
			return false;
		}

		const auto* Text = std::get_if<std::string>(&Value);
		if (!Text || Text->size() < 64u) return false;

		const std::size_t First = Code.size() - BodySize;
		const std::size_t Second = First + LoadSize;
		const std::size_t Call = Second + LoadSize;
		if ((Next != Second || StackRoom < 2u) && (Next != Call || StackRoom < 1u)) return false;
		if (static_cast<EOpcode>(Code[First]) != EOpcode::LoadLocal ||
		    static_cast<EOpcode>(Code[Second]) != EOpcode::LoadLocal ||
		    static_cast<EOpcode>(Code[Call]) != EOpcode::CallBuiltin ||
		    Code[Call + 2u] != 2u || static_cast<EOpcode>(Code[Call + CallSize]) != EOpcode::Return)
		{
			return false;
		}
		const auto FirstSlot = Bytecode::ReadU16LE(Code.data() + First + 1u);
		const auto SecondSlot = Bytecode::ReadU16LE(Code.data() + Second + 1u);
		if (FirstSlot > 1u || SecondSlot > 1u || FirstSlot == SecondSlot) return false;
		for (std::size_t Offset = 0; Offset < First; ++Offset)
		{
			if (static_cast<EOpcode>(Code[Offset]) != EOpcode::Nop) return false;
		}
		// These text queries only use operand values and canonical work accounting.
		// In plain execution they cannot reenter or observe a moved-from local.
		const auto Builtin = static_cast<EBuiltinId>(Code[Call + 1u]);
		return (bNewText && Builtin == EBuiltinId::CharCode) || Builtin == EBuiltinId::Find || Builtin == EBuiltinId::StringLastFind ||
		    Builtin == EBuiltinId::StringStartsWith || Builtin == EBuiltinId::StringEndsWith;
	}

	EFusionResult TryFuseLoad(FValue& V, const FBytecodeModule& M, const std::vector<std::uint8_t>& C,
	    std::size_t& N, std::uint16_t S, const std::vector<FValue>& L, std::size_t R, std::int64_t& O)
	{
		return TryFuseLoadImpl<false>(V, M, C, N, S, L, R, O);
	}
#if defined(_MSC_VER) && defined(_M_X64)
	__declspec(code_seg(".vstext"))
#endif
	EFusionResult TryFuseTextLoad(FValue& V, const FBytecodeModule& M, const std::vector<std::uint8_t>& C,
	    std::size_t& N, std::uint16_t S, const std::vector<FValue>& L, std::size_t R, std::int64_t& O)
	{
		return TryFuseLoadImpl<true>(V, M, C, N, S, L, R, O);
	}
	bool CanTransferTextArgument(const FValue& V, const FBytecodeFunction& F, std::size_t N, std::size_t R) noexcept
	{
		return CanTransferTextArgumentImpl<false>(V, F, N, R);
	}
#if defined(_MSC_VER) && defined(_M_X64)
	__declspec(code_seg(".vstext"))
#endif
	bool CanTransferOptimizedTextArgument(const FValue& V, const FBytecodeFunction& F, std::size_t N, std::size_t R) noexcept
	{
		return CanTransferTextArgumentImpl<true>(V, F, N, R);
	}

} // namespace VeroScript::VmStringLocals
