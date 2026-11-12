// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptJitLinear.h"
#include "VeroScriptJitText.h"

#include "VeroScriptJitIr.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptRuntime.h"
#include "VeroScriptAsmJitExecutionContext.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <utility>

namespace VeroScript::AsmJit::JitLinear
{
	#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
	namespace
	{
		thread_local std::uint64_t NativeContainsCallCount = 0;
	}
	std::uint64_t TestNativeContainsCallCount() noexcept
	{
		return NativeContainsCallCount;
	}
	#endif
	bool TypedContainerMethodsHaveProvenStorage(
		const FTypedLoopPlan& Plan,
		const bool bFusedScalarStorageSafe,
		const bool bAllowScalarArrayStorage,
		const bool bAllowScalarMapStorage,
		const bool bAllowScalarSetStorage)
	{
		for (const FTypedLoopInstruction& Instruction : Plan.Instructions)
		{
			if (Instruction.Opcode != EOpcode::CallMethod
				|| Instruction.Immediate == 0)
			{
				continue;
			}
			const bool bProven = bFusedScalarStorageSafe
				|| (Instruction.Immediate == 1
					&& (bAllowScalarMapStorage || bAllowScalarSetStorage))
				|| (Instruction.Immediate == 2 && bAllowScalarArrayStorage)
				|| (Instruction.Immediate == 3 && bAllowScalarSetStorage);
			if (!bProven)
			{
				// Method spellings are not receiver types. A user class may legally
				// declare has/push/add; executing such a call through the container
				// opcode adapter after an earlier field/local write would turn a safe
				// fallback into a partially committed runtime-plan failure.
				return false;
			}
		}
		return true;
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeToString(
		FTypedLoopNativeContext* const Context, const std::uint32_t InstructionIndex,
		const std::uint64_t Argument, const std::uint32_t InstructionOffset) noexcept
	{
		if (!Context) return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		Context->FailureOffset = InstructionOffset;
		Context->FailureMessage = nullptr;
		if (!Context->Strings || !Context->InstructionKinds
			|| !Context->Strings->Temporaries || !Context->Strings->TemporaryViews
			|| InstructionIndex >= Context->Strings->TemporaryCount)
		{
			Context->FailureMessage = "JIT typed loop: invalid string temporary";
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
		try
		{
			// Twenty digits plus a sign cover every supported integer. Formatting is
			// locale-independent and preserves the complete unsigned 64-bit range.
			char Buffer[21];
			std::to_chars_result Converted{};
			switch (Context->Strings->ProducerKinds ? Context->Strings->ProducerKinds[InstructionIndex] : Context->InstructionKinds[InstructionIndex])
			{
			case ETypedLoopLocalKind::I32:
				{
					Converted = std::to_chars(Buffer, Buffer + sizeof(Buffer), static_cast<std::int32_t>(Argument));
					break;
				}
			case ETypedLoopLocalKind::U32:
				{
					Converted = std::to_chars(Buffer, Buffer + sizeof(Buffer), static_cast<std::uint32_t>(Argument));
					break;
				}
			case ETypedLoopLocalKind::I64:
				{
					Converted = std::to_chars(Buffer, Buffer + sizeof(Buffer), static_cast<std::int64_t>(Argument));
					break;
				}
			case ETypedLoopLocalKind::U64:
				{
					Converted = std::to_chars(Buffer, Buffer + sizeof(Buffer), Argument);
					break;
				}
			default:
				{
					Context->FailureMessage = "JIT typed loop: unsupported string conversion";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
			}
			if (Converted.ec != std::errc{})
			{
				Context->FailureMessage = "JIT typed loop: integer formatting failed";
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			auto& Value = Context->Strings->Temporaries[InstructionIndex];
			Value.assign(Buffer, Converted.ptr);
			auto& View = Context->Strings->TemporaryViews[InstructionIndex];
			View = {&Value, Value.data(), Value.size(), Context->Strings->TemporaryHashValid ? 0u : std::hash<std::string>{}(Value)};
			if (Context->Strings->TemporaryHashValid)
			{
				Context->Strings->TemporaryHashValid[InstructionIndex] = 0u;
			}
			Context->Scratch = reinterpret_cast<std::uint64_t>(&View) | TypedLoopNativeStringTag;
			return 0;
		}
		catch (...)
		{
			Context->FailureMessage = "JIT typed loop: string conversion failed";
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeStoreStringLocal(
		FTypedLoopNativeContext* const Context, const std::uint32_t Slot,
		const std::uint64_t SourceToken, const std::uint32_t InstructionOffset) noexcept
	{
		if (!Context) return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		Context->FailureOffset = InstructionOffset;
		Context->FailureMessage = nullptr;
		if (!Context->Strings || !Context->Bindings || !Context->Strings->LocalViews
			|| Slot >= Context->Strings->LocalCount
			|| (SourceToken & TypedLoopNativeStringTag) == 0u)
		{
			Context->FailureMessage = "JIT typed loop: invalid string assignment";
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
		try
		{
			const auto* const Source = reinterpret_cast<const FTypedLoopNativeStringView*>(
				static_cast<std::uintptr_t>(SourceToken & TypedLoopNativePayloadMask));
			auto& Binding = Context->Bindings[Slot];
			if (!Source || !Source->Value || Binding.Kind != ETypedLoopLocalKind::String || !Binding.Storage)
			{
				Context->FailureMessage = "JIT typed loop: invalid string assignment";
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			auto& Target = *static_cast<std::string*>(Binding.Storage);
			const std::size_t Hash = Source->Hash;
			// Commit at the original StoreLocal, never retain an owning temporary's
			// view in a frame local. Other string locals retain independent values.
			if (&Target != Source->Value)
			{
				Target = *Source->Value;
			}
			auto& View = Context->Strings->LocalViews[Slot];
			View = {&Target, Target.data(), Target.size(), Hash};
			if (Context->Strings->LocalHashValid)
			{
				Context->Strings->LocalHashValid[Slot] = 0u;
			}
			Context->Scratch = reinterpret_cast<std::uint64_t>(&View) | TypedLoopNativeStringTag;
			return 0;
		}
		catch (...)
		{
			Context->FailureMessage = "JIT typed loop: string assignment failed";
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeGetIndexI32(
		FTypedLoopNativeContext* const Context,
		const std::uint64_t ContainerToken,
		const std::uint64_t IndexToken,
		const std::uint32_t InstructionOffset) noexcept
	{
		try
		{
			if (!Context || ContainerToken == 0u)
			{
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			auto* const Binding = reinterpret_cast<FTypedLoopNativeBinding*>(
				static_cast<std::uintptr_t>(ContainerToken));
			Context->FailureOffset = InstructionOffset;
			Context->FailureMessage = nullptr;
			switch (Binding->Kind)
			{
			case ETypedLoopLocalKind::ArrayI32:
				{
					const std::int32_t Index = static_cast<std::int32_t>(IndexToken);
					auto& Values = *static_cast<std::vector<std::int32_t>*>(
						Binding->Storage);
					if (Index < 0 || static_cast<std::size_t>(Index) >= Values.size())
					{
						Context->FailureMessage = "array index out of bounds";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Context->Scratch = static_cast<std::uint32_t>(
						Values[static_cast<std::size_t>(Index)]);
					++Context->ArrayGets;
					return 0;
				}
			case ETypedLoopLocalKind::ArrayU32:
				{
					const std::uint32_t Index = static_cast<std::uint32_t>(IndexToken);
					auto& Values = *static_cast<std::vector<std::uint32_t>*>(
						Binding->Storage);
					if (static_cast<std::size_t>(Index) >= Values.size())
					{
						Context->FailureMessage = "array index out of bounds";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Context->Scratch = Values[static_cast<std::size_t>(Index)];
					++Context->ArrayGets;
					return 0;
				}
			case ETypedLoopLocalKind::ArrayObject:
				{
					const std::uint32_t Index = static_cast<std::uint32_t>(IndexToken);
					if (!Binding->NativeData
						|| static_cast<std::size_t>(Index) >= Binding->NativeSize)
					{
						Context->FailureMessage = "array index out of bounds";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					const auto* View = FindTypedLoopNativeObjectViewAt(
						static_cast<FTypedLoopNativeObjectView*>(Binding->NativeData), Index,
						Context->ObjectViewRanges, Context->ObjectViewRangeCount);
					if (!View)
					{
						Context->FailureMessage = "JIT typed loop: unprepared object view";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Context->Scratch = MakeTypedLoopNativeObjectViewToken(View);
					++Context->ArrayGets;
					return 0;
				}
			case ETypedLoopLocalKind::MapI32I32:
				{
					const std::int32_t Index = static_cast<std::int32_t>(IndexToken);
					auto& Values = *static_cast<FMap::FIntI32Entries*>(Binding->Storage);
					const std::int32_t* const Found = Values.Find(Index);
					if (!Found)
					{
						Context->FailureMessage = "map key not found";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Context->Scratch = static_cast<std::uint32_t>(*Found);
					++Context->MapGets;
					return 0;
				}
			case ETypedLoopLocalKind::MapI32U32:
				{
					const auto* const Found = static_cast<FMap::FIntU32Entries*>(
						Binding->Storage)
												  ->Find(static_cast<std::int32_t>(IndexToken));
					if (!Found)
					{
						Context->FailureMessage = "map key not found";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Context->Scratch = *Found;
					++Context->MapGets;
					return 0;
				}
			case ETypedLoopLocalKind::MapU32I32:
				{
					const auto* const Found = static_cast<FMap::FUIntI32Entries*>(
						Binding->Storage)
												  ->Find(static_cast<std::uint32_t>(IndexToken));
					if (!Found)
					{
						Context->FailureMessage = "map key not found";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Context->Scratch = static_cast<std::uint32_t>(*Found);
					++Context->MapGets;
					return 0;
				}
			case ETypedLoopLocalKind::MapU32U32:
				{
					const auto* const Found = static_cast<FMap::FUIntU32Entries*>(
						Binding->Storage)
												  ->Find(static_cast<std::uint32_t>(IndexToken));
					if (!Found)
					{
						Context->FailureMessage = "map key not found";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Context->Scratch = *Found;
					++Context->MapGets;
					return 0;
				}
			case ETypedLoopLocalKind::ArrayString:
				{
					const std::int32_t Index = static_cast<std::int32_t>(IndexToken);
					if (Index < 0 || !Binding->NativeData
						|| static_cast<std::size_t>(Index) >= Binding->NativeSize)
					{
						Context->FailureMessage = "array index out of bounds";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Context->Scratch = reinterpret_cast<std::uint64_t>(
						static_cast<FTypedLoopNativeStringView*>(Binding->NativeData)
							+ static_cast<std::size_t>(Index)) |
						TypedLoopNativeStringTag;
					++Context->ArrayGets;
					return 0;
				}
			case ETypedLoopLocalKind::MapStringI32:
				{
					if ((IndexToken & TypedLoopNativeStringTag) == 0u)
					{
						Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					const auto* const Key = reinterpret_cast<const FTypedLoopNativeStringView*>(
						static_cast<std::uintptr_t>(IndexToken & TypedLoopNativePayloadMask));
					if (!Key || !Key->Value)
					{
						Context->FailureMessage = "JIT typed loop: invalid borrowed string";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					const std::int32_t* const Found =
						static_cast<FMap::FStringI32Entries*>(Binding->Storage)->FindHashed(
							*Key->Value, NativeTextHash(*Context, *Key));
					if (!Found)
					{
						Context->FailureMessage = "map key not found";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Context->Scratch = static_cast<std::uint32_t>(*Found);
					++Context->MapGets;
					return 0;
				}
			default:
				{
					Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
			}

		}
		catch (...)
		{
			if (Context && Context->Vm)
			{
				Context->Vm->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeSetIndexI32(
		FTypedLoopNativeContext* const Context,
		const std::uint64_t ContainerToken,
		const std::uint64_t IndexToken,
		const std::int32_t Value,
		const std::uint32_t InstructionOffset) noexcept
	{
		try
		{
			if (!Context || ContainerToken == 0u)
			{
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			auto* const Binding = reinterpret_cast<FTypedLoopNativeBinding*>(
				static_cast<std::uintptr_t>(ContainerToken));
			Context->FailureOffset = InstructionOffset;
			Context->FailureMessage = nullptr;
			switch (Binding->Kind)
			{
			case ETypedLoopLocalKind::ArrayI32:
				{
					const std::int32_t Index = static_cast<std::int32_t>(IndexToken);
					auto& Values = *static_cast<std::vector<std::int32_t>*>(
						Binding->Storage);
					if (Index < 0 || static_cast<std::size_t>(Index) >= Values.size())
					{
						Context->FailureMessage = "array index out of bounds";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Values[static_cast<std::size_t>(Index)] = Value;
					++Context->ArraySets;
					Context->Scratch = ContainerToken;
					return 0;
				}
			case ETypedLoopLocalKind::ArrayU32:
				{
					const std::uint32_t Index = static_cast<std::uint32_t>(IndexToken);
					auto& Values = *static_cast<std::vector<std::uint32_t>*>(Binding->Storage);
					if (static_cast<std::size_t>(Index) >= Values.size())
					{
						Context->FailureMessage = "array index out of bounds";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					Values[static_cast<std::size_t>(Index)] = static_cast<std::uint32_t>(Value);
					++Context->ArraySets;
					Context->Scratch = ContainerToken;
					return 0;
				}
			case ETypedLoopLocalKind::MapI32I32:
				{
					try
					{
						static_cast<FMap::FIntI32Entries*>(Binding->Storage)->InsertOrAssign(
						static_cast<std::int32_t>(IndexToken), Value);
					}
					catch (const std::exception&)
					{
						Context->FailureMessage = "map index assignment failed";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					catch (...)
					{
						Context->FailureMessage = "map index assignment failed";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					++Context->MapSets;
					Context->Scratch = ContainerToken;
					return 0;
				}
			case ETypedLoopLocalKind::MapI32U32:
				{
					try
					{
						static_cast<FMap::FIntU32Entries*>(Binding->Storage)->InsertOrAssign(
						static_cast<std::int32_t>(IndexToken), static_cast<std::uint32_t>(Value));
					}
					catch (...)
					{
						Context->FailureMessage = "map index assignment failed";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					++Context->MapSets;
					Context->Scratch = ContainerToken;
					return 0;
				}
			case ETypedLoopLocalKind::MapU32I32:
				{
					try
					{
						static_cast<FMap::FUIntI32Entries*>(Binding->Storage)->InsertOrAssign(
						static_cast<std::uint32_t>(IndexToken), Value);
					}
					catch (...)
					{
						Context->FailureMessage = "map index assignment failed";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					++Context->MapSets;
					Context->Scratch = ContainerToken;
					return 0;
				}
			case ETypedLoopLocalKind::MapU32U32:
				{
					try
					{
						static_cast<FMap::FUIntU32Entries*>(Binding->Storage)->InsertOrAssign(
						static_cast<std::uint32_t>(IndexToken), static_cast<std::uint32_t>(Value));
					}
					catch (...)
					{
						Context->FailureMessage = "map index assignment failed";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					++Context->MapSets;
					Context->Scratch = ContainerToken;
					return 0;
				}
			case ETypedLoopLocalKind::MapStringI32:
				{
					if ((IndexToken & TypedLoopNativeStringTag) == 0u)
					{
						Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					const auto* const Key = reinterpret_cast<const FTypedLoopNativeStringView*>(
						static_cast<std::uintptr_t>(IndexToken & TypedLoopNativePayloadMask));
					if (!Key || !Key->Value)
					{
						Context->FailureMessage = "JIT typed loop: invalid borrowed string";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					try
					{
						static_cast<FMap::FStringI32Entries*>(Binding->Storage)
							->InsertOrAssignHashed(*Key->Value, Value, NativeTextHash(*Context, *Key));
					}
					catch (const std::exception&)
					{
						Context->FailureMessage = "map index assignment failed";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					catch (...)
					{
						Context->FailureMessage = "map index assignment failed";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					++Context->MapSets;
					Context->Scratch = ContainerToken;
					return 0;
				}
			default:
				{
					Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
			}

		}
		catch (...)
		{
			if (Context && Context->Vm)
			{
				Context->Vm->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	// Keep this repeated query target independent of cold code's page offsets.
	#if defined(_MSC_VER) && defined(_M_X64)
	__declspec(code_seg(".vsquery"))
	#endif
	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeContainsI32(
		FTypedLoopNativeContext* const Context,
		const std::uint64_t ContainerToken,
		const std::uint64_t ArgumentToken,
		const std::uint32_t InstructionOffset) noexcept
	{
		try
		{
			#if VEROSCRIPT_ENABLE_JIT_TEST_HOOKS
			++NativeContainsCallCount;
			#endif
			if (!Context || ContainerToken == 0u)
			{
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			auto* const Binding = reinterpret_cast<FTypedLoopNativeBinding*>(
				static_cast<std::uintptr_t>(ContainerToken));
			Context->FailureOffset = InstructionOffset;
			Context->FailureMessage = nullptr;
			{
				bool bFound = false;
				if (Binding->Kind == ETypedLoopLocalKind::MapI32I32)
				{
					bFound = static_cast<FMap::FIntI32Entries*>(Binding->Storage)
						->Find(static_cast<std::int32_t>(ArgumentToken)) != nullptr;
				}
				else if (Binding->Kind == ETypedLoopLocalKind::MapI32U32)
				{
					bFound = static_cast<FMap::FIntU32Entries*>(Binding->Storage)
						->Find(static_cast<std::int32_t>(ArgumentToken)) != nullptr;
				}
				else if (Binding->Kind == ETypedLoopLocalKind::MapU32I32)
				{
					bFound = static_cast<FMap::FUIntI32Entries*>(Binding->Storage)
						->Find(static_cast<std::uint32_t>(ArgumentToken)) != nullptr;
				}
				else if (Binding->Kind == ETypedLoopLocalKind::MapU32U32)
				{
					bFound = static_cast<FMap::FUIntU32Entries*>(Binding->Storage)
						->Find(static_cast<std::uint32_t>(ArgumentToken)) != nullptr;
				}
				else if (Binding->Kind == ETypedLoopLocalKind::SetI32)
				{
					bFound = static_cast<FSet::FIntScalarElements*>(Binding->Storage)
						->Contains(static_cast<std::int32_t>(ArgumentToken));
				}
				else if (Binding->Kind == ETypedLoopLocalKind::SetU32)
				{
					bFound = static_cast<FSet::FUIntScalarElements*>(Binding->Storage)
						->Contains(static_cast<std::uint32_t>(ArgumentToken));
				}
				else if (Binding->Kind == ETypedLoopLocalKind::MapStringI32
					|| Binding->Kind == ETypedLoopLocalKind::SetString)
				{
					if ((ArgumentToken & TypedLoopNativeStringTag) == 0u)
					{
						Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					const auto* const Key = reinterpret_cast<const FTypedLoopNativeStringView*>(
						static_cast<std::uintptr_t>(ArgumentToken & TypedLoopNativePayloadMask));
					if (!Key || !Key->Value)
					{
						Context->FailureMessage = "JIT typed loop: invalid borrowed string";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
					bFound = Binding->Kind == ETypedLoopLocalKind::MapStringI32
						? static_cast<FMap::FStringI32Entries*>(Binding->Storage)->FindHashed(
							*Key->Value, NativeTextHash(*Context, *Key))
							!= nullptr
						: static_cast<FSet::FStringScalarElements*>(Binding->Storage)
							->ContainsHashed(*Key->Value, NativeTextHash(*Context, *Key));
				}
				else
				{
					Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				Context->Scratch = bFound ? 1u : 0u;
				return 0;
			}

		}
		catch (...)
		{
			if (Context && Context->Vm)
			{
				Context->Vm->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeContainerMethodI32(
		FTypedLoopNativeContext* const Context,
		const std::uint64_t ContainerToken,
		const std::uint64_t ArgumentToken,
		const std::uint32_t Operation,
		const std::uint32_t InstructionOffset) noexcept
	{
		try
		{
			if (Operation == 1u)
			{
				return TypedLoopNativeContainsI32(
						Context, ContainerToken, ArgumentToken, InstructionOffset);
			}
			if (!Context || ContainerToken == 0u)
			{
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			auto* const Binding = reinterpret_cast<FTypedLoopNativeBinding*>(
				static_cast<std::uintptr_t>(ContainerToken));
			Context->FailureOffset = InstructionOffset;
			Context->FailureMessage = nullptr;
			if (Operation == 2u && (Binding->Kind == ETypedLoopLocalKind::ArrayI32
				|| Binding->Kind == ETypedLoopLocalKind::ArrayU32))
			{
				void* Data = nullptr;
				std::size_t Size = 0;
				try
				{
					if (Binding->Kind == ETypedLoopLocalKind::ArrayI32)
					{
						auto& Values = *static_cast<std::vector<std::int32_t>*>(Binding->Storage);
						Values.push_back(static_cast<std::int32_t>(ArgumentToken));
						Data = Values.data();
						Size = Values.size();
					}
					else
					{
						auto& Values = *static_cast<std::vector<std::uint32_t>*>(Binding->Storage);
						Values.push_back(static_cast<std::uint32_t>(ArgumentToken));
						Data = Values.data();
						Size = Values.size();
					}
				}
				catch (const std::exception&)
				{
					Context->FailureMessage = "array push failed";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				catch (...)
				{
					Context->FailureMessage = "array push failed";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				// Several frame locals may share the same array handle. push() can
				// reallocate its vector, so every preflight binding for that storage must
				// receive the new raw view before a later alias performs a direct indexed
				// access. Updating only the receiver leaves sibling bindings with a dangling
				// data pointer and stale size for the rest of this native loop invocation.
				FTypedLoopNativeBinding* Alias = Binding->AliasRoot
					? Binding->AliasRoot
					: Binding;
				while (Alias)
				{
					Alias->NativeData = Data;
					Alias->NativeSize = Size;
					Alias = Alias->NextAlias;
				}
				++Context->ArrayPushes;
				Context->Scratch = ContainerToken;
				return 0;
			}
			if (Operation == 3u && (Binding->Kind == ETypedLoopLocalKind::SetI32
				|| Binding->Kind == ETypedLoopLocalKind::SetU32))
			{
				try
				{
					if (Binding->Kind == ETypedLoopLocalKind::SetI32)
					{
						static_cast<FSet::FIntScalarElements*>(Binding->Storage)->Insert(
								static_cast<std::int32_t>(ArgumentToken));
					}
					else
					{
						static_cast<FSet::FUIntScalarElements*>(Binding->Storage)->Insert(
								static_cast<std::uint32_t>(ArgumentToken));
					}
				}
				catch (const std::exception&)
				{
					Context->FailureMessage = "set insertion failed";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				catch (...)
				{
					Context->FailureMessage = "set insertion failed";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				Context->Scratch = ContainerToken;
				return 0;
			}
			Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);

		}
		catch (...)
		{
			if (Context && Context->Vm)
			{
				Context->Vm->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeCompareI32OrString(
		FTypedLoopNativeContext* const Context,
		const std::uint64_t LeftToken,
		const std::uint64_t RightToken,
		const std::uint32_t Operation,
		const std::uint32_t InstructionOffset) noexcept
	{
		try
		{
			if (!Context || (LeftToken & TypedLoopNativeStringTag) == 0u
				|| (RightToken & TypedLoopNativeStringTag) == 0u
				|| Operation > 1u)
			{
				if (Context)
				{
					Context->FailureOffset = InstructionOffset;
					Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
				}
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			const auto* const Left = reinterpret_cast<const FTypedLoopNativeStringView*>(
				static_cast<std::uintptr_t>(LeftToken & TypedLoopNativePayloadMask));
			const auto* const Right = reinterpret_cast<const FTypedLoopNativeStringView*>(
				static_cast<std::uintptr_t>(RightToken & TypedLoopNativePayloadMask));
			if (!Left || !Right || !Left->Value || !Right->Value)
			{
				Context->FailureMessage = "JIT typed loop: invalid borrowed string";
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			const bool bEqual = Left == Right
				|| (Left->Size == Right->Size && NativeTextHash(*Context, *Left) == NativeTextHash(*Context, *Right)
					&& std::char_traits<char>::compare(
						Left->Data, Right->Data, Left->Size) == 0);
			Context->Scratch = Operation == 0u ? (bEqual ? 1u : 0u)
				: (bEqual ? 0u : 1u);
			return 0;

		}
		catch (...)
		{
			if (Context && Context->Vm)
			{
				Context->Vm->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeReportI32FieldMethodFailure(
		FTypedLoopNativeContext* const Context, const std::uint32_t InstructionIndex,
		const std::int32_t Status, const std::uint32_t InstructionOffset) noexcept
	{
		try
		{
			if (!Context || !Context->Vm || !Context->Methods)
			{
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			const auto& Method = Context->Methods[InstructionIndex];
			const auto CalleeOffset = Context->FailureOffset;
			Context->FailureOffset = InstructionOffset;
			Context->FailureMessage = nullptr;
			const char* Error = Status == static_cast<std::int32_t>(EJitRuntimeStatus::IntegerOverflow)
				? "integer overflow" : Status == static_cast<std::int32_t>(EJitRuntimeStatus::DivisionByZero)
				? "division by zero" : Status == static_cast<std::int32_t>(EJitRuntimeStatus::ShiftOutOfRange)
				? "shift count out of range" : "JIT scalar field method failed";
			Context->Vm->JitReportDirectI32TransformFailure(Method.ReceiverSlot, 0xFFFFu,
				Method.FunctionIndex, InstructionOffset, CalleeOffset, Error);
			return Status;
		}
		catch (...)
		{
			if (Context && Context->Vm)
			{
				Context->Vm->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	static std::int32_t InvokeI32FieldMethod(
		FTypedLoopNativeContext* const Context,
		const std::uint32_t InstructionIndex,
		const std::int32_t Argument,
		const std::uint64_t* Arguments,
		const std::uint32_t InstructionOffset) noexcept
	{
		try
		{
			if (!Context || !Context->Vm || !Context->Methods)
			{
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			FTypedLoopNativeMethodBinding& Method = Context->Methods[InstructionIndex];
			Context->FailureOffset = InstructionOffset;
			Context->FailureMessage = nullptr;
			if (Method.FieldCode)
			{
				std::int32_t Returned = 0;
				std::uint32_t FailureOffset = InvalidJitInstructionOffset;
				if (Method.ArgumentCount > 1u && !Arguments)
				{
					Context->FailureMessage = "JIT typed loop: invalid scalar method arguments";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				const auto Status = Method.ArgumentCount <= 1u
					? reinterpret_cast<JitNative::FFieldI32ExpressionKernel>(Method.FieldCode)(
						Argument, &Method.FieldState, &Returned, &FailureOffset)
					: reinterpret_cast<JitNative::FFieldI32ArgumentsExpressionKernel>(Method.FieldCode)(
						Arguments, &Method.FieldState, &Returned, &FailureOffset);
				if (Status != 0)
				{
					Context->FailureOffset = FailureOffset;
					return TypedLoopNativeReportI32FieldMethodFailure(Context, InstructionIndex, Status, InstructionOffset);
				}
				Context->Scratch = static_cast<std::uint32_t>(Returned);
				++Method.CallCount;
				return 0;
			}
			if (!Method.Object || !Method.Storage || Method.FieldIndex == 0xFFFFu)
			{
				Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			std::int32_t EffectiveArgument = Argument;
			if (Method.ArgumentModulus != 0)
			{
				if (EffectiveArgument == std::numeric_limits<std::int32_t>::min()
					&& Method.ArgumentModulus == -1)
				{
					return static_cast<std::int32_t>(EJitRuntimeStatus::IntegerOverflow);
				}
				EffectiveArgument %= Method.ArgumentModulus;
			}
			std::int32_t Sum = 0;
			std::int32_t Updated = 0;
			if (!Runtime::CheckedAdd(*Method.Storage, EffectiveArgument, Sum)
				|| !Runtime::CheckedAdd(Sum, Method.Addend, Updated)
				|| (Method.ResultModulus != 0
					&& Updated == std::numeric_limits<std::int32_t>::min()
					&& Method.ResultModulus == -1))
			{
				return static_cast<std::int32_t>(EJitRuntimeStatus::IntegerOverflow);
			}
			*Method.Storage = Method.ResultModulus == 0
				? Updated
				: Updated % Method.ResultModulus;
			++Method.CallCount;
			Context->Scratch = static_cast<std::uint32_t>(*Method.Storage);
			return 0;

		}
		catch (...)
		{
			if (Context && Context->Vm)
			{
				Context->Vm->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeInvokeI32FieldMethod(
		FTypedLoopNativeContext* const Context, const std::uint32_t InstructionIndex,
		const std::int32_t Argument, const std::uint32_t InstructionOffset) noexcept
	{
		return InvokeI32FieldMethod(Context, InstructionIndex, Argument, nullptr, InstructionOffset);
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeInvokeI32FieldMethodArguments(
		FTypedLoopNativeContext* const Context, const std::uint32_t InstructionIndex,
		const std::uint64_t* const Arguments, const std::uint32_t InstructionOffset) noexcept
	{
		return InvokeI32FieldMethod(Context, InstructionIndex, 0, Arguments, InstructionOffset);
	}

	std::int32_t VEROSCRIPT_JIT_CALL TypedLoopNativeInvokePureI32Method(
		FTypedLoopNativeContext* const Context,
		const std::uint32_t InstructionIndex,
		const std::uint64_t ReceiverToken,
		const std::int32_t Argument,
		const std::uint32_t InstructionOffset) noexcept
	{
		try
		{
			if (!Context || !Context->Vm || !Context->PolymorphicMethods
				|| ReceiverToken == 0u)
			{
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			FValue* SourceValue = nullptr;
			FScriptObject* Object = nullptr;
			std::uint16_t ResolvedTypeIndex = 0xFFFFu;
			if (IsTypedLoopNativeObjectView(ReceiverToken))
			{
				const auto* View = FindTypedLoopNativeObjectView(ReceiverToken,
					Context->ObjectViewRanges, Context->ObjectViewRangeCount, SourceValue);
				if (View)
				{
					ResolvedTypeIndex = View->TypeIndex;
					const auto* Handle = SourceValue ? std::get_if<FObjectHandle>(SourceValue) : nullptr;
					if (Handle && *Handle)
					{
						Object = Handle->get();
					}
				}
			}
			else
			{
				const auto* Receiver = reinterpret_cast<FTypedLoopNativeBinding*>(
					static_cast<std::uintptr_t>(ReceiverToken));
				if (Receiver->Kind == ETypedLoopLocalKind::Object)
				{
					SourceValue = Receiver->SourceValue;
					Object = static_cast<FScriptObject*>(Receiver->Storage);
				}
			}
			FTypedLoopNativePolymorphicMethodBinding& Method =
				Context->PolymorphicMethods[InstructionIndex];
			Context->FailureOffset = InstructionOffset;
			Context->FailureMessage = nullptr;
			if (!Object || !SourceValue || !Method.Candidates
				|| Method.CandidateCount == 0u)
			{
				Context->FailureMessage = "JIT typed loop: runtime plan mismatch";
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}

			if (Object->bDestroyed)
			{
				Context->FailureMessage = "JIT typed loop: destroyed object receiver";
				return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
			}
			FTypedLoopNativePolymorphicTarget* Target = nullptr;
			for (std::size_t Index = 0; Index < Method.CacheCount; ++Index)
			{
				if (Method.Cache[Index].Object == Object ||
					(Method.Cache[Index].TypeIdentity == &Object->GetTypeName() &&
						Method.Cache[Index].TypeIndex == ResolvedTypeIndex))
				{
					Target = &Method.Cache[Index];
					break;
				}
			}
			if (!Target)
			{
				const auto* const Handle = std::get_if<FObjectHandle>(SourceValue);
				if (!Handle || !*Handle || Handle->get() != Object)
				{
					Context->FailureMessage = "JIT typed loop: invalid object receiver";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				std::uint16_t FunctionIndex = 0xFFFFu;
				bool bHandled = false;
				if (!Context->Vm->JitResolveDirectI32MethodTarget(
						*Handle, Method.MethodNameIndex, 1u, InstructionOffset,
						FunctionIndex, bHandled))
				{
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				FTypedLoopNativePolymorphicTarget* Candidate = nullptr;
				for (std::size_t Index = 0; Index < Method.CandidateCount; ++Index)
				{
					if (Method.Candidates[Index].FunctionIndex == FunctionIndex)
					{
						Candidate = &Method.Candidates[Index];
						break;
					}
				}
				// The independently emitted candidate is based on a side-effect-free,
				// one-argument bytecode proof.  It remains valid even when the ordinary
				// function entry chose a non-scalar backend; the resolved function identity,
				// not that backend choice, is the dispatch authority.
				if ((!bHandled && FunctionIndex == 0xFFFFu)
					|| !Candidate || !Candidate->NativeCode)
				{
					Context->FailureMessage = "JIT typed loop: scalar method target changed";
					return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
				}
				if (Method.CacheCount < Method.CacheCapacity)
				{
					Target = &Method.Cache[Method.CacheCount++];
					*Target = *Candidate;
					Target->Object = Object;
				}
				else
				{
					Target = Candidate;
				}
			}

			std::int32_t Returned = 0;
			std::uint32_t CalleeFailureOffset = InvalidJitInstructionOffset;
			std::int32_t Status = 0;
			if (Target->FieldNameIndices)
			{
				std::array<std::int32_t*, 32> Fields;
				for (std::size_t Index = 0; Index < Target->FieldNameIndices->size(); ++Index)
				{
					const auto Field = Target->FieldIndices[Index];
					if (Field >= Object->Fields.size() || !(Fields[Index] = std::get_if<std::int32_t>(&Object->Fields[Field])))
					{
						Context->FailureMessage = "JIT typed loop: scalar field layout changed";
						return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
					}
				}
				FTypedLoopNativeMethodBinding::FFieldState State{Fields.data(), 0};
				Status = reinterpret_cast<JitNative::FFieldI32ExpressionKernel>(Target->NativeCode)(
					Argument, &State, &Returned, &CalleeFailureOffset);
			}
			else
			{
				Status = reinterpret_cast<JitNative::FPureI32ExpressionKernel>(
					Target->NativeCode)(Argument, 0, &Returned, &CalleeFailureOffset);
			}
			if (Status != 0)
			{
				const char* Error = "JIT scalar method failed";
				switch (static_cast<EJitRuntimeStatus>(Status))
				{
				case EJitRuntimeStatus::IntegerOverflow:
					{
						Error = "integer overflow";
						break;
					}
				case EJitRuntimeStatus::DivisionByZero:
					{
						Error = "division by zero";
						break;
					}
				case EJitRuntimeStatus::ShiftOutOfRange:
					{
						Error = "shift count out of range";
						break;
					}
				default:
					{
						break;
					}
				}
				Context->Vm->JitReportDirectI32TransformFailure(
					0xFFFFu, Method.MethodNameIndex, Target->FunctionIndex,
					InstructionOffset, CalleeFailureOffset, Error);
				return Status;
			}
			Context->Scratch = static_cast<std::uint32_t>(Returned);
			++Method.CallCount;
			return 0;

		}
		catch (...)
		{
			if (Context && Context->Vm)
			{
				Context->Vm->ReportNativeFailure();
			}
			return static_cast<std::int32_t>(EJitRuntimeStatus::GenericFailure);
		}
	}

}
