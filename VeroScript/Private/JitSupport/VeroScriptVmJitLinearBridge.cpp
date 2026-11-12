// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptNumericBindings.h"
#include "VeroScriptVm.h"

#include "VeroScriptVmJitBridgeInternal.h"
#include "VeroScriptVmInternal.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptHostBinding.h"
#include "VeroScriptHotReloadInternal.h"
#include "VeroScriptJit.h"
#include "VeroScriptMethodKey.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace VeroScript
{

	bool VeroScriptVm::JitTryGetScalarIndexOnLocal(
	    const std::uint16_t Slot,
	    const FValue& Index,
	    FValue& OutValue,
	    bool& bOutMapHandled,
	    bool& bOutArrayHandled)
	{
		bOutMapHandled = false;
		bOutArrayHandled = false;
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			return true;
		}
		FValue& Local = Frames.back().Locals[Slot];
		const FValue* const Resolved =
		    std::holds_alternative<FMapHandle>(Local) || std::holds_alternative<FDynamicArrayHandle>(Local)
		    ? &Local
		    : ResolveRefStorage(Local);
		if (!Resolved)
		{
			ReportError("invalid ref binding");
			return false;
		}
		if (std::holds_alternative<FMapHandle>(*Resolved))
		{
			return JitTryGetScalarMap(
			    *Resolved, Index, OutValue, bOutMapHandled);
		}
		std::size_t ArrayIndex = 0;
		if (Runtime::TryGetSizeValue(Index, ArrayIndex))
		{
			return JitTryGetDynamicArrayScalar(
			    *Resolved, static_cast<std::int64_t>(ArrayIndex), OutValue, bOutArrayHandled);
		}
		return true;
	}

	bool VeroScriptVm::JitTrySetDynamicArrayScalarOnLocal(
	    const std::uint16_t ArraySlot,
	    const FValue& IndexValue,
	    const FValue& SourceValue,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		if (Frames.empty() || ArraySlot >= Frames.back().Locals.size())
		{
			return true;
		}
		std::size_t Index = 0;
		if (!Runtime::TryGetSizeValue(IndexValue, Index))
		{
			return true;
		}
		FValue& DirectArray = Frames.back().Locals[ArraySlot];
		if (auto* const Handle = std::get_if<FDynamicArrayHandle>(&DirectArray);
		    Handle && *Handle && (*Handle)->HasScalarElements())
		{
			return JitTrySetDynamicArrayScalar(
			    DirectArray, Index, SourceValue, bOutHandled);
		}
		if (auto* const Handle = std::get_if<FDynamicArrayHandle>(&DirectArray);
		    Handle && *Handle && (*Handle)->ElementTypeName == "int")
		{
			if (static_cast<std::size_t>(Index) >= (*Handle)->Elements.size())
			{
				bOutHandled = true;
				ReportError("array index out of bounds");
				return false;
			}
			FValue* const Stored = UnwrapClosureReferenceCell(
			    &(*Handle)->Elements[static_cast<std::size_t>(Index)]);
			if (auto* const Dest = Stored ? std::get_if<std::int32_t>(Stored) : nullptr)
			{
				if (const auto* const Source =
				        std::get_if<std::int32_t>(&SourceValue))
				{
					*Dest = *Source;
					if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
					{
						NotifyContainerMutation(
						    DirectArray, EMutationKind::ContainerSet);
					}
					bOutHandled = true;
					return !bHasError;
				}
			}
		}

		FValue* const Array = std::holds_alternative<FDynamicArrayHandle>(DirectArray)
		    ? &DirectArray
		    : ResolveRefStorage(DirectArray);
		if (!Array)
		{
			ReportError("invalid ref binding");
			return false;
		}
		return JitTrySetDynamicArrayScalar(
		    *Array, Index, SourceValue, bOutHandled);
	}

	bool VeroScriptVm::JitTrySetScalarMapI32AddOnLocals(
	    const std::uint16_t MapSlot,
	    const std::uint16_t KeySlot,
	    const std::uint16_t ValueSlot,
	    const std::int32_t Immediate,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		if (Frames.empty() || MapSlot >= Frames.back().Locals.size() || KeySlot >= Frames.back().Locals.size() || ValueSlot >= Frames.back().Locals.size())
		{
			return true;
		}
		FValue& DirectMapValue = Frames.back().Locals[MapSlot];
		FValue& DirectKeyValue = Frames.back().Locals[KeySlot];
		FValue& DirectSourceValue = Frames.back().Locals[ValueSlot];
		if (auto* const Handle = std::get_if<FMapHandle>(&DirectMapValue);
		    Handle && *Handle)
		{
			const auto* const Key = std::get_if<std::int32_t>(&DirectKeyValue);
			const auto* const Source = std::get_if<std::int32_t>(&DirectSourceValue);
			auto* const Entries =
			    std::get_if<FMap::FIntI32Entries>(&(*Handle)->ScalarEntries);
			if (Key && Source && Entries)
			{
				bOutHandled = true;
				std::int32_t Result = 0;
				if (!Runtime::CheckedAdd(*Source, Immediate, Result))
				{
					ReportErrorId("diag.integer_overflow");
					return false;
				}
				Entries->InsertOrAssign(*Key, Result);
				if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
				{
					NotifyContainerMutation(
					    DirectMapValue, EMutationKind::ContainerSet);
				}
				return !bHasError;
			}
		}
		FValue* const MapValue = std::holds_alternative<FMapHandle>(DirectMapValue)
		    ? &DirectMapValue
		    : ResolveRefStorage(DirectMapValue);
		FValue* const KeyValue = std::holds_alternative<std::int32_t>(DirectKeyValue)
		    ? &DirectKeyValue
		    : ResolveRefStorage(DirectKeyValue);
		FValue* const SourceValue = std::holds_alternative<std::int32_t>(DirectSourceValue)
		    ? &DirectSourceValue
		    : ResolveRefStorage(DirectSourceValue);
		if (!MapValue || !KeyValue || !SourceValue)
		{
			ReportError("invalid ref binding");
			return false;
		}
		auto* const Handle = std::get_if<FMapHandle>(MapValue);
		const auto* const Key = std::get_if<std::int32_t>(KeyValue);
		const auto* const Source = std::get_if<std::int32_t>(SourceValue);
		if (!Handle || !*Handle || !Key || !Source)
		{
			return true;
		}
		auto* const Entries =
		    std::get_if<FMap::FIntI32Entries>(&(*Handle)->ScalarEntries);
		if (!Entries)
		{
			return true;
		}
		bOutHandled = true;
		std::int32_t Result = 0;
		if (!Runtime::CheckedAdd(*Source, Immediate, Result))
		{
			ReportErrorId("diag.integer_overflow");
			return false;
		}
		Entries->InsertOrAssign(*Key, Result);
		if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
		{
			NotifyContainerMutation(*MapValue, EMutationKind::ContainerSet);
		}
		return !bHasError;
	}

	bool VeroScriptVm::JitTrySetI32MapAffineRangeOnLocal(
	    const std::uint16_t MapSlot,
	    const std::int32_t Begin,
	    const std::int32_t End,
	    const std::int32_t Immediate,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || MapSlot >= Frames.back().Locals.size() || Begin >= End)
		{
			return true;
		}
		FValue& DirectMap = Frames.back().Locals[MapSlot];
		auto* const Handle = std::get_if<FMapHandle>(&DirectMap);
		if (!Handle || !*Handle || (*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
		{
			return true;
		}
		auto* const Entries = std::get_if<FMap::FIntI32Entries>(
		    &(*Handle)->ScalarEntries);
		if (!Entries)
		{
			return true;
		}

		// The affine expression is monotonic. Checking both endpoints proves that
		// every generated value is a valid int32 without changing the language's
		// checked-overflow behavior. Unsafe ranges deliberately fall back to the
		// per-element path, which preserves the exact failing write and partial map.
		const std::int64_t FirstValue = static_cast<std::int64_t>(Begin) + static_cast<std::int64_t>(Immediate);
		const std::int64_t LastValue = static_cast<std::int64_t>(End) - 1 + static_cast<std::int64_t>(Immediate);
		if (FirstValue < std::numeric_limits<std::int32_t>::min() || FirstValue > std::numeric_limits<std::int32_t>::max() || LastValue < std::numeric_limits<std::int32_t>::min() || LastValue > std::numeric_limits<std::int32_t>::max())
		{
			return true;
		}

		constexpr std::uint64_t MaxAutomaticBatch = std::uint64_t{1} << 20u;
		const std::uint64_t RangeCount = static_cast<std::uint64_t>(
		    static_cast<std::int64_t>(End) - static_cast<std::int64_t>(Begin));
		if (RangeCount > MaxAutomaticBatch || RangeCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() - Entries->size()))
		{
			return true;
		}
		Entries->InsertOrAssignGenerated(
		    static_cast<std::size_t>(RangeCount),
		    [Begin, Immediate](const std::size_t Offset,
		        std::int32_t& OutKey, std::int32_t& OutValue)
		{
			const std::int64_t Key = static_cast<std::int64_t>(Begin) + static_cast<std::int64_t>(Offset);
			OutKey = static_cast<std::int32_t>(Key);
			OutValue = static_cast<std::int32_t>(
			    Key + static_cast<std::int64_t>(Immediate));
		});
		OutHandled = true;
		return true;
	}

	bool VeroScriptVm::JitTrySetScalarMapDirectOnLocal(
	    const std::uint16_t MapSlot,
	    const FValue& KeyValue,
	    const FValue& SourceValue,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		if (Frames.empty() || MapSlot >= Frames.back().Locals.size())
		{
			return true;
		}
		FValue& DirectMapValue = Frames.back().Locals[MapSlot];
		FValue* const MapValue = std::holds_alternative<FMapHandle>(DirectMapValue)
		    ? &DirectMapValue
		    : ResolveRefStorage(DirectMapValue);
		if (!MapValue)
		{
			ReportError("invalid ref binding");
			return false;
		}
		auto* const Handle = std::get_if<FMapHandle>(MapValue);
		if (!Handle || !*Handle)
		{
			return true;
		}
		if ((*Handle)->ScalarEntries.index() == 0)
		{
			if (!Runtime::IsPrimitiveValueTypeName((*Handle)->ValueTypeName) || (*Handle)->ValueTypeName == "string" || (*Handle)->ValueTypeName == "wstring")
			{
				return true;
			}
			const auto StoreGeneric = [&](auto& Entries, const auto* const Key) -> bool
			{
				if (!Key || !Runtime::ValueMatchesPrimitiveTypeName(SourceValue, (*Handle)->ValueTypeName))
				{
					return true;
				}
				Entries.insert_or_assign(*Key, SourceValue);
				if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
				{
					NotifyContainerMutation(*MapValue, EMutationKind::ContainerSet);
				}
				bOutHandled = true;
				return !bHasError;
			};
			if ((*Handle)->KeyTypeName == "string")
			{
				return StoreGeneric(
				    (*Handle)->StrEntries, std::get_if<std::string>(&KeyValue));
			}
			if ((*Handle)->KeyTypeName == "wstring")
			{
				return StoreGeneric(
				    (*Handle)->WStrEntries, std::get_if<std::u16string>(&KeyValue));
			}
			return true;
		}
		if (const auto* const Key = std::get_if<std::int32_t>(&KeyValue))
		{
			if (const auto* const Source = std::get_if<std::int32_t>(&SourceValue))
			{
				if (auto* const Entries =
				        std::get_if<FMap::FIntI32Entries>(&(*Handle)->ScalarEntries))
				{
					Entries->InsertOrAssign(*Key, *Source);
					if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
					{
						NotifyContainerMutation(*MapValue, EMutationKind::ContainerSet);
					}
					bOutHandled = true;
					return !bHasError;
				}
			}
		}
		bool bTypesMatch = false;
		std::visit(
		    [&](auto& Entries)
		{
			using FEntries = std::decay_t<decltype(Entries)>;
			if constexpr (!std::is_same_v<FEntries, std::monostate>)
			{
				using FKey = typename FEntries::key_type;
				using FMapped = typename FEntries::mapped_type;
				const auto* const Key = std::get_if<FKey>(&KeyValue);
				if (!Key)
				{
					return;
				}
				if constexpr (std::is_same_v<FMapped, FMap::FPrimitiveScalar>)
				{
					FMap::FPrimitiveScalar Value;
					if (Runtime::ValueMatchesPrimitiveTypeName(
					        SourceValue, (*Handle)->ValueTypeName) &&
					    FMap::TryPackPrimitiveScalar(SourceValue, Value))
					{
						Entries.InsertOrAssign(*Key, Value);
						bTypesMatch = true;
					}
				}
				else if (const auto* const Value = std::get_if<FMapped>(&SourceValue))
				{
					Entries.InsertOrAssign(*Key, *Value);
					bTypesMatch = true;
				}
			}
		},
		    (*Handle)->ScalarEntries);
		if (!bTypesMatch)
		{
			return true;
		}
		bOutHandled = true;
		if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
		{
			NotifyContainerMutation(*MapValue, EMutationKind::ContainerSet);
		}
		return !bHasError;
	}

	bool VeroScriptVm::JitTrySetScalarMapDirectFromLocals(
	    const std::uint16_t MapSlot,
	    const std::uint16_t KeySlot,
	    const std::uint16_t SourceSlot,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		if (Frames.empty() || MapSlot >= Frames.back().Locals.size() || KeySlot >= Frames.back().Locals.size() || SourceSlot >= Frames.back().Locals.size())
		{
			return true;
		}
		std::vector<FValue>& Locals = Frames.back().Locals;
		FValue& DirectMap = Locals[MapSlot];
		FValue& DirectKey = Locals[KeySlot];
		FValue& DirectSource = Locals[SourceSlot];
		if (auto* const Handle = std::get_if<FMapHandle>(&DirectMap);
		    Handle && *Handle)
		{
			const auto* const Key = std::get_if<std::int32_t>(&DirectKey);
			const auto* const Source = std::get_if<std::int32_t>(&DirectSource);
			auto* const Entries =
			    std::get_if<FMap::FIntI32Entries>(&(*Handle)->ScalarEntries);
			if (Key && Source && Entries)
			{
				Entries->InsertOrAssign(*Key, *Source);
				if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
				{
					NotifyContainerMutation(DirectMap, EMutationKind::ContainerSet);
				}
				bOutHandled = true;
				return !bHasError;
			}
		}
		const FValue* const Key = Runtime::IsIntegerValue(DirectKey)
		    ? &DirectKey
		    : ResolveRefStorage(DirectKey);
		const FValue* const Source = Runtime::IsIntegerValue(DirectSource) || std::holds_alternative<float>(DirectSource) || std::holds_alternative<double>(DirectSource) || std::holds_alternative<bool>(DirectSource)
		    ? &DirectSource
		    : ResolveRefStorage(DirectSource);
		if (!Key || !Source)
		{
			ReportError("invalid ref binding");
			return false;
		}
		return JitTrySetScalarMapDirectOnLocal(
		    MapSlot, *Key, *Source, bOutHandled);
	}

	bool VeroScriptVm::JitTryPushDynamicArrayScalarOnLocal(
	    const std::uint16_t Slot,
	    const FValue& Value,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			return true;
		}
		FValue& Local = Frames.back().Locals[Slot];
		if (auto* const Handle = std::get_if<FDynamicArrayHandle>(&Local);
		    Handle && *Handle && (*Handle)->HasScalarElements())
		{
			if (auto* const Values = std::get_if<std::vector<std::int32_t>>(
			        &(*Handle)->ScalarElements))
			{
				if (const auto* const Scalar = std::get_if<std::int32_t>(&Value))
				{
					Values->push_back(*Scalar);
					bOutHandled = true;
					return true;
				}
			}
			return JitTryPushDynamicArrayScalar(Local, Value, bOutHandled);
		}
		if (auto* const Handle = std::get_if<FDynamicArrayHandle>(&Local);
		    Handle && *Handle)
		{
			const std::string& ElementType = (*Handle)->ElementTypeName;
			bool bStored = false;
			if (ElementType == "int")
			{
				if (const auto* const Scalar = std::get_if<std::int32_t>(&Value))
				{
					(*Handle)->Elements.emplace_back(*Scalar);
					bStored = true;
				}
			}
			else if (ElementType == "uint" || ElementType == "unsigned int" || ElementType == "unsigned")
			{
				if (const auto* const Scalar = std::get_if<std::uint32_t>(&Value))
				{
					(*Handle)->Elements.emplace_back(*Scalar);
					bStored = true;
				}
			}
			else if (ElementType == "int64")
			{
				if (const auto* const Scalar = std::get_if<std::int64_t>(&Value))
				{
					(*Handle)->Elements.emplace_back(*Scalar);
					bStored = true;
				}
			}
			else if (ElementType == "uint64")
			{
				if (const auto* const Scalar = std::get_if<std::uint64_t>(&Value))
				{
					(*Handle)->Elements.emplace_back(*Scalar);
					bStored = true;
				}
			}
			else if (ElementType == "float")
			{
				if (const auto* const Scalar = std::get_if<float>(&Value))
				{
					(*Handle)->Elements.emplace_back(*Scalar);
					bStored = true;
				}
			}
			else if (ElementType == "double")
			{
				if (const auto* const Scalar = std::get_if<double>(&Value))
				{
					(*Handle)->Elements.emplace_back(*Scalar);
					bStored = true;
				}
			}
			else if (ElementType == "bool")
			{
				if (const auto* const Scalar = std::get_if<bool>(&Value))
				{
					(*Handle)->Elements.emplace_back(*Scalar);
					bStored = true;
				}
			}
			if (bStored)
			{
				if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
				{
					NotifyContainerMutation(Local, EMutationKind::ContainerAdd);
				}
				bOutHandled = true;
				return !bHasError;
			}
		}
		const FValue* const Resolved = std::holds_alternative<FDynamicArrayHandle>(Local)
		    ? &Local
		    : ResolveRefStorage(Local);
		if (!Resolved)
		{
			ReportError("invalid ref binding");
			return false;
		}
		return JitTryPushDynamicArrayScalar(*Resolved, Value, bOutHandled);
	}

	bool VeroScriptVm::JitHandleLinearThrow(
	    FValue Value,
	    const std::size_t InstructionOffset,
	    std::size_t& OutHandlerOffset)
	{
		OutHandlerOffset = 0;
		if (Frames.empty() || !Frames.back().Function)
		{
			ReportError("JIT linear throw outside function");
			return false;
		}
		ActiveInstructionStart = InstructionOffset;
		Frames.back().LastInstructionOffset = InstructionOffset;
		bool bHandled = false;
		if (!TryHandleCurrentFrameScriptException(
		        Value, Frames.back().Function->Location, bHandled))
		{
			return false;
		}
		if (!bHandled)
		{
			return RaiseScriptException(
			    std::move(Value), Frames.back().Function->Location);
		}
		if (Frames.empty() || !Frames.back().Function)
		{
			ReportError("JIT linear catch invalidated its frame");
			return false;
		}
		OutHandlerOffset = Frames.back().InstructionPointer;
		return true;
	}

	bool VeroScriptVm::JitLoadLocal(const std::uint16_t Slot, FValue& OutValue)
	{
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			ReportError("JIT LoadLocal: invalid slot");
			return false;
		}
		FValue& Local = Frames.back().Locals[Slot];
		FValue* const Resolved = std::holds_alternative<FObjectHandle>(Local)
		    ? &Local
		    : ResolveRefStorage(Local);
		if (!Resolved)
		{
			ReportError("invalid ref binding");
			return false;
		}
		OutValue = *Resolved;
		return true;
	}

	bool VeroScriptVm::JitLoadLocalField(
	    const std::uint16_t Slot,
	    const std::uint16_t NameIndex,
	    FValue& OutValue)
	{
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			ReportError("JIT LoadLocalField: invalid slot");
			return false;
		}
		FValue& Local = Frames.back().Locals[Slot];
		FValue* const Resolved = std::holds_alternative<FObjectHandle>(Local)
		    ? &Local
		    : ResolveRefStorage(Local);
		if (!Resolved)
		{
			ReportError("invalid ref binding");
			return false;
		}
		return JitLoadField(*Resolved, NameIndex, OutValue);
	}

	bool VeroScriptVm::JitTryCallScalarContainerMethodOnLocal(
	    const std::uint16_t Slot,
	    const std::uint16_t MethodNameIndex,
	    const FValue* const Arguments,
	    const std::uint8_t ArgumentCount,
	    const bool bDiscardReturn,
	    FValue& OutValue,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (!ActiveModule || MethodNameIndex >= ActiveModule->StringPool.size() || ArgumentCount != 1 || !Arguments)
		{
			return true;
		}
		const std::string& MethodName = ActiveModule->StringPool[MethodNameIndex];
		EJitScalarSetOperation Operation = EJitScalarSetOperation::Add;
		if (MethodName == "has")
		{
			Operation = EJitScalarSetOperation::Has;
		}
		else if (MethodName == "remove")
		{
			Operation = EJitScalarSetOperation::Remove;
		}
		else if (MethodName != "add")
		{
			return true;
		}
		return JitTryCallScalarSetMethodOnLocal(
		    Slot,
		    Arguments[0],
		    Operation,
		    bDiscardReturn,
		    OutValue,
		    OutHandled);
	}

	bool VeroScriptVm::JitTryUpdateScalarMapEntryOnLocal(
	    const std::uint16_t MapSlot,
	    const FValue& Key,
	    const EOpcode Operation,
	    const FValue& Operand,
	    bool& OutHandled,
	    EJitScalarMapUpdateFailure& OutFailure)
	{
		OutHandled = false;
		OutFailure = EJitScalarMapUpdateFailure::None;
		if (Frames.empty() || MapSlot >= Frames.back().Locals.size())
		{
			return true;
		}
		FValue& DirectMapValue = Frames.back().Locals[MapSlot];
		auto* const Handle = std::get_if<FMapHandle>(&DirectMapValue);
		if (!Handle || !*Handle || (*Handle)->ScalarEntries.index() == 0)
		{
			return true;
		}
		// Typed scalar maps use raw key/value storage. Apply the common compound
		// operations directly for every native 32-bit key and value combination,
		// preserving the authoritative VM overflow and diagnostic semantics without
		// constructing temporary FValue variants or entering the generic visitor.
		enum class ERawUpdateResult : std::uint8_t
		{
			NotApplicable,
			Succeeded,
			Failed,
		};
		const auto TryRawUpdate = [&](auto* const Entries,
		                              const auto* const TypedKey,
		                              const auto* const Right) -> ERawUpdateResult
		{
			using TValue = std::remove_cv_t<std::remove_pointer_t<decltype(Right)>>;
			if (!Entries || !TypedKey || !Right)
			{
				return ERawUpdateResult::NotApplicable;
			}
			TValue* const Stored = Entries->FindMutable(*TypedKey);
			if (!Stored)
			{
				OutFailure = EJitScalarMapUpdateFailure::MissingKey;
				return ERawUpdateResult::Failed;
			}

			TValue Result = *Stored;
			if constexpr (std::is_same_v<TValue, std::int32_t>)
			{
				switch (Operation)
				{
				case EOpcode::Add:
					{
						if (!Runtime::CheckedAdd(*Stored, *Right, Result))
						{
							OutFailure = EJitScalarMapUpdateFailure::IntegerOverflow;
							return ERawUpdateResult::Failed;
						}
						break;
					}
				case EOpcode::Sub:
					{
						if (!Runtime::CheckedSub(*Stored, *Right, Result))
						{
							OutFailure = EJitScalarMapUpdateFailure::IntegerOverflow;
							return ERawUpdateResult::Failed;
						}
						break;
					}
				case EOpcode::Mul:
					{
						if (!Runtime::CheckedMul(*Stored, *Right, Result))
						{
							OutFailure = EJitScalarMapUpdateFailure::IntegerOverflow;
							return ERawUpdateResult::Failed;
						}
						break;
					}
				case EOpcode::Div:
				case EOpcode::Mod:
					{
						if (*Right == 0)
						{
							OutFailure = EJitScalarMapUpdateFailure::DivisionByZero;
							return ERawUpdateResult::Failed;
						}
						if (*Stored == std::numeric_limits<std::int32_t>::min() && *Right == -1)
						{
							OutFailure = EJitScalarMapUpdateFailure::IntegerOverflow;
							return ERawUpdateResult::Failed;
						}
						Result = Operation == EOpcode::Div
						    ? static_cast<std::int32_t>(*Stored / *Right)
						    : static_cast<std::int32_t>(*Stored % *Right);
						break;
					}
				case EOpcode::BitAnd:
					{
						Result = *Stored & *Right;
						break;
					}
				case EOpcode::BitOr:
					{
						Result = *Stored | *Right;
						break;
					}
				case EOpcode::BitXor:
					{
						Result = *Stored ^ *Right;
						break;
					}

				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						if (*Right < 0 || *Right > 31)
						{
							OutFailure = EJitScalarMapUpdateFailure::ShiftOutOfRange;
							return ERawUpdateResult::Failed;
						}
						Result = Operation == EOpcode::Shl
						    ? static_cast<std::int32_t>(
						          static_cast<std::uint32_t>(*Stored) << *Right)
						    : static_cast<std::int32_t>(*Stored >> *Right);
						break;
					}
				default:
					return ERawUpdateResult::NotApplicable;
				}
			}
			else if constexpr (std::is_same_v<TValue, std::uint32_t>)
			{
				switch (Operation)
				{
				case EOpcode::Add:
					{
						Result = *Stored + *Right;
						break;
					}
				case EOpcode::Sub:
					{
						Result = *Stored - *Right;
						break;
					}
				case EOpcode::Mul:
					{
						Result = *Stored * *Right;
						break;
					}
				case EOpcode::Div:
				case EOpcode::Mod:
					{
						if (*Right == 0u)
						{
							OutFailure = EJitScalarMapUpdateFailure::DivisionByZero;
							return ERawUpdateResult::Failed;
						}
						Result = Operation == EOpcode::Div
						    ? *Stored / *Right
						    : *Stored % *Right;
						break;
					}
				case EOpcode::BitAnd:
					{
						Result = *Stored & *Right;
						break;
					}
				case EOpcode::BitOr:
					{
						Result = *Stored | *Right;
						break;
					}
				case EOpcode::BitXor:
					{
						Result = *Stored ^ *Right;
						break;
					}

				case EOpcode::Shl:
				case EOpcode::Shr:
					{
						if (*Right > 31u)
						{
							OutFailure = EJitScalarMapUpdateFailure::ShiftOutOfRange;
							return ERawUpdateResult::Failed;
						}
						Result = Operation == EOpcode::Shl
						    ? *Stored << *Right
						    : *Stored >> *Right;
						break;
					}
				default:
					return ERawUpdateResult::NotApplicable;
				}
			}
			else if constexpr (std::is_same_v<TValue, float> || std::is_same_v<TValue, double>)
			{
				switch (Operation)
				{
				case EOpcode::Add:
					{
						Result = *Stored + *Right;
						break;
					}
				case EOpcode::Sub:
					{
						Result = *Stored - *Right;
						break;
					}
				case EOpcode::Mul:
					{
						Result = *Stored * *Right;
						break;
					}
				case EOpcode::Div:
					{
						if (*Right == TValue{0})
						{
							OutFailure = EJitScalarMapUpdateFailure::DivisionByZero;
							return ERawUpdateResult::Failed;
						}
						Result = *Stored / *Right;
						break;
					}
				default:
					return ERawUpdateResult::NotApplicable;
				}
			}
			else
			{
				return ERawUpdateResult::NotApplicable;
			}

			*Stored = Result;
			if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
			{
				NotifyContainerMutation(DirectMapValue, EMutationKind::ContainerSet);
			}
			return ERawUpdateResult::Succeeded;
		};
		const auto FinishRawUpdate = [&](const ERawUpdateResult Result) -> bool
		{
			if (Result == ERawUpdateResult::NotApplicable)
			{
				return false;
			}
			OutHandled = true;
			return true;
		};

		ERawUpdateResult RawResult = TryRawUpdate(
		    std::get_if<FMap::FIntI32Entries>(&(*Handle)->ScalarEntries),
		    std::get_if<std::int32_t>(&Key), std::get_if<std::int32_t>(&Operand));
		if (FinishRawUpdate(RawResult))
		{
			return RawResult == ERawUpdateResult::Succeeded && !bHasError;
		}
		RawResult = TryRawUpdate(
		    std::get_if<FMap::FUIntU32Entries>(&(*Handle)->ScalarEntries),
		    std::get_if<std::uint32_t>(&Key), std::get_if<std::uint32_t>(&Operand));
		if (FinishRawUpdate(RawResult))
		{
			return RawResult == ERawUpdateResult::Succeeded && !bHasError;
		}
		RawResult = TryRawUpdate(
		    std::get_if<FMap::FIntU32Entries>(&(*Handle)->ScalarEntries),
		    std::get_if<std::int32_t>(&Key), std::get_if<std::uint32_t>(&Operand));
		if (FinishRawUpdate(RawResult))
		{
			return RawResult == ERawUpdateResult::Succeeded && !bHasError;
		}
		RawResult = TryRawUpdate(
		    std::get_if<FMap::FUIntI32Entries>(&(*Handle)->ScalarEntries),
		    std::get_if<std::uint32_t>(&Key), std::get_if<std::int32_t>(&Operand));
		if (FinishRawUpdate(RawResult))
		{
			return RawResult == ERawUpdateResult::Succeeded && !bHasError;
		}
		RawResult = TryRawUpdate(
		    std::get_if<FMap::FIntF32Entries>(&(*Handle)->ScalarEntries),
		    std::get_if<std::int32_t>(&Key), std::get_if<float>(&Operand));
		if (FinishRawUpdate(RawResult))
		{
			return RawResult == ERawUpdateResult::Succeeded && !bHasError;
		}
		RawResult = TryRawUpdate(
		    std::get_if<FMap::FUIntF32Entries>(&(*Handle)->ScalarEntries),
		    std::get_if<std::uint32_t>(&Key), std::get_if<float>(&Operand));
		if (FinishRawUpdate(RawResult))
		{
			return RawResult == ERawUpdateResult::Succeeded && !bHasError;
		}
		RawResult = TryRawUpdate(
		    std::get_if<FMap::FIntF64Entries>(&(*Handle)->ScalarEntries),
		    std::get_if<std::int32_t>(&Key), std::get_if<double>(&Operand));
		if (FinishRawUpdate(RawResult))
		{
			return RawResult == ERawUpdateResult::Succeeded && !bHasError;
		}
		RawResult = TryRawUpdate(
		    std::get_if<FMap::FUIntF64Entries>(&(*Handle)->ScalarEntries),
		    std::get_if<std::uint32_t>(&Key), std::get_if<double>(&Operand));
		if (FinishRawUpdate(RawResult))
		{
			return RawResult == ERawUpdateResult::Succeeded && !bHasError;
		}
		bool bTypeMatched = false;
		bool bUpdated = false;
		const bool bOk = std::visit(
		    [&](auto& Entries) -> bool
		{
			using TEntries = std::decay_t<decltype(Entries)>;
			if constexpr (std::is_same_v<TEntries, std::monostate>)
			{
				return true;
			}
			else
			{
				using TKey = typename TEntries::key_type;
				using TMapped = typename TEntries::mapped_type;
				const auto* const TypedKey = std::get_if<TKey>(&Key);
				if (!TypedKey)
				{
					return true;
				}
				bTypeMatched = true;
				TMapped* const Stored = Entries.FindMutable(*TypedKey);
				if (!Stored)
				{
					OutFailure = EJitScalarMapUpdateFailure::MissingKey;
					return false;
				}
				FValue Current;
				if constexpr (std::is_same_v<TMapped, FMap::FPrimitiveScalar>)
				{
					Current = FMap::UnpackPrimitiveScalar(*Stored);
				}
				else
				{
					Current = *Stored;
				}
				FValue Result;
				const bool bArithmetic = Operation == EOpcode::Add || Operation == EOpcode::Sub || Operation == EOpcode::Mul || Operation == EOpcode::Div || Operation == EOpcode::Mod;
				if (!(bArithmetic
				            ? JitEvalArithmetic(Operation, Current, Operand, Result)
				            : JitEvalBitwise(Operation, Current, Operand, Result)))
				{
					OutFailure = EJitScalarMapUpdateFailure::Operation;
					return false;
				}
				if constexpr (std::is_same_v<TMapped, FMap::FPrimitiveScalar>)
				{
					FMap::FPrimitiveScalar Packed = *Stored;
					if (!FMap::TryPackPrimitiveScalar(Result, Packed) || Packed.index() != Stored->index())
					{
						return true;
					}
					*Stored = std::move(Packed);
				}
				else
				{
					const auto* const TypedResult = std::get_if<TMapped>(&Result);
					if (!TypedResult)
					{
						return true;
					}
					*Stored = *TypedResult;
				}
				bUpdated = true;
				return true;
			}
		},
		    (*Handle)->ScalarEntries);
		if (!bOk)
		{
			OutHandled = bTypeMatched;
			return false;
		}
		OutHandled = bUpdated;
		if (bUpdated && (*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
		{
			NotifyContainerMutation(DirectMapValue, EMutationKind::ContainerSet);
		}
		return !bHasError;
	}

	bool VeroScriptVm::JitTryCallScalarSetMethodOnLocal(
	    const std::uint16_t Slot,
	    const FValue& Argument,
	    const EJitScalarSetOperation Operation,
	    const bool bDiscardReturn,
	    FValue& OutValue,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			return true;
		}
		FValue& DirectContainer = Frames.back().Locals[Slot];
		FValue* const Container = std::holds_alternative<FSetHandle>(DirectContainer)
		    ? &DirectContainer
		    : ResolveRefStorage(DirectContainer);
		if (!Container)
		{
			ReportError("invalid ref binding");
			return false;
		}
		auto* const Handle = std::get_if<FSetHandle>(Container);
		if (!Handle || !*Handle)
		{
			return true;
		}
		const auto ApplyFlat = [&](auto& Elements, const auto* const Element) -> bool
		{
			if (!Element)
			{
				return true;
			}
			if (Operation == EJitScalarSetOperation::Has)
			{
				OutValue = Elements.Contains(*Element);
			}
			else if (Operation == EJitScalarSetOperation::Remove)
			{
				OutValue = Elements.Erase(*Element);
			}
			else
			{
				(void)Elements.Insert(*Element);
				if (!bDiscardReturn)
				{
					OutValue = *Container;
				}
			}
			if (Operation != EJitScalarSetOperation::Has && (*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
			{
				NotifyContainerMutation(
				    *Container,
				    Operation == EJitScalarSetOperation::Remove
				        ? EMutationKind::ContainerRemove
				        : EMutationKind::ContainerAdd);
			}
			OutHandled = true;
			return !bHasError;
		};
		if ((*Handle)->ScalarElements.index() != 0)
		{
			if (auto* Elements = std::get_if<FSet::FIntScalarElements>(
			        &(*Handle)->ScalarElements))
			{
				return ApplyFlat(*Elements, std::get_if<std::int32_t>(&Argument));
			}
			if (auto* Elements = std::get_if<FSet::FUIntScalarElements>(
			        &(*Handle)->ScalarElements))
			{
				return ApplyFlat(*Elements, std::get_if<std::uint32_t>(&Argument));
			}
			if (auto* Elements = std::get_if<FSet::FInt64ScalarElements>(
			        &(*Handle)->ScalarElements))
			{
				return ApplyFlat(*Elements, std::get_if<std::int64_t>(&Argument));
			}
			if (auto* Elements = std::get_if<FSet::FUInt64ScalarElements>(
			        &(*Handle)->ScalarElements))
			{
				return ApplyFlat(*Elements, std::get_if<std::uint64_t>(&Argument));
			}
			if (auto* Elements = std::get_if<FSet::FStringScalarElements>(
			        &(*Handle)->ScalarElements))
			{
				return ApplyFlat(*Elements, std::get_if<std::string>(&Argument));
			}
			if (auto* Elements = std::get_if<FSet::FWStringScalarElements>(
			        &(*Handle)->ScalarElements))
			{
				return ApplyFlat(*Elements, std::get_if<std::u16string>(&Argument));
			}
			return true;
		}

		const auto Apply = [&](auto& Elements, const auto* const Element) -> bool
		{
			if (!Element)
			{
				return true;
			}
			if (Operation == EJitScalarSetOperation::Has)
			{
				OutValue = Elements.find(*Element) != Elements.end();
			}
			else if (Operation == EJitScalarSetOperation::Remove)
			{
				OutValue = Elements.erase(*Element) > 0;
				if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
				{
					NotifyContainerMutation(
					    *Container, EMutationKind::ContainerRemove);
				}
			}
			else
			{
				Elements.insert(*Element);
				if (!bDiscardReturn)
				{
					OutValue = *Container;
				}
				if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
				{
					NotifyContainerMutation(*Container, EMutationKind::ContainerAdd);
				}
			}
			OutHandled = true;
			return !bHasError;
		};
		const std::string& ElementType = (*Handle)->ElementTypeName;
		if (ElementType == "int")
		{
			return Apply(
			    (*Handle)->IntElements, std::get_if<std::int32_t>(&Argument));
		}
		if (ElementType == "unsigned int")
		{
			return Apply(
			    (*Handle)->UIntElements, std::get_if<std::uint32_t>(&Argument));
		}
		if (ElementType == "int64")
		{
			return Apply(
			    (*Handle)->Int64Elements, std::get_if<std::int64_t>(&Argument));
		}
		if (ElementType == "uint64")
		{
			return Apply(
			    (*Handle)->UInt64Elements, std::get_if<std::uint64_t>(&Argument));
		}
		if (ElementType == "string")
		{
			return Apply(
			    (*Handle)->StrElements, std::get_if<std::string>(&Argument));
		}
		if (ElementType == "wstring")
		{
			return Apply(
			    (*Handle)->WStrElements, std::get_if<std::u16string>(&Argument));
		}
		return true;
	}

	bool VeroScriptVm::JitTryCallScalarSetMethodOnLocals(
	    const std::uint16_t Slot,
	    const std::uint16_t ArgumentSlot,
	    const EJitScalarSetOperation Operation,
	    const bool bDiscardReturn,
	    FValue& OutValue,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || ArgumentSlot >= Frames.back().Locals.size())
		{
			return true;
		}
		FValue& DirectArgument = Frames.back().Locals[ArgumentSlot];
		const bool bDirectPrimitive = Runtime::IsIntegerValue(DirectArgument) || std::holds_alternative<float>(DirectArgument) || std::holds_alternative<double>(DirectArgument) || std::holds_alternative<bool>(DirectArgument) || std::holds_alternative<std::string>(DirectArgument) || std::holds_alternative<std::u16string>(DirectArgument);
		FValue* const Argument = bDirectPrimitive
		    ? &DirectArgument
		    : ResolveRefStorage(DirectArgument);
		if (!Argument)
		{
			ReportError("invalid ref binding");
			return false;
		}
		return JitTryCallScalarSetMethodOnLocal(
		    Slot,
		    *Argument,
		    Operation,
		    bDiscardReturn,
		    OutValue,
		    OutHandled);
	}

	bool VeroScriptVm::JitTryInsertI32SetRangeOnLocal(
	    const std::uint16_t Slot,
	    const std::int32_t Begin,
	    const std::int32_t End,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || Slot >= Frames.back().Locals.size() || Begin >= End)
		{
			return true;
		}
		FValue& DirectContainer = Frames.back().Locals[Slot];
		FValue* const Container = std::holds_alternative<FSetHandle>(DirectContainer)
		    ? &DirectContainer
		    : ResolveRefStorage(DirectContainer);
		auto* const Handle = Container ? std::get_if<FSetHandle>(Container) : nullptr;
		if (!Handle || !*Handle || (*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
		{
			return true;
		}
		auto* const Elements = std::get_if<FSet::FIntScalarElements>(
		    &(*Handle)->ScalarElements);
		if (!Elements)
		{
			return true;
		}
		// The recognized range is strictly increasing, so its iteration count is
		// an exact upper bound on additional elements. Reserve bounded batches once
		// instead of repeatedly growing and rehashing inside the fused loop. Very
		// large ranges retain progressive allocation unless the script explicitly
		// requests reserve(), avoiding a surprising eager multi-gigabyte allocation.
		constexpr std::uint64_t MaxAutomaticBulkReserve = std::uint64_t{1} << 20u;
		const std::uint64_t RangeCount = static_cast<std::uint64_t>(
		    static_cast<std::int64_t>(End) - static_cast<std::int64_t>(Begin));
		if (RangeCount <= MaxAutomaticBulkReserve && RangeCount <= static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max() - Elements->size()))
		{
			Elements->InsertGenerated(
			    static_cast<std::size_t>(RangeCount),
			    [Begin](const std::size_t Offset, std::int32_t& OutKey)
			{
				OutKey = static_cast<std::int32_t>(
				    static_cast<std::int64_t>(Begin) + static_cast<std::int64_t>(Offset));
			});
		}
		else
		{
			for (std::int64_t Value = Begin; Value < End; ++Value)
			{
				(void)Elements->Insert(static_cast<std::int32_t>(Value));
			}
		}
		OutHandled = true;
		return true;
	}

	bool VeroScriptVm::JitTryCallScalarContainerMethodOnLocals(
	    const std::uint16_t Slot,
	    const std::uint16_t MethodNameIndex,
	    const std::uint16_t ArgumentSlot,
	    const bool bDiscardReturn,
	    FValue& OutValue,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || ArgumentSlot >= Frames.back().Locals.size())
		{
			return true;
		}
		FValue& DirectArgument = Frames.back().Locals[ArgumentSlot];
		const bool bDirectPrimitive = Runtime::IsIntegerValue(DirectArgument) || std::holds_alternative<float>(DirectArgument) || std::holds_alternative<double>(DirectArgument) || std::holds_alternative<bool>(DirectArgument) || std::holds_alternative<std::string>(DirectArgument) || std::holds_alternative<std::u16string>(DirectArgument);
		FValue* const Argument = bDirectPrimitive
		    ? &DirectArgument
		    : ResolveRefStorage(DirectArgument);
		if (!Argument)
		{
			ReportError("invalid ref binding");
			return false;
		}
		return JitTryCallScalarContainerMethodOnLocal(
		    Slot,
		    MethodNameIndex,
		    Argument,
		    1,
		    bDiscardReturn,
		    OutValue,
		    OutHandled);
	}

	bool VeroScriptVm::JitTryLoadScalarLocal(
	    const std::uint16_t Slot,
	    FValue& OutValue,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			// This entry point is used by speculative Linear expression decoding.
			// A byte sequence that is not the requested scalar shape must decline the
			// optimization instead of manufacturing a runtime error. Valid bytecode
			// LoadLocal instructions are range-checked by the normal execution path.
			return true;
		}
		FValue& Local = Frames.back().Locals[Slot];
		const auto TryLoadDirectScalar = [&](const FValue& Value) -> bool
		{
			if (const auto* I32 = std::get_if<std::int32_t>(&Value))
			{
				OutValue = *I32;
			}
			else if (const auto* U32 = std::get_if<std::uint32_t>(&Value))
			{
				OutValue = *U32;
			}
			else if (const auto* I64 = std::get_if<std::int64_t>(&Value))
			{
				OutValue = *I64;
			}
			else if (const auto* U64 = std::get_if<std::uint64_t>(&Value))
			{
				OutValue = *U64;
			}
			else if (const auto* F32 = std::get_if<float>(&Value))
			{
				OutValue = *F32;
			}
			else if (const auto* F64 = std::get_if<double>(&Value))
			{
				OutValue = *F64;
			}
			else if (const auto* Bool = std::get_if<bool>(&Value))
			{
				OutValue = *Bool;
			}
			else if (const auto* String = std::get_if<std::string>(&Value))
			{
				OutValue = *String;
			}
			else if (const auto* WString = std::get_if<std::u16string>(&Value))
			{
				OutValue = *WString;
			}
			else
			{
				return false;
			}
			return true;
		};
		if (TryLoadDirectScalar(Local))
		{
			OutHandled = true;
			return true;
		}
		FValue* const Resolved = ResolveRefStorage(Local);
		if (!Resolved)
		{
			ReportError("invalid ref binding");
			return false;
		}
		if (!TryLoadDirectScalar(*Resolved))
		{
			return true;
		}
		OutHandled = true;
		return true;
	}

	bool VeroScriptVm::JitTryLoadScalarGlobal(
	    const std::uint16_t Slot,
	    FValue& OutValue,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Slot >= Globals.size())
		{
			// Scalar expression decoding is a speculative shape probe. Bytes from a
			// later expression can legally resemble LoadGlobal plus an out-of-range
			// operand; decline without changing observable VM error state.
			return true;
		}
		if (!JitLoadGlobal(Slot, OutValue))
		{
			return false;
		}
		OutHandled = std::holds_alternative<bool>(OutValue) || std::holds_alternative<std::int8_t>(OutValue) || std::holds_alternative<std::uint8_t>(OutValue) || std::holds_alternative<std::int16_t>(OutValue) || std::holds_alternative<std::uint16_t>(OutValue) || std::holds_alternative<std::int32_t>(OutValue) || std::holds_alternative<std::uint32_t>(OutValue) || std::holds_alternative<std::int64_t>(OutValue) || std::holds_alternative<std::uint64_t>(OutValue) || std::holds_alternative<float>(OutValue) || std::holds_alternative<double>(OutValue);
		return true;
	}

	bool VeroScriptVm::JitStoreLocal(const std::uint16_t Slot, FValue Value)
	{
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			ReportError("JIT StoreLocal: invalid slot");
			return false;
		}
		FValue& Local = Frames.back().Locals[Slot];
		const bool bDirectPrimitive = Runtime::IsNumericValue(Local) || std::holds_alternative<bool>(Local) || std::holds_alternative<std::string>(Local) || std::holds_alternative<std::u16string>(Local);
		if (bDirectPrimitive && Local.index() == Value.index())
		{
			Local = std::move(Value);
			return true;
		}
		// Match the interpreter's ownership-aware object-local fast path. The old
		// object is bypassed only when it cannot be the last live script handle;
		// final-owner replacement still executes the full destructor path below.
		if (auto* const Previous = std::get_if<FObjectHandle>(&Local))
		{
			if (auto* const Next = std::get_if<FObjectHandle>(&Value);
			    Next && (!*Previous || (*Previous)->bDestroyed || (*Previous)->bDestroying || Previous->use_count() != 1))
			{
				*Previous = std::move(*Next);
				return true;
			}
		}
		FValue* const Resolved = Runtime::IsIntegerValue(Local) || std::holds_alternative<float>(Local) || std::holds_alternative<double>(Local) || std::holds_alternative<bool>(Local)
		    ? &Local
		    : ResolveRefStorage(Local);
		if (!Resolved)
		{
			ReportError("invalid ref binding");
			return false;
		}
		if (!AssignValue(*Resolved, std::move(Value), {}))
		{
			return false;
		}
		if (Resolved != &Frames.back().Locals[Slot])
		{
			NotifyReferenceMutation(Frames.back().Locals[Slot]);
		}
		return !bHasError;
	}

	bool VeroScriptVm::JitTryUpdateLocalFieldI32Modulo(
	    const std::uint16_t Slot,
	    const std::uint16_t NameIndex,
	    const FValue& Delta,
	    const std::int32_t Modulus,
	    const std::uint32_t CallSiteOffset,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || Slot >= Frames.back().Locals.size() || !ActiveModule || NameIndex >= ActiveModule->StringPool.size() || Modulus == 0)
		{
			return true;
		}
		const auto* const DeltaI32 = std::get_if<std::int32_t>(&Delta);
		auto* const Handle = std::get_if<FObjectHandle>(
		    &Frames.back().Locals[Slot]);
		if (!DeltaI32 || !Handle || !*Handle || (*Handle)->bDestroyed)
		{
			return true;
		}
		FScriptObject* const Object = Handle->get();
		const std::uint16_t TypeIndex =
		    Object->RuntimeTypeModule == ActiveModule && Object->RuntimeTypeGeneration == ProgramGeneration && Object->RuntimeTypeIndex < ActiveModule->Types.size()
		    ? Object->RuntimeTypeIndex
		    : ResolveObjectTypeIndex(*Object);
		constexpr std::size_t WaysPerSet = 2;
		constexpr std::size_t SetCount = 4;
		const std::size_t SetIndex =
		    (static_cast<std::size_t>(CallSiteOffset) * 2654435761u) & (SetCount - 1u);
		const std::size_t SetBase = SetIndex * WaysPerSet;
		std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
		FValue* FieldStorage = nullptr;
		for (std::size_t Way = 0; Way < WaysPerSet; ++Way)
		{
			FFieldInlineCacheEntry& Cached =
			    Frames.back().FieldIc[SetBase + Way];
			if (Cached.Module == ActiveModule && Cached.ProgramGeneration == ProgramGeneration && Cached.CallSiteOffset == CallSiteOffset && Cached.NameIndex == NameIndex && Cached.TypeIndex == TypeIndex)
			{
				FieldIndex = Cached.FieldIndex;
				if (!Cached.Object.owner_before(*Handle) && !Handle->owner_before(Cached.Object))
				{
					FieldStorage = Cached.Storage;
				}
				else if (FieldIndex < Object->Fields.size())
				{
					Cached.Object = *Handle;
					Cached.Storage = UnwrapClosureReferenceCell(
					    &Object->Fields[FieldIndex]);
					FieldStorage = Cached.Storage;
				}
				break;
			}
		}
		if (FieldIndex == static_cast<std::uint16_t>(-1))
		{
			FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
			const std::size_t Way = Frames.back().FieldIcNextWay[SetIndex]++ & 1u;
			FFieldInlineCacheEntry& Cached = Frames.back().FieldIc[SetBase + Way];
			Cached.Module = ActiveModule;
			Cached.Object.reset();
			Cached.Storage = nullptr;
			Cached.ProgramGeneration = ProgramGeneration;
			Cached.CallSiteOffset = CallSiteOffset;
			Cached.NameIndex = NameIndex;
			Cached.TypeIndex = TypeIndex;
			Cached.FieldIndex = FieldIndex;
			if (FieldIndex < Object->Fields.size())
			{
				Cached.Object = *Handle;
				Cached.Storage = UnwrapClosureReferenceCell(
				    &Object->Fields[FieldIndex]);
				FieldStorage = Cached.Storage;
			}
		}
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= Object->Fields.size())
		{
			return true;
		}
		if (!FieldStorage)
		{
			FieldStorage = UnwrapClosureReferenceCell(&Object->Fields[FieldIndex]);
		}
		auto* const Field = FieldStorage
		    ? std::get_if<std::int32_t>(FieldStorage)
		    : nullptr;
		if (!Field)
		{
			return true;
		}
		OutHandled = true;
		std::int32_t Sum = 0;
		if (!Runtime::CheckedAdd(*Field, *DeltaI32, Sum))
		{
			ReportErrorId("diag.integer_overflow");
			return false;
		}
		if (Sum == std::numeric_limits<std::int32_t>::min() && Modulus == -1)
		{
			// Signed INT_MIN % -1 is undefined in C++ (and traps on some targets).
			// The authoritative VM arithmetic path defines it as integer overflow.
			ReportErrorId("diag.integer_overflow");
			return false;
		}
		*Field = Sum % Modulus;
		NotifyFieldMutationJitFast(*Object, FieldIndex);
		return !bHasError;
	}

	bool VeroScriptVm::JitBorrowLocalI32Field(
	    const std::uint16_t Slot,
	    const std::uint16_t NameIndex,
	    FScriptObject*& OutObject,
	    std::uint16_t& OutFieldIndex,
	    std::int32_t*& OutStorage)
	{
		OutObject = nullptr;
		OutFieldIndex = static_cast<std::uint16_t>(-1);
		OutStorage = nullptr;
		if (Frames.empty() || Slot >= Frames.back().Locals.size() || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(&Frames.back().Locals[Slot]);
		if (!Handle || !*Handle || (*Handle)->bDestroyed)
		{
			return true;
		}
		FScriptObject* const Object = Handle->get();
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*Object);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= Object->Fields.size())
		{
			return true;
		}
		FValue* const Field = UnwrapClosureReferenceCell(&Object->Fields[FieldIndex]);
		auto* const Storage = Field ? std::get_if<std::int32_t>(Field) : nullptr;
		if (!Storage)
		{
			return true;
		}
		OutObject = Object;
		OutFieldIndex = FieldIndex;
		OutStorage = Storage;
		return true;
	}

	bool VeroScriptVm::JitBorrowLocalF64Field(
	    const std::uint16_t Slot,
	    const std::uint16_t NameIndex,
	    FScriptObject*& OutObject,
	    std::uint16_t& OutFieldIndex,
	    double*& OutStorage)
	{
		OutObject = nullptr;
		OutFieldIndex = static_cast<std::uint16_t>(-1);
		OutStorage = nullptr;
		if (Frames.empty() || Slot >= Frames.back().Locals.size() || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(&Frames.back().Locals[Slot]);
		if (!Handle || !*Handle || (*Handle)->bDestroyed)
		{
			return true;
		}
		FScriptObject* const Object = Handle->get();
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*Object);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= Object->Fields.size())
		{
			return true;
		}
		FValue* const Field = UnwrapClosureReferenceCell(&Object->Fields[FieldIndex]);
		auto* const Storage = Field ? std::get_if<double>(Field) : nullptr;
		if (!Storage)
		{
			return true;
		}
		OutObject = Object;
		OutFieldIndex = FieldIndex;
		OutStorage = Storage;
		return true;
	}

	bool VeroScriptVm::JitBorrowLocalU32Field(
	    const std::uint16_t Slot,
	    const std::uint16_t NameIndex,
	    FScriptObject*& OutObject,
	    std::uint16_t& OutFieldIndex,
	    std::uint32_t*& OutStorage)
	{
		OutObject = nullptr;
		OutFieldIndex = static_cast<std::uint16_t>(-1);
		OutStorage = nullptr;
		if (Frames.empty() || Slot >= Frames.back().Locals.size() || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(
		    &Frames.back().Locals[Slot]);
		if (!Handle || !*Handle || (*Handle)->bDestroyed)
		{
			return true;
		}
		FScriptObject* const Object = Handle->get();
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*Object);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= Object->Fields.size())
		{
			return true;
		}
		FValue* const Field = UnwrapClosureReferenceCell(
		    &Object->Fields[FieldIndex]);
		auto* const Storage = Field ? std::get_if<std::uint32_t>(Field) : nullptr;
		if (!Storage)
		{
			return true;
		}
		OutObject = Object;
		OutFieldIndex = FieldIndex;
		OutStorage = Storage;
		return true;
	}

	bool VeroScriptVm::JitBorrowLocalF32Field(
	    const std::uint16_t Slot,
	    const std::uint16_t NameIndex,
	    FScriptObject*& OutObject,
	    std::uint16_t& OutFieldIndex,
	    float*& OutStorage)
	{
		OutObject = nullptr;
		OutFieldIndex = static_cast<std::uint16_t>(-1);
		OutStorage = nullptr;
		if (Frames.empty() || Slot >= Frames.back().Locals.size() || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			return true;
		}
		auto* const Handle = std::get_if<FObjectHandle>(&Frames.back().Locals[Slot]);
		if (!Handle || !*Handle || (*Handle)->bDestroyed)
		{
			return true;
		}
		FScriptObject* const Object = Handle->get();
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*Object);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= Object->Fields.size())
		{
			return true;
		}
		FValue* const Field = UnwrapClosureReferenceCell(&Object->Fields[FieldIndex]);
		auto* const Storage = Field ? std::get_if<float>(Field) : nullptr;
		if (!Storage)
		{
			return true;
		}
		OutObject = Object;
		OutFieldIndex = FieldIndex;
		OutStorage = Storage;
		return true;
	}

	bool VeroScriptVm::JitBorrowLocalScalarField(
	    const std::uint16_t Slot,
	    const std::uint16_t NameIndex,
	    FScriptObject*& OutObject,
	    std::uint16_t& OutFieldIndex,
	    const FValue*& OutStorage)
	{
		OutObject = nullptr;
		OutFieldIndex = static_cast<std::uint16_t>(-1);
		OutStorage = nullptr;
		if (Frames.empty() || Slot >= Frames.back().Locals.size() || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			return true;
		}
		const auto* const Handle =
		    std::get_if<FObjectHandle>(&Frames.back().Locals[Slot]);
		if (!Handle || !*Handle || (*Handle)->bDestroyed)
		{
			return true;
		}
		FScriptObject* const Object = Handle->get();
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(*Object);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= Object->Fields.size())
		{
			return true;
		}
		const FValue* const Storage =
		    UnwrapClosureReferenceCell(&Object->Fields[FieldIndex]);
		if (!Storage || !(std::holds_alternative<std::int32_t>(*Storage) || std::holds_alternative<std::uint32_t>(*Storage) || std::holds_alternative<std::int64_t>(*Storage) || std::holds_alternative<std::uint64_t>(*Storage) || std::holds_alternative<float>(*Storage) || std::holds_alternative<double>(*Storage) || std::holds_alternative<bool>(*Storage) || std::holds_alternative<std::string>(*Storage) || std::holds_alternative<std::u16string>(*Storage)))
		{
			return true;
		}
		OutObject = Object;
		OutFieldIndex = FieldIndex;
		OutStorage = Storage;
		return true;
	}

	bool VeroScriptVm::JitTryAddLocalFromLocalField(
	    const std::uint16_t DestSlot,
	    const std::uint16_t ObjectSlot,
	    const std::uint16_t NameIndex,
	    const std::uint32_t CallSiteOffset,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || DestSlot >= Frames.back().Locals.size() || ObjectSlot >= Frames.back().Locals.size() || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			return true;
		}
		std::vector<FValue>& Locals = Frames.back().Locals;
		FValue& DirectDest = Locals[DestSlot];
		FValue& DirectObject = Locals[ObjectSlot];
		const auto* const DirectHandle = std::get_if<FObjectHandle>(&DirectObject);
		if (DirectHandle && *DirectHandle && !(*DirectHandle)->bDestroyed)
		{
			FScriptObject* const Object = DirectHandle->get();
			const std::uint16_t TypeIndex =
			    Object->RuntimeTypeModule == ActiveModule && Object->RuntimeTypeGeneration == ProgramGeneration && Object->RuntimeTypeIndex < ActiveModule->Types.size()
			    ? Object->RuntimeTypeIndex
			    : ResolveObjectTypeIndex(*Object);
			constexpr std::size_t WaysPerSet = 2;
			constexpr std::size_t SetCount = 4;
			const std::uint32_t SiteHash = CallSiteOffset != 0xFFFFFFFFu
			    ? CallSiteOffset
			    : (static_cast<std::uint32_t>(DestSlot) * 2654435761u ^ static_cast<std::uint32_t>(ObjectSlot) * 2246822519u ^ NameIndex);
			const std::size_t SetIndex =
			    (static_cast<std::size_t>(SiteHash) * 2654435761u) & (SetCount - 1u);
			const std::size_t SetBase = SetIndex * WaysPerSet;
			std::uint16_t FieldIndex = static_cast<std::uint16_t>(-1);
			FValue* FieldStorage = nullptr;
			for (std::size_t Way = 0; Way < WaysPerSet; ++Way)
			{
				FFieldInlineCacheEntry& Cached =
				    Frames.back().FieldIc[SetBase + Way];
				if (Cached.Module == ActiveModule && Cached.ProgramGeneration == ProgramGeneration && Cached.CallSiteOffset == CallSiteOffset && Cached.NameIndex == NameIndex && Cached.TypeIndex == TypeIndex)
				{
					FieldIndex = Cached.FieldIndex;
					if (!Cached.Object.owner_before(*DirectHandle) && !DirectHandle->owner_before(Cached.Object))
					{
						FieldStorage = Cached.Storage;
					}
					else if (FieldIndex < Object->Fields.size())
					{
						Cached.Object = *DirectHandle;
						Cached.Storage = UnwrapClosureReferenceCell(
						    &Object->Fields[FieldIndex]);
						FieldStorage = Cached.Storage;
					}
					break;
				}
			}
			if (FieldIndex == static_cast<std::uint16_t>(-1))
			{
				FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
				const std::size_t Way =
				    Frames.back().FieldIcNextWay[SetIndex]++ & 1u;
				FFieldInlineCacheEntry& Cached =
				    Frames.back().FieldIc[SetBase + Way];
				Cached.Module = ActiveModule;
				Cached.Object.reset();
				Cached.Storage = nullptr;
				Cached.ProgramGeneration = ProgramGeneration;
				Cached.CallSiteOffset = CallSiteOffset;
				Cached.NameIndex = NameIndex;
				Cached.TypeIndex = TypeIndex;
				Cached.FieldIndex = FieldIndex;
				if (FieldIndex < Object->Fields.size())
				{
					Cached.Object = *DirectHandle;
					Cached.Storage = UnwrapClosureReferenceCell(
					    &Object->Fields[FieldIndex]);
					FieldStorage = Cached.Storage;
				}
			}
			if (FieldIndex != static_cast<std::uint16_t>(-1) && FieldIndex < Object->Fields.size())
			{
				if (!FieldStorage)
				{
					FieldStorage = UnwrapClosureReferenceCell(
					    &Object->Fields[FieldIndex]);
				}
				if (!FieldStorage)
				{
					return true;
				}
				const FValue& DirectField = *FieldStorage;
				if (auto* DestI32 = std::get_if<std::int32_t>(&DirectDest))
				{
					if (const auto* FieldI32 = std::get_if<std::int32_t>(&DirectField))
					{
						std::int32_t Result = 0;
						if (!Runtime::CheckedAdd(*DestI32, *FieldI32, Result))
						{
							ReportErrorId("diag.integer_overflow");
							return false;
						}
						*DestI32 = Result;
						OutHandled = true;
						return true;
					}
				}
				if (auto* DestU32 = std::get_if<std::uint32_t>(&DirectDest))
				{
					if (const auto* FieldU32 = std::get_if<std::uint32_t>(&DirectField))
					{
						*DestU32 += *FieldU32;
						OutHandled = true;
						return true;
					}
				}
				if (auto* DestI64 = std::get_if<std::int64_t>(&DirectDest))
				{
					if (const auto* FieldI64 = std::get_if<std::int64_t>(&DirectField))
					{
						if ((*FieldI64 > 0 && *DestI64 > std::numeric_limits<std::int64_t>::max() - *FieldI64) || (*FieldI64 < 0 && *DestI64 < std::numeric_limits<std::int64_t>::min() - *FieldI64))
						{
							ReportErrorId("diag.integer_overflow");
							return false;
						}
						*DestI64 += *FieldI64;
						OutHandled = true;
						return true;
					}
				}
				if (auto* DestU64 = std::get_if<std::uint64_t>(&DirectDest))
				{
					if (const auto* FieldU64 = std::get_if<std::uint64_t>(&DirectField))
					{
						*DestU64 += *FieldU64;
						OutHandled = true;
						return true;
					}
				}
				if (auto* DestF32 = std::get_if<float>(&DirectDest))
				{
					if (const auto* FieldF32 = std::get_if<float>(&DirectField))
					{
						*DestF32 += *FieldF32;
						OutHandled = true;
						return true;
					}
				}
				if (auto* DestF64 = std::get_if<double>(&DirectDest))
				{
					if (const auto* FieldF64 = std::get_if<double>(&DirectField))
					{
						*DestF64 += *FieldF64;
						OutHandled = true;
						return true;
					}
				}
			}
		}

		const FValue* const ObjectValue = ResolveRefStorage(DirectObject);
		const auto* const Handle = ObjectValue
		    ? std::get_if<FObjectHandle>(ObjectValue)
		    : nullptr;
		if (!Handle || !*Handle || (*Handle)->bDestroyed)
		{
			return true;
		}
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**Handle);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= (*Handle)->Fields.size())
		{
			return true;
		}
		const FValue* const Field = UnwrapClosureReferenceCell(
		    &(*Handle)->Fields[FieldIndex]);
		if (!Field || (!Runtime::IsIntegerValue(*Field) && !std::holds_alternative<float>(*Field) && !std::holds_alternative<double>(*Field)))
		{
			return true;
		}
		OutHandled = true;
		return JitAddValueToLocal(DestSlot, *Field);
	}

	bool VeroScriptVm::JitTryAddLocalFromScalarMap(
	    const std::uint16_t DestSlot,
	    const std::uint16_t MapSlot,
	    const FValue& Key,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || DestSlot >= Frames.back().Locals.size() || MapSlot >= Frames.back().Locals.size())
		{
			return true;
		}
		FValue& DirectDest = Frames.back().Locals[DestSlot];
		FValue& DirectMap = Frames.back().Locals[MapSlot];
		const auto* const MapHandle = std::get_if<FMapHandle>(&DirectMap);
		if (auto* const DestI32 = std::get_if<std::int32_t>(&DirectDest);
		    DestI32 && MapHandle && *MapHandle)
		{
			if (const auto* const I32Key = std::get_if<std::int32_t>(&Key))
			{
				if (const auto* const Entries =
				        std::get_if<FMap::FIntI32Entries>(&(*MapHandle)->ScalarEntries))
				{
					OutHandled = true;
					const std::int32_t* const Found = Entries->Find(*I32Key);
					if (!Found)
					{
						ReportError("map key not found");
						return false;
					}
					std::int32_t Result = 0;
					if (!Runtime::CheckedAdd(*DestI32, *Found, Result))
					{
						ReportErrorId("diag.integer_overflow");
						return false;
					}
					*DestI32 = Result;
					return true;
				}
			}
		}

		const FValue* const MapValue = MapHandle
		    ? &DirectMap
		    : ResolveRefStorage(DirectMap);
		if (!MapValue)
		{
			ReportError("invalid ref binding");
			return false;
		}
		FValue Found;
		bool bMapHandled = false;
		if (!JitTryGetScalarMap(*MapValue, Key, Found, bMapHandled))
		{
			OutHandled = bMapHandled;
			return false;
		}
		if (!bMapHandled)
		{
			return true;
		}
		OutHandled = true;
		return JitAddValueToLocal(DestSlot, Found);
	}

	bool VeroScriptVm::JitTryAddLocalFromScalarMapModuloI32(
	    const std::uint16_t DestSlot,
	    const std::uint16_t MapSlot,
	    const FValue& Key,
	    const std::int32_t Modulus,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || DestSlot >= Frames.back().Locals.size() || MapSlot >= Frames.back().Locals.size())
		{
			return true;
		}
		if (Modulus == 0)
		{
			// Only claim the operation after proving that the operands use the
			// exact typed representation; otherwise the authoritative bytecode path
			// is responsible for reporting its own error.
			return true;
		}
		auto* const Dest = std::get_if<std::int32_t>(
		    &Frames.back().Locals[DestSlot]);
		const auto* const MapHandle = std::get_if<FMapHandle>(
		    &Frames.back().Locals[MapSlot]);
		const auto* const I32Key = std::get_if<std::int32_t>(&Key);
		if (!Dest || !MapHandle || !*MapHandle || !I32Key)
		{
			return true;
		}
		const auto* const Entries = std::get_if<FMap::FIntI32Entries>(
		    &(*MapHandle)->ScalarEntries);
		if (!Entries)
		{
			return true;
		}
		OutHandled = true;
		const std::int32_t* const Found = Entries->Find(*I32Key);
		if (!Found)
		{
			ReportError("map key not found");
			return false;
		}
		std::int32_t Sum = 0;
		if (!Runtime::CheckedAdd(*Dest, *Found, Sum))
		{
			ReportErrorId("diag.integer_overflow");
			return false;
		}
		if (Sum == std::numeric_limits<std::int32_t>::min() && Modulus == -1)
		{
			// Match EvalArithmetic and avoid signed modulo undefined behaviour.
			ReportErrorId("diag.integer_overflow");
			return false;
		}
		*Dest = Sum % Modulus;
		return true;
	}

	bool VeroScriptVm::JitTryAddLocalFromScalarArray(
	    const std::uint16_t DestSlot,
	    const std::uint16_t ArraySlot,
	    const FValue& IndexValue,
	    bool& OutHandled)
	{
		OutHandled = false;
		if (Frames.empty() || DestSlot >= Frames.back().Locals.size() || ArraySlot >= Frames.back().Locals.size())
		{
			return true;
		}
		std::int64_t Index = 0;
		if (!Runtime::TryGetInteger64(IndexValue, Index))
		{
			return true;
		}
		FValue& DirectDest = Frames.back().Locals[DestSlot];
		FValue& DirectArray = Frames.back().Locals[ArraySlot];
		const auto* const Handle = std::get_if<FDynamicArrayHandle>(&DirectArray);
		if (auto* const Dest = std::get_if<std::int32_t>(&DirectDest);
		    Dest && Handle && *Handle)
		{
			if (const auto* const Values = std::get_if<std::vector<std::int32_t>>(
			        &(*Handle)->ScalarElements))
			{
				OutHandled = true;
				if (Index < 0 || static_cast<std::uint64_t>(Index) >= Values->size())
				{
					OutHandled = false;
					return true;
				}
				std::int32_t Result = 0;
				if (!Runtime::CheckedAdd(
				        *Dest, (*Values)[static_cast<std::size_t>(Index)], Result))
				{
					OutHandled = false;
					return true;
				}
				*Dest = Result;
				return true;
			}
		}
		if (auto* const Dest = std::get_if<std::uint32_t>(&DirectDest);
		    Dest && Handle && *Handle)
		{
			if (const auto* const Values = std::get_if<std::vector<std::uint32_t>>(
			        &(*Handle)->ScalarElements))
			{
				OutHandled = true;
				if (Index < 0 || static_cast<std::uint64_t>(Index) >= Values->size())
				{
					OutHandled = false;
					return true;
				}
				*Dest += (*Values)[static_cast<std::size_t>(Index)];
				return true;
			}
		}
		if (auto* const Dest = std::get_if<std::int32_t>(&DirectDest);
		    Dest && Handle && *Handle && (*Handle)->ElementTypeName == "int")
		{
			OutHandled = true;
			if (Index < 0 || static_cast<std::uint64_t>(Index) >= (*Handle)->Elements.size())
			{
				OutHandled = false;
				return true;
			}
			const FValue* const Stored = UnwrapClosureReferenceCell(
			    &(*Handle)->Elements[static_cast<std::size_t>(Index)]);
			const auto* const Source = Stored
			    ? std::get_if<std::int32_t>(Stored)
			    : nullptr;
			if (Source)
			{
				std::int32_t Result = 0;
				if (!Runtime::CheckedAdd(*Dest, *Source, Result))
				{
					OutHandled = false;
					return true;
				}
				*Dest = Result;
				return true;
			}
			OutHandled = false;
		}

		const FValue* const Array = Handle
		    ? &DirectArray
		    : ResolveRefStorage(DirectArray);
		if (!Array)
		{
			return true;
		}
		FValue Found;
		const auto* ResolvedArray = std::get_if<FDynamicArrayHandle>(Array);
		if (!ResolvedArray || !*ResolvedArray || Index < 0 || static_cast<std::uint64_t>(Index) >= (*ResolvedArray)->ElementCount())
		{
			return true;
		}
		if ((*ResolvedArray)->HasScalarElements())
		{
			if (!(*ResolvedArray)->TryGetScalar(static_cast<std::size_t>(Index), Found)) return true;
		}
		else
		{
			const auto* Stored = UnwrapClosureReferenceCell(&(*ResolvedArray)->Elements[static_cast<std::size_t>(Index)]);
			if (!Stored || !Runtime::IsPrimitiveValueTypeName((*ResolvedArray)->ElementTypeName) || !Runtime::ValueMatchesPrimitiveTypeName(*Stored, (*ResolvedArray)->ElementTypeName))
			{
				return true;
			}
			Found = *Stored;
		}
		OutHandled = true;
		return JitAddValueToLocal(DestSlot, Found);
	}

	bool VeroScriptVm::JitBindLocal(const std::uint16_t Slot, FValue Value)
	{
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			ReportError("JIT BindLocal: invalid slot");
			return false;
		}
		Frames.back().Locals[Slot] = std::move(Value);
		return true;
	}

	bool VeroScriptVm::JitAddLocalImm(const std::uint16_t Slot, const std::int32_t Imm)
	{
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			ReportError("JIT AddLocalImm: invalid slot");
			return false;
		}
		FValue& Local = Frames.back().Locals[Slot];
		if (auto* const I32 = std::get_if<std::int32_t>(&Local))
		{
			std::int32_t Result = 0;
			if (!Runtime::CheckedAdd(*I32, Imm, Result))
			{
				ReportErrorId("diag.integer_overflow");
				return false;
			}
			*I32 = Result;
			return true;
		}
		if (auto* const U32 = std::get_if<std::uint32_t>(&Local))
		{
			*U32 += static_cast<std::uint32_t>(Imm);
			return true;
		}
		if (auto* const I64 = std::get_if<std::int64_t>(&Local))
		{
			const std::int64_t WideImm = Imm;
			if ((WideImm > 0 && *I64 > std::numeric_limits<std::int64_t>::max() - WideImm) || (WideImm < 0 && *I64 < std::numeric_limits<std::int64_t>::min() - WideImm))
			{
				ReportErrorId("diag.integer_overflow");
				return false;
			}
			*I64 += WideImm;
			return true;
		}
		if (auto* const U64 = std::get_if<std::uint64_t>(&Local))
		{
			*U64 += static_cast<std::uint64_t>(static_cast<std::int64_t>(Imm));
			return true;
		}
		if (auto* const F32 = std::get_if<float>(&Local))
		{
			*F32 += static_cast<float>(Imm);
			return true;
		}
		if (auto* const F64 = std::get_if<double>(&Local))
		{
			*F64 += static_cast<double>(Imm);
			return true;
		}
		FValue* const Resolved = Runtime::IsIntegerValue(Local) || std::holds_alternative<float>(Local) || std::holds_alternative<double>(Local)
		    ? &Local
		    : ResolveRefStorage(Local);
		if (!Resolved)
		{
			ReportError("invalid ref binding");
			return false;
		}
		const auto CompleteMutation = [&]() -> bool
		{
			if (Resolved != &Frames.back().Locals[Slot])
			{
				NotifyReferenceMutation(Frames.back().Locals[Slot]);
			}
			return !bHasError;
		};
		if (Runtime::IsIntegerValue(*Resolved))
		{
			if (!Runtime::AddIntegerImmediate(*Resolved, Imm))
			{
				ReportErrorId("diag.integer_overflow");
				return false;
			}
			return CompleteMutation();
		}
		if (auto* F32 = std::get_if<float>(Resolved))
		{
			*F32 += static_cast<float>(Imm);
			return CompleteMutation();
		}
		if (auto* F64 = std::get_if<double>(Resolved))
		{
			*F64 += static_cast<double>(Imm);
			return CompleteMutation();
		}
		FValue Right = Imm;
		FValue Result;
		if (!JitEvalArithmetic(EOpcode::Add, *Resolved, Right, Result))
		{
			return false;
		}
		if (!AssignValue(*Resolved, std::move(Result), {}))
		{
			return false;
		}
		return CompleteMutation();
	}

	bool VeroScriptVm::JitAddLocalLocal(
	    const std::uint16_t DestSlot,
	    const std::uint16_t SrcSlot)
	{
		if (Frames.empty() || SrcSlot >= Frames.back().Locals.size())
		{
			ReportError("JIT AddLocalLocal: invalid slot");
			return false;
		}
		FValue& DirectSource = Frames.back().Locals[SrcSlot];
		const FValue* const Source = Runtime::IsIntegerValue(DirectSource) || std::holds_alternative<float>(DirectSource) || std::holds_alternative<double>(DirectSource)
		    ? &DirectSource
		    : ResolveRefStorage(DirectSource);
		if (!Source)
		{
			ReportError("invalid ref binding");
			return false;
		}
		return JitAddValueToLocal(DestSlot, *Source);
	}

	bool VeroScriptVm::JitAddValueToLocal(
	    const std::uint16_t DestSlot,
	    const FValue& Source)
	{
		if (Frames.empty() || DestSlot >= Frames.back().Locals.size())
		{
			ReportError("JIT add value to local: invalid slot");
			return false;
		}
		FValue& DirectDest = Frames.back().Locals[DestSlot];
		FValue* const Dest = Runtime::IsIntegerValue(DirectDest) || std::holds_alternative<float>(DirectDest) || std::holds_alternative<double>(DirectDest)
		    ? &DirectDest
		    : ResolveRefStorage(DirectDest);
		const FValue* const Src = &Source;
		if (!Dest || !Src)
		{
			ReportError("invalid ref binding");
			return false;
		}
		const auto CompleteMutation = [&]() -> bool
		{
			if (Dest != &Frames.back().Locals[DestSlot])
			{
				NotifyReferenceMutation(Frames.back().Locals[DestSlot]);
			}
			return !bHasError;
		};
		if (auto* DestI32 = std::get_if<std::int32_t>(Dest))
		{
			if (const auto* SrcI32 = std::get_if<std::int32_t>(Src))
			{
				std::int32_t Result = 0;
				if (!Runtime::CheckedAdd(*DestI32, *SrcI32, Result))
				{
					ReportErrorId("diag.integer_overflow");
					return false;
				}
				*DestI32 = Result;
				return CompleteMutation();
			}
		}
		if (auto* DestU32 = std::get_if<std::uint32_t>(Dest))
		{
			if (const auto* SrcU32 = std::get_if<std::uint32_t>(Src))
			{
				*DestU32 = static_cast<std::uint32_t>(*DestU32 + *SrcU32);
				return CompleteMutation();
			}
		}
		if (auto* DestI64 = std::get_if<std::int64_t>(Dest))
		{
			if (const auto* SrcI64 = std::get_if<std::int64_t>(Src))
			{
				if ((*SrcI64 > 0 && *DestI64 > std::numeric_limits<std::int64_t>::max() - *SrcI64) || (*SrcI64 < 0 && *DestI64 < std::numeric_limits<std::int64_t>::min() - *SrcI64))
				{
					ReportErrorId("diag.integer_overflow");
					return false;
				}
				*DestI64 += *SrcI64;
				return CompleteMutation();
			}
		}
		if (auto* DestU64 = std::get_if<std::uint64_t>(Dest))
		{
			if (const auto* SrcU64 = std::get_if<std::uint64_t>(Src))
			{
				*DestU64 += *SrcU64;
				return CompleteMutation();
			}
		}
		if (auto* DestF32 = std::get_if<float>(Dest))
		{
			if (const auto* SrcF32 = std::get_if<float>(Src))
			{
				*DestF32 += *SrcF32;
				return CompleteMutation();
			}
		}
		if (auto* DestF64 = std::get_if<double>(Dest))
		{
			if (const auto* SrcF64 = std::get_if<double>(Src))
			{
				*DestF64 += *SrcF64;
				return CompleteMutation();
			}
		}
		if (Runtime::TryAppendStringValue(*Dest, *Src))
		{
			return CompleteMutation();
		}
		FValue Result;
		if (!JitEvalArithmetic(EOpcode::Add, *Dest, *Src, Result))
		{
			return false;
		}
		if (!AssignValue(*Dest, std::move(Result), {}))
		{
			return false;
		}
		return CompleteMutation();
	}

	bool VeroScriptVm::JitMakeLocalRef(const std::uint16_t Slot, FValue& OutRef)
	{
		if (Frames.empty() || Slot >= Frames.back().Locals.size())
		{
			ReportError("JIT MakeLocalRef: invalid slot");
			return false;
		}
		OutRef = VmDetail::MakeLocalReference(Frames.back().Locals[Slot],
		    static_cast<std::uint32_t>(Frames.size() - 1), Slot);
		return true;
	}

	bool VeroScriptVm::JitMakeGlobalRef(const std::uint16_t Slot, FValue& OutRef)
	{
		if (bDemandGlobalInitializing && !EnsureGlobalInitialized(Slot))
		{
			return false;
		}
		if (Slot >= Globals.size())
		{
			ReportErrorId("diag.invalid_global_slot");
			return false;
		}
		FGlobalRef Ref;
		Ref.Slot = Slot;
		OutRef = Ref;
		return true;
	}

	bool VeroScriptVm::JitMakeFieldRef(
	    FValue Object,
	    const std::uint16_t NameIndex,
	    FValue& OutRef)
	{
		if (!ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("invalid field name");
			return false;
		}
		auto* ObjectHandle = std::get_if<FObjectHandle>(&Object);
		if (!ObjectHandle || !*ObjectHandle || (*ObjectHandle)->bDestroyed)
		{
			ReportError("field ref requires a live object");
			return false;
		}
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**ObjectHandle);
		const std::uint16_t FieldIndex =
		    FindFieldIndexByName(TypeIndex, ActiveModule->StringPool[NameIndex]);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= (*ObjectHandle)->Fields.size())
		{
			ReportError("unknown field: " + ActiveModule->StringPool[NameIndex]);
			return false;
		}
		FThisFieldRef Ref;
		Ref.Object = *ObjectHandle;
		Ref.FieldIndex = FieldIndex;
		OutRef = Ref;
		return true;
	}

	bool VeroScriptVm::JitGetIndexRef(
	    const std::uint16_t ArrayLocalSlot,
	    FValue Index,
	    FValue& OutRef,
	    const bool bBorrow)
	{
		if (Frames.empty() || ArrayLocalSlot >= Frames.back().Locals.size())
		{
			ReportErrorId("diag.invalid_index_ref_array_slot");
			return false;
		}
		if (!Runtime::IsIntegerValue(Index))
		{
			ReportErrorId("diag.index_ref_index_must_be_int");
			return false;
		}
		std::size_t ResolvedIndex = 0;
		const bool bValidIndex = Runtime::TryGetSizeValue(Index, ResolvedIndex);
		if (!bValidIndex && bBorrow)
		{
			ReportErrorId("diag.index_ref_index_out_of_bounds");
			return false;
		}
		FIndexRef Ref;
		Ref.FrameIndex = static_cast<std::uint32_t>(Frames.size() - 1);
		Ref.ArraySlot = ArrayLocalSlot;
		Ref.Index = bValidIndex ? static_cast<std::int64_t>(ResolvedIndex) : -1;
		OutRef = Ref;
		return !bBorrow || BindElementReference(OutRef);
	}

	bool VeroScriptVm::JitGetMapEntryRef(
	    const std::uint16_t MapLocalSlot,
	    FValue Key,
	    FValue& OutRef)
	{
		if (Frames.empty() || MapLocalSlot >= Frames.back().Locals.size())
		{
			ReportErrorId("diag.invalid_index_ref_array_slot");
			return false;
		}
		FValue* const MapStorage = ResolveRefStorage(Frames.back().Locals[MapLocalSlot]);
		if (!MapStorage)
		{
			ReportError("invalid map entry ref");
			return false;
		}
		FMapEntryKey EntryKey;
		const auto BindKey = [&](const std::string& KeyTypeName) -> bool
		{
			if (VmDetail::IsSigned32KeyStorageType(KeyTypeName))
			{
				std::int32_t SignedKey = 0;
				if (!VmDetail::TryGetSigned32Key(Key, KeyTypeName, SignedKey))
				{
					ReportError("map key must be " + KeyTypeName);
					return false;
				}
				EntryKey = SignedKey;
				return true;
			}
			if (KeyTypeName == "unsigned int")
			{
				const auto* UInt = std::get_if<std::uint32_t>(&Key);
				if (!UInt)
				{
					ReportError("map key must be unsigned int");
					return false;
				}
				EntryKey = *UInt;
				return true;
			}
			if (KeyTypeName == "int64")
			{
				const auto* Int64 = std::get_if<std::int64_t>(&Key);
				if (!Int64)
				{
					ReportError("map key must be int64");
					return false;
				}
				EntryKey = *Int64;
				return true;
			}
			if (KeyTypeName == "uint64")
			{
				const auto* UInt64 = std::get_if<std::uint64_t>(&Key);
				if (!UInt64)
				{
					ReportError("map key must be uint64");
					return false;
				}
				EntryKey = *UInt64;
				return true;
			}
			if (KeyTypeName == "string")
			{
				const auto* Str = std::get_if<std::string>(&Key);
				if (!Str)
				{
					ReportError("map key must be string");
					return false;
				}
				EntryKey = *Str;
				return true;
			}
			if (KeyTypeName == "wstring")
			{
				const auto* WStr = std::get_if<std::u16string>(&Key);
				if (!WStr)
				{
					ReportError("map key must be wstring");
					return false;
				}
				EntryKey = *WStr;
				return true;
			}
			if (const auto* EnumKey = std::get_if<FEnumValue>(&Key);
			    EnumKey && EnumKey->TypeName == Runtime::EnumValueTypeName(*ActiveModule, KeyTypeName))
			{
				EntryKey = *EnumKey;
				return true;
			}
			ReportError("invalid map key type");
			return false;
		};
		if (auto* MapHandle = std::get_if<FMapHandle>(MapStorage))
		{
			if (!*MapHandle || !BindKey((*MapHandle)->KeyTypeName))
			{
				return false;
			}
		}
		else if (auto* Ordered = std::get_if<FOrderedMapHandle>(MapStorage))
		{
			if (!*Ordered || !BindKey((*Ordered)->KeyTypeName))
			{
				return false;
			}
		}
		else
		{
			ReportError("map entry ref requires map");
			return false;
		}
		FMapEntryRef Ref;
		Ref.FrameIndex = static_cast<std::uint32_t>(Frames.size() - 1);
		Ref.MapSlot = MapLocalSlot;
		Ref.SetKey(std::move(EntryKey));
		OutRef = std::move(Ref);
		return BindElementReference(OutRef);
	}

	bool VeroScriptVm::JitTryCallNativeScalar(
	    const std::uint16_t NativeIndex,
	    const FValue* const Arguments,
	    const std::uint8_t ArgumentCount,
	    bool& bOutHandled,
	    FValue& OutReturn)
	{
		bOutHandled = false;
		FMemoryPollScope MemoryPollScope(*this);
		if (!ActiveNatives || NativeIndex >= ActiveNatives->size() || ArgumentCount > 8 || (ArgumentCount != 0 && !Arguments))
		{
			return true;
		}
		const FNativeFunction& Native = (*ActiveNatives)[NativeIndex];
		// Scalar-native Linear entries validate every encoded call site against the
		// active table once before entering the frame. Do not repeat the full metadata
		// walk inside a hot native loop; other callers remain independently guarded.
		if (!JitBridgeDetail::IsScalarNativeSignature(Native, ArgumentCount))
		{
			return true;
		}
		for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
		{
			const FValue& Argument = Arguments[Index];
			if (std::holds_alternative<FLocalRef>(Argument) || std::holds_alternative<FGlobalRef>(Argument) || std::holds_alternative<FThisFieldRef>(Argument) || std::holds_alternative<FIndexRef>(Argument) || std::holds_alternative<FMapEntryRef>(Argument))
			{
				return true;
			}
		}

		bOutHandled = true;
		++JitScalarNativeFastPathCalls;
		std::array<FValue, 8> Prepared{};
		for (std::size_t Index = 0; Index < ArgumentCount; ++Index)
		{
			Prepared[Index] = Arguments[Index];
			std::string CoerceError;
			if (!Numerics::ValidateArgument(Native, Index, Prepared[Index], CoerceError))
			{
				ReportErrorId(CoerceError);
				return false;
			}
			if (!CoerceValueToNativeType(
			        Native.ParamTypes[Index], Prepared[Index], CoerceError))
			{
				ReportError(
				    CoerceError.empty()
				        ? ("native argument type mismatch: " + Native.Name)
				        : (CoerceError + ": " + Native.Name));
				return false;
			}
		}

		// Snapshot everything used after the callback. A callback may re-enter the
		// session and replace the public native table. Keep ordinary native names
		// inline so the successful hot path remains allocation-free.
		std::array<char, 96> InlineNativeName{};
		std::string OverflowNativeName;
		std::string_view NativeName;
		if (Native.Name.size() <= InlineNativeName.size())
		{
			std::memcpy(
			    InlineNativeName.data(), Native.Name.data(), Native.Name.size());
			NativeName = std::string_view(
			    InlineNativeName.data(), Native.Name.size());
		}
		else
		{
			OverflowNativeName = Native.Name;
			NativeName = OverflowNativeName;
		}
		const ENativeType ReturnType = Native.ReturnType;
		const FNativeCallback Callback = Native.Callback;
		void* const UserData = Native.UserData;
		const auto NamedError = [&](const std::string_view Prefix)
		{
			std::string Message;
			Message.reserve(Prefix.size() + NativeName.size());
			Message.append(Prefix);
			Message.append(NativeName);
			return Message;
		};

		FScriptError NativeDiagnostic;
		FNativeCallContext Context;
		Context.Args = Prepared.data();
		// Scalar-specialized natives are by-value only. Match the generic native
		// path and expose no mutable argument table rather than a non-null table
		// containing only null entries.
		Context.MutableArgs = nullptr;
		Context.ArgCount = ArgumentCount;
		Context.UserData = UserData;
		Context.HostThis = nullptr;
		// Keep the specialized scalar path observationally equivalent to
		// CallNative: host callbacks may synchronously invoke a loaded script
		// callable through the active VM without restarting the session.
		Context.ScriptInvoker = this;
		Context.MemoryServices = this;
		Context.I18nServices = this;
		Context.NumericServices = this;
		Context.DiagnosticOutput = &NativeDiagnostic;
		// A scalar call has no original reference storage, including during reentry from a ref callback.
		FNativeReferenceScope ReferenceScope(*this, nullptr, 0);

		OutReturn = std::int32_t{0};
		std::string OutError;
		bool bCallbackOk = false;
		FNativeValueScope ValueScope(*this, Prepared.data(), ArgumentCount, OutReturn);
		try
		{
			bCallbackOk = Callback(Context, OutReturn, OutError);
		}
		catch (const std::exception& Exception)
		{
			NativeDiagnostic = {};
			OutError = std::string("native callback threw: ") + Exception.what();
		}
		catch (...)
		{
			NativeDiagnostic = {};
			OutError = "native callback threw an unknown exception";
		}
		std::string RootFailure;
		const bool RootsCleaned = ValueScope.Previous || DrainNativeValueRoots(RootFailure);
		if (!bCallbackOk)
		{
			return NativeFailure(std::move(NativeDiagnostic), std::move(OutError), NativeName);
		}
		if (!RootsCleaned)
		{
			if (!bHasError)
			{
				ReportError(std::move(RootFailure));
			}
			return false;
		}
		if (ReturnType == ENativeType::Void)
		{
			return !bHasError;
		}
		if (!CoerceValueToNativeType(ReturnType, OutReturn, OutError))
		{
			ReportError(
			    OutError.empty()
			        ? NamedError("native return type mismatch: ")
			        : (OutError + ": " + std::string(NativeName)));
			return false;
		}
		return !bHasError;
	}

}
