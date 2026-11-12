// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptVm.h"

#include "VeroScriptVmJitBridgeInternal.h"

#include "VeroScriptBuiltins.h"
#include "VeroScriptConcurrent.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptDiagnostics.h"
#include "VeroScriptEnvironment.h"
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <system_error>
#include <thread>
#include <type_traits>
#include <unordered_set>

namespace VeroScript
{
	bool VeroScriptVm::JitNewObject(const std::uint16_t TypeIndex, FValue& OutObject, const bool bUninitialized)
	{
		const FObjectHandle Object = CreateObject(TypeIndex, {}, bUninitialized);
		if (!Object)
		{
			return false;
		}
		OutObject = Object;
		return true;
	}

	bool VeroScriptVm::JitLoadField(
		const FValue& Object,
		const std::uint16_t NameIndex,
		FValue& OutValue)
	{
		const auto* Handle = std::get_if<FObjectHandle>(&Object);
		if (!Handle || !*Handle || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT load field: invalid object or name");
			return false;
		}
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**Handle);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1))
		{
			if ((*Handle)->bDestroyed)
			{
				ReportError("field receiver has already been destroyed");
			}
			else
			{
				ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			}
			return false;
		}
		if (FieldIndex >= (*Handle)->Fields.size())
		{
			ReportError("JIT load field: invalid field index");
			return false;
		}
		FValue* const Storage = UnwrapClosureReferenceCell(&(*Handle)->Fields[FieldIndex]);
		if (!Storage)
		{
			ReportError("JIT load field: invalid referenced storage");
			return false;
		}
		// Scalar fields carry no nested mutation origin and need no ownership work.
		// Copy them directly from the rooted object ABI instead of entering the
		// generic field/value path on every LinearTrampoline access.
		if (const auto* V = std::get_if<std::int32_t>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<std::uint32_t>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<std::int64_t>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<std::uint64_t>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<double>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<float>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<bool>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<std::int8_t>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<std::uint8_t>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<std::int16_t>(Storage))
		{
			OutValue = *V;
			return true;
		}
		if (const auto* V = std::get_if<std::uint16_t>(Storage))
		{
			OutValue = *V;
			return true;
		}
		return GetField(**Handle, FieldIndex, OutValue, {});
	}

	bool VeroScriptVm::JitStoreField(
		const FValue& Object,
		const std::uint16_t NameIndex,
		const FValue& Value,
		const bool bNotifyMutation)
	{
		const auto* Handle = std::get_if<FObjectHandle>(&Object);
		if (!Handle || !*Handle || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT store field: invalid object or name");
			return false;
		}
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**Handle);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1))
		{
			if ((*Handle)->bDestroyed)
			{
				ReportError("field receiver has already been destroyed");
			}
			else
			{
				ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			}
			return false;
		}
		if (FieldIndex >= (*Handle)->Fields.size())
		{
			ReportError("JIT store field: invalid field index");
			return false;
		}
		FValue* const Storage = UnwrapClosureReferenceCell(&(*Handle)->Fields[FieldIndex]);
		if (!Storage)
		{
			ReportError("JIT store field: invalid referenced storage");
			return false;
		}
		const auto StoreExactScalar = [&](auto* Destination) -> bool
		{
			using T = std::remove_pointer_t<decltype(Destination)>;
			const auto* Source = std::get_if<T>(&Value);
			if (!Source)
			{
				return false;
			}
			*Destination = *Source;
			if (bNotifyMutation)
			{
				NotifyFieldMutation(**Handle, FieldIndex);
			}
			return true;
		};
		if (auto* V = std::get_if<std::int32_t>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<std::uint32_t>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<std::int64_t>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<std::uint64_t>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<double>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<float>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<bool>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<std::int8_t>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<std::uint8_t>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<std::int16_t>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		if (auto* V = std::get_if<std::uint16_t>(Storage); V && StoreExactScalar(V))
		{
			return true;
		}
		return SetField(**Handle, FieldIndex, Value, {}, bNotifyMutation);
	}

	bool VeroScriptVm::JitIncField(FValue Object, const std::uint16_t NameIndex)
	{
		auto* Handle = std::get_if<FObjectHandle>(&Object);
		if (!Handle || !*Handle || !ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT IncField: invalid object or name");
			return false;
		}
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(**Handle);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1) || FieldIndex >= (*Handle)->Fields.size())
		{
			if ((*Handle)->bDestroyed)
			{
				ReportError("field receiver has already been destroyed");
			}
			else
			{
				ReportErrorId("diag.unknown_field", {}, {ActiveModule->StringPool[NameIndex]});
			}
			return false;
		}
		FValue* const Resolved =
			UnwrapClosureReferenceCell(&(*Handle)->Fields[FieldIndex]);
		if (!Resolved)
		{
			ReportError("JIT IncField: invalid reference storage");
			return false;
		}
		FValue& Slot = *Resolved;
		if (Runtime::IsIntegerValue(Slot))
		{
			if (!Runtime::AddIntegerImmediate(Slot, 1))
			{
				ReportErrorId("diag.integer_overflow");
				return false;
			}
			NotifyFieldMutation(**Handle, FieldIndex);
			return true;
		}
		if (auto* I32 = std::get_if<std::int32_t>(&Slot))
		{
			std::int32_t Result = 0;
			if (!Runtime::CheckedAdd(*I32, 1, Result))
			{
				ReportErrorId("diag.integer_overflow");
				return false;
			}
			*I32 = Result;
			NotifyFieldMutation(**Handle, FieldIndex);
			return true;
		}
		if (auto* U32 = std::get_if<std::uint32_t>(&Slot))
		{
			*U32 = static_cast<std::uint32_t>(*U32 + 1u);
			NotifyFieldMutation(**Handle, FieldIndex);
			return true;
		}
		if (auto* F64 = std::get_if<double>(&Slot))
		{
			*F64 += 1.0;
			NotifyFieldMutation(**Handle, FieldIndex);
			return true;
		}
		if (auto* F32 = std::get_if<float>(&Slot))
		{
			*F32 += 1.0f;
			NotifyFieldMutation(**Handle, FieldIndex);
			return true;
		}
		if (auto* I64 = std::get_if<std::int64_t>(&Slot))
		{
			if (*I64 == std::numeric_limits<std::int64_t>::max())
			{
				ReportErrorId("diag.integer_overflow");
				return false;
			}
			++*I64;
			NotifyFieldMutation(**Handle, FieldIndex);
			return true;
		}
		if (auto* U64 = std::get_if<std::uint64_t>(&Slot))
		{
			++*U64;
			NotifyFieldMutation(**Handle, FieldIndex);
			return true;
		}
		FValue One = std::int32_t{1};
		FValue Sum;
		if (!EvalArithmetic(EOpcode::Add, Slot, One, Sum, {}))
		{
			return false;
		}
		return AssignValue(Slot, std::move(Sum), {});
	}

	bool VeroScriptVm::JitTryIncLocalField(
		const std::uint16_t Slot,
		const std::uint16_t NameIndex,
		bool& OutHandled)
	{
		OutHandled = false;
		if (!ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			return true;
		}
		// Linear kernels execute against a direct local window which is distinct
		// from the interpreter frame. Resolving through the shared Linear ABI keeps
		// strict-JIT, Mixed and interpreted fallback behavior aligned.
		FValue* const DirectLocal = JitBorrowDirectLinearLocal(Slot);
		FValue* const Local = DirectLocal ? ResolveRefStorage(*DirectLocal) : nullptr;
		auto* const Handle = Local ? std::get_if<FObjectHandle>(Local) : nullptr;
		if (!Handle || !*Handle)
		{
			return true;
		}
		FScriptObject& Object = **Handle;
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(Object);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1)
			|| FieldIndex >= Object.Fields.size())
		{
			return true;
		}
		FValue* const Field = UnwrapClosureReferenceCell(&Object.Fields[FieldIndex]);
		if (!Field)
		{
			return true;
		}
		if (Runtime::IsIntegerValue(*Field))
		{
			OutHandled = true;
			if (!Runtime::AddIntegerImmediate(*Field, 1))
			{
				ReportErrorId("diag.integer_overflow");
				return false;
			}
		}
		else if (auto* F64 = std::get_if<double>(Field))
		{
			*F64 += 1.0;
			OutHandled = true;
		}
		else if (auto* F32 = std::get_if<float>(Field))
		{
			*F32 += 1.0f;
			OutHandled = true;
		}
		if (OutHandled)
		{
			NotifyFieldMutationJitFast(Object, FieldIndex);
		}
		return !bHasError;
	}

	bool VeroScriptVm::JitTryIncAndAccumulateLocalI32Field(
		const std::uint16_t ObjectSlot,
		const std::uint16_t NameIndex,
		const std::uint16_t AccumulatorSlot,
		bool& OutHandled,
		EJitFieldAccumulateFailure& OutFailure)
	{
		OutHandled = false;
		OutFailure = EJitFieldAccumulateFailure::None;
		// This fused operation reads both the field and accumulator after the field
		// write barrier. A mutation callback may re-enter the VM, move frame locals,
		// or replace object field storage, invalidating both borrowed pointers. Let
		// the ordinary opcodes re-resolve each value in that observable mode.
		if (MutationCallback)
		{
			return true;
		}
		if (!ActiveModule || NameIndex >= ActiveModule->StringPool.size())
		{
			return true;
		}
		FValue* const DirectObject = JitBorrowDirectLinearLocal(ObjectSlot);
		FValue* const DirectAccumulator = JitBorrowDirectLinearLocal(AccumulatorSlot);
		FValue* const ObjectValue = DirectObject
			? ResolveRefStorage(*DirectObject)
			: nullptr;
		auto* const Handle = ObjectValue
			? std::get_if<FObjectHandle>(ObjectValue)
			: nullptr;
		FValue* const AccumulatorValue = DirectAccumulator
			? ResolveRefStorage(*DirectAccumulator)
			: nullptr;
		auto* const Accumulator = AccumulatorValue
			? std::get_if<std::int32_t>(AccumulatorValue)
			: nullptr;
		if (!Handle || !*Handle || !Accumulator)
		{
			return true;
		}
		FScriptObject& Object = **Handle;
		const std::uint16_t TypeIndex = ResolveObjectTypeIndex(Object);
		const std::uint16_t FieldIndex = FindJitFieldIndex(TypeIndex, NameIndex);
		if (FieldIndex == static_cast<std::uint16_t>(-1)
			|| FieldIndex >= Object.Fields.size())
		{
			return true;
		}
		FValue* const FieldValue = UnwrapClosureReferenceCell(
			&Object.Fields[FieldIndex]);
		auto* const Field = FieldValue
			? std::get_if<std::int32_t>(FieldValue)
			: nullptr;
		if (!Field)
		{
			return true;
		}

		OutHandled = true;
		if (*Field == std::numeric_limits<std::int32_t>::max())
		{
			OutFailure = EJitFieldAccumulateFailure::FieldIncrement;
			ReportErrorId("diag.integer_overflow");
			return false;
		}
		++*Field;
		NotifyFieldMutationJitFast(Object, FieldIndex);
		std::int32_t Sum = 0;
		if (!Runtime::CheckedAdd(*Accumulator, *Field, Sum))
		{
			OutFailure = EJitFieldAccumulateFailure::AccumulatorAddition;
			ReportErrorId("diag.integer_overflow");
			return false;
		}
		*Accumulator = Sum;
		return true;
	}

	bool VeroScriptVm::JitCloneStruct(FValue& InOutValue)
	{
		if (const auto* HostHandle = std::get_if<FHostObjectHandle>(&InOutValue);
			HostHandle && *HostHandle && ActiveNatives)
		{
			const std::string CloneName = MakeHostCloneNativeName((*HostHandle)->TypeName);
			const std::uint16_t CloneIndex = FindActiveNativeIndexByName(CloneName);
			if (CloneIndex != static_cast<std::uint16_t>(-1))
			{
				const FHostValueCallback StampCallback =
					(*HostHandle)->CloneCallback
					? (*HostHandle)->CloneCallback
					: (*ActiveNatives)[CloneIndex].Callback;
				void* const StampUserData =
					(*HostHandle)->CloneCallback
					? (*HostHandle)->CloneUserData
					: (*ActiveNatives)[CloneIndex].UserData;
				const std::size_t StackBefore = OperandStack.size();
				const auto StampOwner = (*HostHandle)->CloneCallback
					? (*HostHandle)->CloneOwner : (*ActiveNatives)[CloneIndex].CallbackOwner;
				Push(InOutValue);
				if (!CallNative(CloneIndex, 1, {}))
				{
					(void)DiscardExecutionAbove(Frames.size(), StackBefore, {});
					return false;
				}
				if (OperandStack.size() != StackBefore + 1)
				{
					ReportError("JIT CloneStruct produced unexpected stack depth");
					return false;
				}
				InOutValue = Pop();
				if (const auto* Cloned = std::get_if<FHostObjectHandle>(&InOutValue);
					Cloned && *Cloned)
				{
					(*Cloned)->CloneCallback = StampCallback;
					(*Cloned)->CloneUserData = StampUserData;
					(*Cloned)->CloneOwner = StampOwner;
				}
				return !bHasError;
			}
		}
		FValue Cloned;
		if (!CopyLanguageValue(InOutValue, Cloned, {}))
		{
			return false;
		}
		InOutValue = std::move(Cloned);
		return true;
	}

}
