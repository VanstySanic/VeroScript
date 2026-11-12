// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitText.h"
#include "VeroScriptRuntime.h"

#include <functional>
#include <charconv>
#include <new>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace VeroScript::AsmJit::JitLinear
{
	namespace
	{
		using K = ETypedLoopLocalKind;

		const FTypedLoopNativeStringView* ReadView(const std::uint64_t Token) noexcept
		{
			return (Token & TypedLoopNativeStringTag) != 0u
				? reinterpret_cast<const FTypedLoopNativeStringView*>(
					static_cast<std::uintptr_t>(Token & TypedLoopNativePayloadMask)) : nullptr;
		}

		std::uint8_t* HashFlag(FTypedLoopNativeStringView* Begin,
			const std::size_t Count, std::uint8_t* Flags,
			const FTypedLoopNativeStringView* View) noexcept
		{
			if (!Flags || !Begin) return nullptr;
			const auto Address = reinterpret_cast<std::uintptr_t>(View);
			const auto Base = reinterpret_cast<std::uintptr_t>(Begin);
			if (Address < Base) return nullptr;
			const auto Delta = Address - Base;
			if (Delta % sizeof(*Begin) != 0u || Delta / sizeof(*Begin) >= Count) return nullptr;
			return Flags + Delta / sizeof(*Begin);
		}

		void Publish(FTypedLoopNativeStringView& View, const std::string& Value,
			std::uint8_t* const Valid) noexcept
		{
			View = {&Value, Value.data(), Value.size(), 0u};
			if (Valid)
			{
				*Valid = 0u;
			}
		}

		std::int32_t Fail(FTypedLoopNativeContext* const Context,
			const std::uint32_t Offset, const char* const Message) noexcept
		{
			if (Context)
			{
				Context->FailureOffset = Offset;
				Context->FailureMessage = Message;
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::size_t ResolveNativeTextHash(FTypedLoopNativeContext& Context,
		const FTypedLoopNativeStringView& View) noexcept
	{
		auto& Scratch = *Context.Strings;
		auto* Valid = HashFlag(Scratch.LocalViews, Scratch.LocalCount,
			Scratch.LocalHashValid, &View);
		if (!Valid)
		{
			Valid = HashFlag(Scratch.TemporaryViews, Scratch.TemporaryCount,
				Scratch.TemporaryHashValid, &View);
		}
		if (!Valid || *Valid != 0u) return View.Hash;
		const auto Hash = View.Value ? std::hash<std::string>{}(*View.Value) : 0u;
		// Only invocation-local mutable views have a validity bitmap. Literal and
		// shared immutable views never enter this cache write.
		const_cast<FTypedLoopNativeStringView&>(View).Hash = Hash;
		*Valid = 1u;
		return Hash;
	}

	FTypedLoopTextBufferPool::FTypedLoopTextBufferPool(const std::size_t Count)
		: Values(Count), Views(Count), Kinds(Count), HashValid(Count, 0u)
	{
		for (auto &Value : Values)
		{
			Value.reserve(21u);
		}
	}

	std::size_t FTypedLoopTextBufferPool::RetainedBytes() const noexcept
	{
		std::size_t Size = sizeof(*this) + Values.capacity() * sizeof(std::string)
			+ Views.capacity() * sizeof(FTypedLoopNativeStringView)
			+ Kinds.capacity() * sizeof(K) + HashValid.capacity();
		for (const auto &Value : Values)
		{
			Size += Value.capacity() + 1u;
		}
		return Size;
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeStoreText(
		FTypedLoopNativeContext* const Context, const std::uint32_t InstructionIndex,
		const std::uint64_t LeftToken, const std::uint64_t RightToken) noexcept
	{
		if (!Context || !Context->Strings || !Context->Bindings ||
			!Context->Strings->Instructions || InstructionIndex >= Context->Strings->InstructionCount)
		{
			return Fail(Context, InvalidJitInstructionOffset, "JIT typed loop: invalid text store");
		}
		auto& Scratch = *Context->Strings;
		const auto& Instruction = Scratch.Instructions[InstructionIndex];
		const auto Slot = Instruction.Operand0;
		const auto Offset = Instruction.bTextConcatStore
			? static_cast<std::uint32_t>(Instruction.Immediate) : Instruction.BytecodeOffset;
		if (Slot >= Scratch.LocalCount || !Scratch.LocalViews ||
			Context->Bindings[Slot].Kind != K::String || !Context->Bindings[Slot].Storage)
		{
			return Fail(Context, Offset, "JIT typed loop: invalid string assignment");
		}
		char Buffer[21];
		const FTypedLoopNativeStringView* Left = nullptr;
		const FTypedLoopNativeStringView* Right = nullptr;
		std::string_view Formatted;
		const bool HasFormat = Instruction.TextFormatInstruction != 0xFFFFu;
		if (HasFormat)
		{
			const auto Pc = Instruction.TextFormatInstruction;
			if (Pc >= Scratch.InstructionCount || !Context->InstructionKinds)
			{
				return Fail(Context, Offset, "JIT typed loop: invalid integer format");
			}
			const auto Bits = Instruction.bTextConcatStore ? RightToken : LeftToken;
			std::to_chars_result Result{};
			switch (Context->InstructionKinds[Pc])
			{
			case K::I32:
				{
					Result = std::to_chars(Buffer, Buffer + sizeof(Buffer), static_cast<std::int32_t>(Bits));
					break;
				}
			case K::U32:
				{
					Result = std::to_chars(Buffer, Buffer + sizeof(Buffer), static_cast<std::uint32_t>(Bits));
					break;
				}
			case K::I64:
				{
					Result = std::to_chars(Buffer, Buffer + sizeof(Buffer), static_cast<std::int64_t>(Bits));
					break;
				}
			case K::U64:
				{
					Result = std::to_chars(Buffer, Buffer + sizeof(Buffer), Bits);
					break;
				}
			default:
				return Fail(Context, Offset, "JIT typed loop: unsupported string conversion");
			}
			if (Result.ec != std::errc{}) return Fail(Context, Offset, "JIT typed loop: integer formatting failed");
			Formatted = std::string_view(Buffer, static_cast<std::size_t>(Result.ptr - Buffer));
		}
		if (!HasFormat || Instruction.bTextConcatStore)
		{
			Left = ReadView(LeftToken);
			if (!Left || !Left->Value) return Fail(Context, Offset, "JIT typed loop: invalid borrowed string");
		}
		if (Instruction.bTextConcatStore && !HasFormat)
		{
			Right = ReadView(RightToken);
			if (!Right || !Right->Value) return Fail(Context, Offset, "JIT typed loop: invalid borrowed string");
		}
		try
		{
			auto& Target = *static_cast<std::string*>(Context->Bindings[Slot].Storage);
			if (!Instruction.bTextConcatStore)
			{
				Target.assign(Formatted.data(), Formatted.size());
			}
			else
			{
				const auto RightSize = HasFormat ? Formatted.size() : Right->Size;
				if (RightSize > Target.max_size() || Left->Size > Target.max_size() - RightSize)
				{
					return Fail(Context, Offset, "string length overflow");
				}
				if (&Target == Left->Value)
				{
					if (HasFormat)
					{
						Runtime::AppendString(Target, Formatted);
					}
					else
					{
						Runtime::AppendString(Target, *Right->Value);
					}
				}
				else if (!HasFormat && &Target == Right->Value)
				{
					// Insertion and self-append preserve their inputs across reallocation.
					Target.insert(0u, *Left->Value);
				}
				else
				{
					// Reserve before changing content. Once it succeeds, neither append
					// can allocate; failures retain the original destination snapshot.
					Target.reserve(Left->Size + RightSize);
					Target.assign(*Left->Value);
					if (HasFormat)
					{
						Runtime::AppendString(Target, Formatted);
					}
					else
					{
						Runtime::AppendString(Target, *Right->Value);
					}
				}
			}
			auto& View = Scratch.LocalViews[Slot];
			Publish(View, Target, Scratch.LocalHashValid ? Scratch.LocalHashValid + Slot : nullptr);
			Context->FailureOffset = Offset;
			Context->FailureMessage = nullptr;
			Context->Scratch = reinterpret_cast<std::uint64_t>(&View) | TypedLoopNativeStringTag;
			return 0;
		}
		catch (const std::bad_alloc&)
		{
			return Fail(Context, Offset, "out of memory");
		}
		catch (...)
		{
			return Fail(Context, Offset, "string concatenation failed");
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeTextOperation(
		FTypedLoopNativeContext* const Context, const std::uint32_t Instruction,
		const std::uint64_t Left, const std::uint64_t Right) noexcept
	{
		if (!Context || !Context->Strings || Instruction >= Context->Strings->InstructionCount)
		{
			return Fail(Context, InvalidJitInstructionOffset, "JIT typed loop: invalid text operation");
		}
		return TypedLoopNativeCharacterCode(Context, Left, Right,
			Context->InstructionKinds[Instruction], Context->Strings->Instructions[Instruction].BytecodeOffset);
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeCharacterCode(
		FTypedLoopNativeContext* const Context, const std::uint64_t TextToken,
		const std::uint64_t IndexBits, const K IndexKind, const std::uint32_t Offset) noexcept
	{
		const auto* Text = ReadView(TextToken);
		if (!Context || !Text || !Text->Value)
		{
			return Fail(Context, Offset, "JIT typed loop: invalid borrowed string");
		}
		std::uint64_t Index = IndexBits;
		switch (IndexKind)
		{
		case K::I32:
			{
				Index = static_cast<std::uint64_t>(static_cast<std::int64_t>(static_cast<std::int32_t>(IndexBits)));
				break;
			}
		case K::U32:
			{
				Index = static_cast<std::uint32_t>(IndexBits);
				break;
			}
		case K::I64:
		case K::U64:
			{
				break;
			}
		default:
			return Fail(Context, Offset, "__char_code expects (string, int64)");
		}
		if (Index >= Text->Size || Index > std::numeric_limits<std::size_t>::max())
		{
			return Fail(Context, Offset, "char index out of range");
		}
		Context->FailureOffset = Offset;
		Context->FailureMessage = nullptr;
		Context->Scratch = static_cast<unsigned char>(Text->Data[static_cast<std::size_t>(Index)]);
		return 0;
	}
}
