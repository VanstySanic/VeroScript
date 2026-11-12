// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptVm.h"

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
#include "VeroScriptVmInternal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_set>

namespace VeroScript
{
	FNativeArray VeroScriptVm::CreateNativeArray(const std::string& TypeName, const FSourceLocation& Location,
	    const bool bUninitialized)
	{
		FNativeArray Array;
		std::string ElementType;
		std::uint32_t Length = 0;
		if (!Runtime::ParseNativeArrayType(TypeName, ElementType, Length))
		{
			ReportError("invalid native array type: " + TypeName, Location);
			return Array;
		}
		std::string LimitError;
		if (!Runtime::ValidateNativeArrayTypeLimits(TypeName, &LimitError))
		{
			ReportErrorId("diag.native_array_size_exceeds_limit", Location);
			return Array;
		}

		Array.TypeName() = TypeName;
		Array.ElementTypeName() = ElementType;
		Array.Length() = Length;
		Array.Storage().Elements.reserve(Length);
		if (bUninitialized)
		{
			Array.Storage().Elements.resize(Length);
			return Array;
		}
		for (std::uint32_t Index = 0; Index < Length; ++Index)
		{
			FValue Element = CreateDefaultValue(ElementType, Location);
			Array.Storage().Elements.push_back(std::move(Element));
			if (bHasError || bHasPendingScriptException || bExitRequested)
			{
				// Earlier elements are complete language values. Release them in reverse
				// order, preserving the interrupted initializer's error or transfer.
				(void)ReleaseValueList(Array.Storage().Elements, Location);
				return FNativeArray{};
			}
		}
		return Array;
	}

	FDynamicArrayHandle VeroScriptVm::CreateDynamicArray(
	    const std::string& TypeName,
	    const FSourceLocation& Location)
	{
		std::string ElementType;
		if (!Runtime::ParseDynamicArrayElementType(TypeName, ElementType))
		{
			ReportError("invalid dynamic array type: " + TypeName, Location);
			return nullptr;
		}

		auto Array = std::make_shared<FDynamicArray>();
		Array->TypeName = TypeName;
		Array->ElementTypeName = ElementType;
		// Primitive array storage is a property of array<T>, not a JIT-private
		// representation. Generic operations preserve it across calls and fields.
		(void)Array->EnableScalarStorage();
		return Array;
	}

	bool VeroScriptVm::AppendDynamicArrayValue(
	    FDynamicArray& Array,
	    const FValue& Value,
	    const FSourceLocation& Location)
	{
		if (Array.HasScalarElements())
		{
			if (!Array.TryPushScalar(Value))
			{
				ReportError("scalar array value type mismatch", Location);
				return false;
			}
			return true;
		}
		return AppendClonedValue(Array.Elements, Value, Location);
	}

	void VeroScriptVm::ReserveDynamicArrayStorage(
	    FDynamicArray& Array,
	    const std::size_t Capacity)
	{
		if (Array.HasScalarElements())
		{
			Array.ReserveScalar(Capacity);
		}
		else
		{
			Array.Elements.reserve(Capacity);
		}
	}

	FMapHandle VeroScriptVm::CreateMap(const std::string& TypeName, const FSourceLocation& Location)
	{
		std::string KeyType;
		std::string ValueType;
		if (Runtime::IsMultiSetTypeName(TypeName))
		{
			if (!Runtime::ParseMultiSetElementType(TypeName, KeyType))
			{
				ReportError("invalid multiset type: " + TypeName, Location);
				return nullptr;
			}
			ValueType = "array<bool>";
		}
		else if (!Runtime::ParseMapType(TypeName, KeyType, ValueType))
		{
			ReportError("invalid map type: " + TypeName, Location);
			return nullptr;
		}

		auto Map = std::make_shared<FMap>();
		Map->TypeName = TypeName;
		Map->KeyTypeName = KeyType;
		Map->ValueTypeName = ValueType;
		return Map;
	}

	FSetHandle VeroScriptVm::CreateSet(const std::string& TypeName, const FSourceLocation& Location)
	{
		std::string ElementType;
		if (!Runtime::ParseSetElementType(TypeName, ElementType))
		{
			ReportError("invalid set type: " + TypeName, Location);
			return nullptr;
		}

		auto Set = std::make_shared<FSet>();
		Set->TypeName = TypeName;
		Set->ElementTypeName = ElementType;
		return Set;
	}

	FQueueHandle VeroScriptVm::CreateQueue(const std::string& TypeName, const FSourceLocation& Location)
	{
		std::string ElementType;
		if (!Runtime::ParseQueueElementType(TypeName, ElementType))
		{
			ReportError("invalid queue type: " + TypeName, Location);
			return nullptr;
		}
		if (Runtime::IsPriorityQueueTypeName(TypeName) && !Runtime::IsAllowedPriorityQueueElementType(ElementType))
		{
			ReportError(
			    "priority_queue element type lacks a built-in total order: " + ElementType,
			    Location);
			return nullptr;
		}

		auto Queue = std::make_shared<FQueue>();
		Queue->TypeName = TypeName;
		Queue->ElementTypeName = ElementType;
		return Queue;
	}

	FStackHandle VeroScriptVm::CreateStack(const std::string& TypeName, const FSourceLocation& Location)
	{
		std::string ElementType;
		if (!Runtime::ParseStackElementType(TypeName, ElementType))
		{
			ReportError("invalid stack type: " + TypeName, Location);
			return nullptr;
		}

		auto Stack = std::make_shared<FStack>();
		Stack->TypeName = TypeName;
		Stack->ElementTypeName = ElementType;
		return Stack;
	}

	FChannelHandle VeroScriptVm::CreateChannel(const std::string& TypeName, const FSourceLocation& Location)
	{
		std::string ElementType;
		if (!Runtime::ParseChannelElementType(TypeName, ElementType))
		{
			ReportError("invalid channel type (expected scalar or channel): " + TypeName, Location);
			return nullptr;
		}

		auto Channel = std::make_shared<FChannel>();
		if (!Channel->TryBindTypeNames(TypeName, ElementType))
		{
			ReportError("failed to initialize channel type metadata", Location);
			return nullptr;
		}
		LiveChannels.push_back(Channel);
		return Channel;
	}

	FOrderedMapHandle VeroScriptVm::CreateOrderedMap(
	    const std::string& TypeName,
	    const FSourceLocation& Location)
	{
		std::string KeyType;
		std::string ValueType;
		if (Runtime::IsOrderedMultiSetTypeName(TypeName))
		{
			if (!Runtime::ParseMultiSetElementType(TypeName, KeyType))
			{
				ReportError("invalid ordered_multiset type: " + TypeName, Location);
				return nullptr;
			}
			ValueType = "array<bool>";
		}
		else if (!Runtime::ParseOrderedMapType(TypeName, KeyType, ValueType))
		{
			ReportError("invalid ordered_map type: " + TypeName, Location);
			return nullptr;
		}

		auto OrderedMap = std::make_shared<FOrderedMap>();
		OrderedMap->TypeName = TypeName;
		OrderedMap->KeyTypeName = KeyType;
		OrderedMap->ValueTypeName = ValueType;
		return OrderedMap;
	}

	FDequeHandle VeroScriptVm::CreateDeque(const std::string& TypeName, const FSourceLocation& Location)
	{
		std::string ElementType;
		if (!Runtime::ParseDequeElementType(TypeName, ElementType))
		{
			ReportError("invalid deque type: " + TypeName, Location);
			return nullptr;
		}

		auto Deque = std::make_shared<FDeque>();
		Deque->TypeName = TypeName;
		Deque->ElementTypeName = ElementType;
		return Deque;
	}

	bool VeroScriptVm::ValueMatchesElementType(
	    const FValue& Value,
	    const std::string& ElementTypeName,
	    const FSourceLocation& Location)
	{
		if (ElementTypeName == "variant") return ValidateVariantValue(Value);
		if (IsClosureReferenceCell(Value))
		{
			const FValue* const Visible = UnwrapClosureReferenceCell(&Value);
			return Visible && ValueMatchesElementType(*Visible, ElementTypeName, Location);
		}
		if (ElementTypeName == "bool")
		{
			return std::holds_alternative<bool>(Value);
		}
		if (ElementTypeName == "string")
		{
			return std::holds_alternative<std::string>(Value);
		}
		if (ElementTypeName == "wstring")
		{
			return std::holds_alternative<std::u16string>(Value);
		}
		if (ElementTypeName == "float")
		{
			return std::holds_alternative<float>(Value);
		}
		if (ElementTypeName == "double")
		{
			return std::holds_alternative<double>(Value);
		}
		if (Runtime::IsUnsignedTypeName(ElementTypeName))
		{
			if (ElementTypeName == "unsigned char")
			{
				return std::holds_alternative<std::uint8_t>(Value);
			}
			if (ElementTypeName == "unsigned short")
			{
				return std::holds_alternative<std::uint16_t>(Value);
			}
			return ElementTypeName == "uint64" || ElementTypeName == "unsigned long"
			    ? std::holds_alternative<std::uint64_t>(Value)
			    : std::holds_alternative<std::uint32_t>(Value);
		}
		if (ElementTypeName == "int64" || ElementTypeName == "long")
		{
			return std::holds_alternative<std::int64_t>(Value);
		}
		if (ElementTypeName == "char")
		{
			return std::holds_alternative<std::int8_t>(Value);
		}
		if (ElementTypeName == "short")
		{
			return std::holds_alternative<std::int16_t>(Value);
		}
		if (ElementTypeName == "int")
		{
			return std::holds_alternative<std::int32_t>(Value);
		}
		const auto MatchScriptObject = [&](const FObjectHandle& Object) -> bool
		{
			if (!Object)
			{
				// Class/interface-typed slots are handles and may be null; struct slots may not.
				const std::uint16_t TypeIndex = FindTypeIndex(ElementTypeName);
				if (TypeIndex != static_cast<std::uint16_t>(-1) && ActiveModule && ActiveModule->Types[TypeIndex].Kind == EScriptObjectKind::Class)
				{
					return true;
				}
				std::uint16_t InterfaceIndex = static_cast<std::uint16_t>(-1);
				return ActiveModule && ModuleMeta::FindInterfaceIndex(*ActiveModule, ElementTypeName, InterfaceIndex);
			}
			const auto ActualIndex = Object->RuntimeTypeModule == ActiveModule &&
			        Object->RuntimeTypeGeneration == ProgramGeneration
			    ? Object->RuntimeTypeIndex : std::uint16_t{0xFFFF};
			const std::string& ActualTypeName = Object->GetTypeName();
			return ActualTypeName == ElementTypeName || IsActiveObjectTypeAssignableTo(ActualTypeName, ElementTypeName, ActualIndex);
		};
		const auto* ObjectHandle = std::get_if<FObjectHandle>(&Value);
		// Primitive and variant spellings were checked above. Only structured
		// names can select a container or function branch below.
		if (ObjectHandle && ElementTypeName.find_first_of("<[(") == std::string::npos)
		{
			return MatchScriptObject(*ObjectHandle);
		}
		if (Runtime::IsDynamicArrayTypeName(ElementTypeName))
		{
			const auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&Value);
			return DynamicArray && *DynamicArray && VmDetail::RuntimeTypeNamesEqual((*DynamicArray)->TypeName, ElementTypeName);
		}
		if (Runtime::IsMapTypeName(ElementTypeName) || Runtime::IsMultiSetTypeName(ElementTypeName))
		{
			const auto* MapHandle = std::get_if<FMapHandle>(&Value);
			return MapHandle && *MapHandle && VmDetail::RuntimeTypeNamesEqual((*MapHandle)->TypeName, ElementTypeName);
		}
		if (Runtime::IsSetTypeName(ElementTypeName))
		{
			const auto* SetHandle = std::get_if<FSetHandle>(&Value);
			return SetHandle && *SetHandle && (*SetHandle)->TypeName == ElementTypeName;
		}
		if (Runtime::IsQueueTypeName(ElementTypeName))
		{
			const auto* QueueHandle = std::get_if<FQueueHandle>(&Value);
			return QueueHandle && *QueueHandle && VmDetail::RuntimeTypeNamesEqual((*QueueHandle)->TypeName, ElementTypeName);
		}
		if (Runtime::IsStackTypeName(ElementTypeName))
		{
			const auto* StackHandle = std::get_if<FStackHandle>(&Value);
			return StackHandle && *StackHandle && VmDetail::RuntimeTypeNamesEqual((*StackHandle)->TypeName, ElementTypeName);
		}
		if (Runtime::IsOrderedMapTypeName(ElementTypeName) || Runtime::IsOrderedMultiSetTypeName(ElementTypeName))
		{
			const auto* OrderedMapHandle = std::get_if<FOrderedMapHandle>(&Value);
			return OrderedMapHandle && *OrderedMapHandle && VmDetail::RuntimeTypeNamesEqual((*OrderedMapHandle)->TypeName, ElementTypeName);
		}
		if (Runtime::IsDequeTypeName(ElementTypeName))
		{
			const auto* DequeHandle = std::get_if<FDequeHandle>(&Value);
			return DequeHandle && *DequeHandle && VmDetail::RuntimeTypeNamesEqual((*DequeHandle)->TypeName, ElementTypeName);
		}
		if (Runtime::IsChannelTypeName(ElementTypeName))
		{
			const auto* ChannelHandle = std::get_if<FChannelHandle>(&Value);
			if (!ChannelHandle || !*ChannelHandle)
			{
				return false;
			}
			std::string ActualType;
			std::string ActualElement;
			(*ChannelHandle)->GetTypeNames(ActualType, ActualElement);
			return NativeElementTypeNamesEqual(ActualType, ElementTypeName);
		}
		if (Runtime::IsNativeArrayTypeName(ElementTypeName) || Runtime::IsScriptArgsArrayTypeName(ElementTypeName))
		{
			const auto* NativeArray = std::get_if<FNativeArray>(&Value);
			return NativeArray && VmDetail::RuntimeTypeNamesEqual(NativeArray->TypeName(), ElementTypeName);
		}
		if (Runtime::IsFunctionTypeName(ElementTypeName))
		{
			const auto* FunctionHandle = std::get_if<FFunctionHandle>(&Value);
			if (!FunctionHandle)
			{
				return false;
			}
			if (!*FunctionHandle)
			{
				return true;
			}
			if ((*FunctionHandle)->ProgramGeneration != ProgramGeneration)
			{
				return false;
			}
			std::string ActualType = (*FunctionHandle)->TypeName;
			if (ActualType.empty() && ActiveModule && (*FunctionHandle)->FunctionIndex < ActiveModule->Functions.size())
			{
				ActualType =
				    ActiveModule->Functions[(*FunctionHandle)->FunctionIndex].ClosureTypeName;
			}
			return VmDetail::RuntimeTypeNamesEqual(ActualType, ElementTypeName);
		}
		if (ObjectHandle)
		{
			return MatchScriptObject(*ObjectHandle);
		}
		if (const auto* HostHandle = std::get_if<FHostObjectHandle>(&Value))
		{
			if (!*HostHandle)
			{
				// Host class handles may be null; empty HostObject is rejected by typed natives.
				return true;
			}
			return HostObjectMatchesType(*HostHandle, ElementTypeName);
		}
		if (const auto* EnumValue = std::get_if<FEnumValue>(&Value))
		{
			return EnumValue->TypeName == ElementTypeName || (ActiveModule && EnumValue->TypeName == Runtime::EnumValueTypeName(*ActiveModule, ElementTypeName));
		}
		return false;
	}

	bool VeroScriptVm::CheckedCastValue(
	    FValue& Value,
	    const std::string& TargetTypeName,
	    const FSourceLocation& Location)
	{
		if (TryCheckedCastValue(Value, TargetTypeName))
		{
			return true;
		}
		ReportError("cast failed: value is not convertible to " + TargetTypeName, Location);
		return false;
	}

	bool VeroScriptVm::TryCheckedCastValue(
	    FValue& Value,
	    const std::string& TargetTypeName)
	{
		if (TargetTypeName.empty())
		{
			return false;
		}

		const auto FindScriptEnum = [this, &TargetTypeName]() -> const FBytecodeEnumDesc*
		{
			if (!ActiveModule)
			{
				return nullptr;
			}
			for (const FBytecodeEnumDesc& EnumDesc : ActiveModule->Enums)
			{
				if (EnumDesc.bScoped && ModuleMeta::TypeNameMatches(EnumDesc.Name, TargetTypeName))
				{
					return &EnumDesc;
				}
			}
			return nullptr;
		};
		if (const auto* const Enum = std::get_if<FEnumValue>(&Value))
		{
			// Keep exact FEnumValue identity for host and script strong enums alike.
			// Strong host enums are recorded in Module.Enums by the compiler so a
			// numeric source can only become a known declared enum below.
			if (ModuleMeta::TypeNameMatches(Enum->TypeName, TargetTypeName))
			{
				return true;
			}
			if (const FBytecodeEnumDesc* const TargetEnum = FindScriptEnum())
			{
				return ModuleMeta::TypeNameMatches(Enum->TypeName, TargetEnum->Name);
			}
			if (Runtime::IsPrimitiveValueTypeName(TargetTypeName) && TargetTypeName != "bool" && TargetTypeName != "string" && TargetTypeName != "wstring")
			{
				// Replacing the variant destroys the enum that owns this numeric field.
				const std::int64_t NumericValue = Enum->Value;
				Value = NumericValue;
				std::string CoerceError;
				return Runtime::CoerceValueToPrimitiveTypeName(
				    TargetTypeName, Value, CoerceError);
			}
			return false;
		}

		if (const FBytecodeEnumDesc* const TargetEnum = FindScriptEnum())
		{
			FValue Converted = Value;
			std::string CoerceError;
			if (!Runtime::CoerceValueToPrimitiveTypeName(
			        TargetEnum->UnderlyingTypeName, Converted, CoerceError))
			{
				return false;
			}
			std::int64_t EnumValue = 0;
			if (const auto* const Int8Value = std::get_if<std::int8_t>(&Converted))
			{
				EnumValue = *Int8Value;
			}
			else if (const auto* const UInt8Value = std::get_if<std::uint8_t>(&Converted))
			{
				EnumValue = *UInt8Value;
			}
			else if (const auto* const Int16Value = std::get_if<std::int16_t>(&Converted))
			{
				EnumValue = *Int16Value;
			}
			else if (const auto* const UInt16Value = std::get_if<std::uint16_t>(&Converted))
			{
				EnumValue = *UInt16Value;
			}
			else if (const auto* const Int32Value = std::get_if<std::int32_t>(&Converted))
			{
				EnumValue = *Int32Value;
			}
			else if (const auto* const UInt32Value = std::get_if<std::uint32_t>(&Converted))
			{
				EnumValue = *UInt32Value;
			}
			else if (const auto* const Int64Value = std::get_if<std::int64_t>(&Converted))
			{
				EnumValue = *Int64Value;
			}
			else if (const auto* const UInt64Value = std::get_if<std::uint64_t>(&Converted))
			{
				if (*UInt64Value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
				{
					return false;
				}
				EnumValue = static_cast<std::int64_t>(*UInt64Value);
			}
			else
			{
				return false;
			}
			Value = FEnumValue{Runtime::EnumValueTypeName(*ActiveModule, TargetEnum->Name), EnumValue};
			return true;
		}
		if (Runtime::IsPrimitiveValueTypeName(TargetTypeName))
		{
			std::string CoerceError;
			return Runtime::CoerceValueToPrimitiveTypeName(
			    TargetTypeName, Value, CoerceError);
		}

		if (const auto* const Object = std::get_if<FObjectHandle>(&Value);
		    Object && !*Object)
		{
			// ConstNull is represented as an empty script-object handle. A checked
			// conversion to a script class/interface keeps that representation; all
			// script value types are rejected. The remaining compiler-approved
			// reference domain is a host class, whose ABI requires HostObject even
			// when the handle itself is empty.
			if (ValueMatchesElementType(Value, TargetTypeName, {}))
			{
				return true;
			}
			const std::uint16_t TypeIndex = FindTypeIndex(TargetTypeName);
			if (TypeIndex != static_cast<std::uint16_t>(-1))
			{
				return false;
			}
			Value = FHostObjectHandle{};
			return true;
		}

		return ValueMatchesElementType(Value, TargetTypeName, {});
	}

	bool VeroScriptVm::ValidateAndStampNativeContainerValue(
	    FValue& Value,
	    const std::string& ExpectedTypeName,
	    const FSourceLocation& Location,
	    const FNativeTypeDesc* const ExpectedDesc)
	{
		const std::string Expected =
		    CanonicalizeNativeElementTypeName(ExpectedTypeName);
		if (Expected == "variant") return ValidateVariantValue(Value);
		const auto DescArgument = [ExpectedDesc](const std::size_t Index)
		    -> const FNativeTypeDesc*
		{
			return ExpectedDesc && Index < ExpectedDesc->TypeArguments.size()
			    ? &ExpectedDesc->TypeArguments[Index]
			    : nullptr;
		};
		std::string ElementType;
		if (Runtime::ParseDynamicArrayElementType(Expected, ElementType))
		{
			auto* const Handle = std::get_if<FDynamicArrayHandle>(&Value);
			if (!Handle || !*Handle)
			{
				return false;
			}
			if ((!(*Handle)->ElementTypeName.empty() && !NativeElementTypeNamesEqual((*Handle)->ElementTypeName, ElementType)) || (!(*Handle)->TypeName.empty() && (*Handle)->TypeName != "array" && !NativeElementTypeNamesEqual((*Handle)->TypeName, Expected)))
			{
				return false;
			}
			// Native callbacks are exposed only the boxed Elements ABI. A callback
			// that installs VM-private scalar storage after entry is malformed.
			if ((*Handle)->HasScalarElements())
			{
				return false;
			}
			for (FValue& Element : (*Handle)->Elements)
			{
				if (!ValidateAndStampNativeContainerValue(
				        Element, ElementType, Location, DescArgument(0)))
				{
					return false;
				}
			}
			(*Handle)->ElementTypeName = ElementType;
			(*Handle)->TypeName = Expected;
			return true;
		}

		const FNativeTypeDesc* const SequenceElementDesc = DescArgument(0);
		const auto ValidateSequence =
		    [this, &Expected, &Location, SequenceElementDesc](
		        auto* const Handle,
		        const std::string& ElementType,
		        const std::string_view BareType) -> bool
		{
			if (!Handle || !*Handle)
			{
				return false;
			}
			if ((!(*Handle)->ElementTypeName.empty() && !NativeElementTypeNamesEqual((*Handle)->ElementTypeName, ElementType)) || (!(*Handle)->TypeName.empty() && (*Handle)->TypeName != BareType && !NativeElementTypeNamesEqual((*Handle)->TypeName, Expected)))
			{
				return false;
			}
			for (FValue& Element : (*Handle)->Elements)
			{
				if (!ValidateAndStampNativeContainerValue(
				        Element, ElementType, Location, SequenceElementDesc))
				{
					return false;
				}
			}
			(*Handle)->ElementTypeName = ElementType;
			(*Handle)->TypeName = Expected;
			return true;
		};
		if (Runtime::ParseQueueElementType(Expected, ElementType))
		{
			auto* const Handle = std::get_if<FQueueHandle>(&Value);
			if (!ValidateSequence(Handle, ElementType, "queue"))
			{
				return false;
			}
			if (Runtime::IsPriorityQueueTypeName(Expected))
			{
				// Native callbacks construct the public FQueue representation directly.
				// Its element sequence is not required to arrive in heap order, so
				// establish the same maximum-first invariant as script literals/push.
				std::make_heap(
				    (*Handle)->Elements.begin(),
				    (*Handle)->Elements.end(),
				    VmDetail::PriorityValueLess);
			}
			return true;
		}
		if (Runtime::ParseStackElementType(Expected, ElementType))
		{
			return ValidateSequence(
			    std::get_if<FStackHandle>(&Value), ElementType, "stack");
		}
		if (Runtime::ParseDequeElementType(Expected, ElementType))
		{
			return ValidateSequence(
			    std::get_if<FDequeHandle>(&Value), ElementType, "deque");
		}
		if (Runtime::ParseChannelElementType(Expected, ElementType))
		{
			auto* const Handle = std::get_if<FChannelHandle>(&Value);
			if (!Handle || !*Handle)
			{
				return false;
			}
			std::string ActualType;
			std::string ActualElement;
			(*Handle)->GetTypeNames(ActualType, ActualElement);
			if ((!ActualElement.empty() && !NativeElementTypeNamesEqual(ActualElement, ElementType)) || (!ActualType.empty() && ActualType != "channel" && !NativeElementTypeNamesEqual(ActualType, Expected)))
			{
				return false;
			}
			// Channel contents are synchronized and intentionally opaque here.
			return (*Handle)->TryBindTypeNames(Expected, ElementType);
		}

		std::string KeyType;
		std::string ValueType;
		const bool bOrderedMultiMap = Runtime::IsOrderedMultiMapTypeName(Expected) && Runtime::SplitOrderedMapType(Expected, KeyType, ValueType);
		if (bOrderedMultiMap)
		{
			ValueType = "array<" + ValueType + ">";
		}
		const bool bOrderedMultiSet = Runtime::IsOrderedMultiSetTypeName(Expected) && Runtime::ParseMultiSetElementType(Expected, KeyType);
		if (bOrderedMultiSet)
		{
			ValueType = "array<bool>";
		}
		if (bOrderedMultiSet || bOrderedMultiMap || Runtime::SplitOrderedMapType(Expected, KeyType, ValueType))
		{
			auto* const Handle = std::get_if<FOrderedMapHandle>(&Value);
			if (!Handle || !*Handle)
			{
				return false;
			}
			if ((!(*Handle)->KeyTypeName.empty() && !NativeElementTypeNamesEqual((*Handle)->KeyTypeName, KeyType)) || (!(*Handle)->ValueTypeName.empty() && !NativeElementTypeNamesEqual((*Handle)->ValueTypeName, ValueType)) || (!(*Handle)->TypeName.empty() && (*Handle)->TypeName != (bOrderedMultiSet ? "ordered_multiset" : "ordered_map") && !NativeElementTypeNamesEqual((*Handle)->TypeName, Expected)))
			{
				return false;
			}
			const bool bIntKey = VmDetail::IsSigned32KeyStorageType(KeyType);
			const bool bUIntKey = KeyType == "unsigned int";
			const bool bInt64Key = KeyType == "int64";
			const bool bUInt64Key = KeyType == "uint64";
			const bool bStringKey = KeyType == "string";
			const bool bWStringKey = KeyType == "wstring";
			// Host scoped enums are intentionally absent from bytecode type tables.
			// Their strongly typed storage is therefore also authoritative at the
			// native boundary; every key is still checked against KeyType below.
			const FNativeTypeDesc* const KeyDesc = DescArgument(0);
			const bool bEnumKey = KeyDesc
			    ? KeyDesc->Type == ENativeType::Enum
			    : (VmDetail::IsScopedEnumType(ActiveModule, KeyType) || !(*Handle)->EnumEntries.empty());
			const bool bHostKey =
			    KeyDesc
			    ? KeyDesc->Type == ENativeType::HostObject
			    : (!bIntKey && !bUIntKey && !bInt64Key && !bUInt64Key && !bStringKey && !bWStringKey && !bEnumKey);
			if ((!bIntKey && !(*Handle)->IntEntries.empty()) || (!bUIntKey && !(*Handle)->UIntEntries.empty()) || (!bInt64Key && !(*Handle)->Int64Entries.empty()) || (!bUInt64Key && !(*Handle)->UInt64Entries.empty()) || (!bStringKey && !(*Handle)->StrEntries.empty()) || (!bWStringKey && !(*Handle)->WStrEntries.empty()) || (!bEnumKey && !(*Handle)->EnumEntries.empty()) || (!bHostKey && !(*Handle)->HostEntries.empty()))
			{
				return false;
			}
			if (KeyType == "char" && std::any_of((*Handle)->IntEntries.begin(), (*Handle)->IntEntries.end(), [&KeyType](const auto& Pair)
			{
				return !VmDetail::IsValidSigned32KeyStorageValue(
				    Pair.first, KeyType);
			}))
			{
				return false;
			}
			if ((!bIntKey && !(*Handle)->IntKeyOrder.empty()) || (!bUIntKey && !(*Handle)->UIntKeyOrder.empty()) || (!bInt64Key && !(*Handle)->Int64KeyOrder.empty()) || (!bUInt64Key && !(*Handle)->UInt64KeyOrder.empty()) || (!bStringKey && !(*Handle)->StrKeyOrder.empty()) || (!bWStringKey && !(*Handle)->WStrKeyOrder.empty()) || (!bEnumKey && !(*Handle)->EnumKeyOrder.empty()) || (!bHostKey && !(*Handle)->HostKeyOrder.empty()))
			{
				return false;
			}
			const auto ValidateOrder =
			    [](const auto& Order, const auto& Entries) -> bool
			{
				if (Order.size() != Entries.size())
				{
					return false;
				}
				std::unordered_set<const void*> SeenEntries;
				SeenEntries.reserve(Order.size());
				for (const auto& Key : Order)
				{
					const auto It = Entries.find(Key);
					if (It == Entries.end() || !SeenEntries.insert(static_cast<const void*>(std::addressof(It->second))).second)
					{
						return false;
					}
				}
				return true;
			};
			if ((bIntKey && !ValidateOrder((*Handle)->IntKeyOrder, (*Handle)->IntEntries)) || (bUIntKey && !ValidateOrder((*Handle)->UIntKeyOrder, (*Handle)->UIntEntries)) || (bInt64Key && !ValidateOrder((*Handle)->Int64KeyOrder, (*Handle)->Int64Entries)) || (bUInt64Key && !ValidateOrder((*Handle)->UInt64KeyOrder, (*Handle)->UInt64Entries)) || (bStringKey && !ValidateOrder((*Handle)->StrKeyOrder, (*Handle)->StrEntries)) || (bWStringKey && !ValidateOrder((*Handle)->WStrKeyOrder, (*Handle)->WStrEntries)) || (bEnumKey && !ValidateOrder((*Handle)->EnumKeyOrder, (*Handle)->EnumEntries)) || (bHostKey && !ValidateOrder((*Handle)->HostKeyOrder, (*Handle)->HostEntries)))
			{
				return false;
			}
			if (bHostKey)
			{
				for (const auto& Pair : (*Handle)->HostEntries)
				{
					if (!HostObjectMatchesType(Pair.first, KeyType))
					{
						return false;
					}
				}
				decltype((*Handle)->HostEntries) ReindexedEntries;
				std::vector<FHostObjectHandle> ReindexedOrder;
				ReindexedEntries.reserve((*Handle)->HostEntries.size());
				ReindexedOrder.reserve((*Handle)->HostKeyOrder.size());
				for (const FHostObjectHandle& Key : (*Handle)->HostKeyOrder)
				{
					const auto Existing = (*Handle)->HostEntries.find(Key);
					if (Existing == (*Handle)->HostEntries.end())
					{
						return false;
					}
					FHostObjectHandle StoredKey;
					if (!PrepareHostKeyForInsert(Key, StoredKey, Location))
					{
						return false;
					}
					auto Inserted = ReindexedEntries.emplace(
					    StoredKey, std::move(Existing->second));
					if (!Inserted.second)
					{
						// Native identity-keyed input may contain keys that collapse
						// under the registered value equality protocol.
						return false;
					}
					ReindexedOrder.push_back(Inserted.first->first);
				}
				(*Handle)->HostEntries = std::move(ReindexedEntries);
				(*Handle)->HostKeyOrder = std::move(ReindexedOrder);
			}
			if (bEnumKey)
			{
				for (const auto& Pair : (*Handle)->EnumEntries)
				{
					if (Pair.first.TypeName != Runtime::EnumValueTypeName(*ActiveModule, KeyType))
					{
						return false;
					}
				}
			}
			// A multimap stores array<V> per key while its public descriptor names V.
			// Let the normal recursive type-name validation construct that array layer.
			const FNativeTypeDesc* const ValueDesc =
			    bOrderedMultiMap ? nullptr : DescArgument(1);
			const auto ValidateEntries =
			    [this, &ValueType, &Location, ValueDesc](auto& Entries) -> bool
			{
				for (auto& Pair : Entries)
				{
					if (!ValidateAndStampNativeContainerValue(
					        Pair.second, ValueType, Location, ValueDesc))
					{
						return false;
					}
				}
				return true;
			};
			if ((bIntKey && !ValidateEntries((*Handle)->IntEntries)) || (bUIntKey && !ValidateEntries((*Handle)->UIntEntries)) || (bInt64Key && !ValidateEntries((*Handle)->Int64Entries)) || (bUInt64Key && !ValidateEntries((*Handle)->UInt64Entries)) || (bStringKey && !ValidateEntries((*Handle)->StrEntries)) || (bWStringKey && !ValidateEntries((*Handle)->WStrEntries)) || (bEnumKey && !ValidateEntries((*Handle)->EnumEntries)) || (bHostKey && !ValidateEntries((*Handle)->HostEntries)))
			{
				return false;
			}
			(*Handle)->KeyTypeName = KeyType;
			(*Handle)->ValueTypeName = ValueType;
			(*Handle)->TypeName = Expected;
			return true;
		}
		KeyType.clear();
		ValueType.clear();
		const bool bMultiMap = Runtime::IsMultiMapTypeName(Expected) && Runtime::SplitMapType(Expected, KeyType, ValueType);
		if (bMultiMap)
		{
			ValueType = "array<" + ValueType + ">";
		}
		const bool bMultiSet = Runtime::IsMultiSetTypeName(Expected) && Runtime::ParseMultiSetElementType(Expected, KeyType);
		if (bMultiSet)
		{
			ValueType = "array<bool>";
		}
		if (bMultiSet || bMultiMap || Runtime::SplitMapType(Expected, KeyType, ValueType))
		{
			auto* const Handle = std::get_if<FMapHandle>(&Value);
			if (!Handle || !*Handle)
			{
				return false;
			}
			if ((!(*Handle)->KeyTypeName.empty() && !NativeElementTypeNamesEqual((*Handle)->KeyTypeName, KeyType)) || (!(*Handle)->ValueTypeName.empty() && !NativeElementTypeNamesEqual((*Handle)->ValueTypeName, ValueType)) || (!(*Handle)->TypeName.empty() && (*Handle)->TypeName != (bMultiSet ? "multiset" : "map") && !NativeElementTypeNamesEqual((*Handle)->TypeName, Expected)))
			{
				return false;
			}
			const bool bIntKey = VmDetail::IsSigned32KeyStorageType(KeyType);
			const bool bUIntKey = KeyType == "unsigned int";
			const bool bInt64Key = KeyType == "int64";
			const bool bUInt64Key = KeyType == "uint64";
			const bool bStringKey = KeyType == "string";
			const bool bWStringKey = KeyType == "wstring";
			// See ordered-map path above: non-empty EnumEntries identifies a scoped
			// host enum whose declaration is owned by the binding registry.
			const FNativeTypeDesc* const KeyDesc = DescArgument(0);
			const bool bEnumKey = KeyDesc
			    ? KeyDesc->Type == ENativeType::Enum
			    : (VmDetail::IsScopedEnumType(ActiveModule, KeyType) || !(*Handle)->EnumEntries.empty());
			const bool bHostKey =
			    KeyDesc
			    ? KeyDesc->Type == ENativeType::HostObject
			    : (!bIntKey && !bUIntKey && !bInt64Key && !bUInt64Key && !bStringKey && !bWStringKey && !bEnumKey);
			if ((!bIntKey && !(*Handle)->IntEntries.empty()) || (!bUIntKey && !(*Handle)->UIntEntries.empty()) || (!bInt64Key && !(*Handle)->Int64Entries.empty()) || (!bUInt64Key && !(*Handle)->UInt64Entries.empty()) || (!bStringKey && !(*Handle)->StrEntries.empty()) || (!bWStringKey && !(*Handle)->WStrEntries.empty()) || (!bEnumKey && !(*Handle)->EnumEntries.empty()) || (!bHostKey && !(*Handle)->HostEntries.empty()))
			{
				return false;
			}
			if ((*Handle)->ScalarEntries.index() != 0)
			{
				return false;
			}
			if (KeyType == "char" && std::any_of((*Handle)->IntEntries.begin(), (*Handle)->IntEntries.end(), [&KeyType](const auto& Pair)
			{
				return !VmDetail::IsValidSigned32KeyStorageValue(
				    Pair.first, KeyType);
			}))
			{
				return false;
			}
			if (bHostKey)
			{
				for (const auto& Pair : (*Handle)->HostEntries)
				{
					if (!HostObjectMatchesType(Pair.first, KeyType))
					{
						return false;
					}
				}
				decltype((*Handle)->HostEntries) ReindexedEntries;
				ReindexedEntries.reserve((*Handle)->HostEntries.size());
				for (auto& Pair : (*Handle)->HostEntries)
				{
					FHostObjectHandle StoredKey;
					if (!PrepareHostKeyForInsert(Pair.first, StoredKey, Location) || !ReindexedEntries.emplace(std::move(StoredKey), std::move(Pair.second)).second)
					{
						return false;
					}
				}
				(*Handle)->HostEntries = std::move(ReindexedEntries);
			}
			if (bEnumKey)
			{
				for (const auto& Pair : (*Handle)->EnumEntries)
				{
					if (Pair.first.TypeName != Runtime::EnumValueTypeName(*ActiveModule, KeyType))
					{
						return false;
					}
				}
			}
			// See the ordered-multimap path above.
			const FNativeTypeDesc* const ValueDesc = bMultiMap ? nullptr : DescArgument(1);
			const auto ValidateEntries =
			    [this, &ValueType, &Location, ValueDesc](auto& Entries) -> bool
			{
				for (auto& Pair : Entries)
				{
					if (!ValidateAndStampNativeContainerValue(
					        Pair.second, ValueType, Location, ValueDesc))
					{
						return false;
					}
				}
				return true;
			};
			if ((bIntKey && !ValidateEntries((*Handle)->IntEntries)) || (bUIntKey && !ValidateEntries((*Handle)->UIntEntries)) || (bInt64Key && !ValidateEntries((*Handle)->Int64Entries)) || (bUInt64Key && !ValidateEntries((*Handle)->UInt64Entries)) || (bStringKey && !ValidateEntries((*Handle)->StrEntries)) || (bWStringKey && !ValidateEntries((*Handle)->WStrEntries)) || (bEnumKey && !ValidateEntries((*Handle)->EnumEntries)) || (bHostKey && !ValidateEntries((*Handle)->HostEntries)))
			{
				return false;
			}
			(*Handle)->KeyTypeName = KeyType;
			(*Handle)->ValueTypeName = ValueType;
			(*Handle)->TypeName = Expected;
			return true;
		}

		if (Runtime::SplitSetElementType(Expected, ElementType))
		{
			auto* const Handle = std::get_if<FSetHandle>(&Value);
			if (!Handle || !*Handle)
			{
				return false;
			}
			if ((!(*Handle)->ElementTypeName.empty() && !NativeElementTypeNamesEqual((*Handle)->ElementTypeName, ElementType)) || (!(*Handle)->TypeName.empty() && (*Handle)->TypeName != "set" && !NativeElementTypeNamesEqual((*Handle)->TypeName, Expected)))
			{
				return false;
			}
			const bool bIntElement = VmDetail::IsSigned32KeyStorageType(ElementType);
			const bool bUIntElement = ElementType == "unsigned int";
			const bool bInt64Element = ElementType == "int64";
			const bool bUInt64Element = ElementType == "uint64";
			const bool bStringElement = ElementType == "string";
			const bool bWStringElement = ElementType == "wstring";
			const FNativeTypeDesc* const ElementDesc = DescArgument(0);
			const bool bEnumElement = ElementDesc
			    ? ElementDesc->Type == ENativeType::Enum
			    : (VmDetail::IsScopedEnumType(ActiveModule, ElementType) || !(*Handle)->EnumElements.empty());
			const bool bHostElement =
			    ElementDesc
			    ? ElementDesc->Type == ENativeType::HostObject
			    : (!bIntElement && !bUIntElement && !bInt64Element && !bUInt64Element && !bStringElement && !bWStringElement && !bEnumElement);
			if ((!bIntElement && !(*Handle)->IntElements.empty()) || (!bUIntElement && !(*Handle)->UIntElements.empty()) || (!bInt64Element && !(*Handle)->Int64Elements.empty()) || (!bUInt64Element && !(*Handle)->UInt64Elements.empty()) || (!bStringElement && !(*Handle)->StrElements.empty()) || (!bWStringElement && !(*Handle)->WStrElements.empty()) || (!bEnumElement && !(*Handle)->EnumElements.empty()) || (!bHostElement && !(*Handle)->HostElements.empty()))
			{
				return false;
			}
			if ((*Handle)->ScalarElements.index() != 0)
			{
				return false;
			}
			if (ElementType == "char" && std::any_of((*Handle)->IntElements.begin(), (*Handle)->IntElements.end(), [&ElementType](const std::int32_t Element)
			{
				return !VmDetail::IsValidSigned32KeyStorageValue(
				    Element, ElementType);
			}))
			{
				return false;
			}
			if (bHostElement)
			{
				for (const FHostObjectHandle& HostElement : (*Handle)->HostElements)
				{
					if (!HostObjectMatchesType(HostElement, ElementType))
					{
						return false;
					}
				}
				decltype((*Handle)->HostElements) ReindexedElements;
				ReindexedElements.reserve((*Handle)->HostElements.size());
				for (const FHostObjectHandle& Element : (*Handle)->HostElements)
				{
					FHostObjectHandle StoredElement;
					if (!PrepareHostKeyForInsert(
					        Element, StoredElement, Location) ||
					    !ReindexedElements.insert(std::move(StoredElement)).second)
					{
						return false;
					}
				}
				(*Handle)->HostElements = std::move(ReindexedElements);
			}
			if (bEnumElement)
			{
				for (const FEnumValue& EnumElement : (*Handle)->EnumElements)
				{
					if (EnumElement.TypeName != Runtime::EnumValueTypeName(*ActiveModule, ElementType))
					{
						return false;
					}
				}
			}
			(*Handle)->ElementTypeName = ElementType;
			(*Handle)->TypeName = Expected;
			return true;
		}

		return ValueMatchesElementType(Value, Expected, Location);
	}

	const void* VeroScriptVm::ContainerStorageIdentity(const FValue& Value) const
	{
		return std::visit([](const auto& Handle) -> const void*
		{
			using T = std::decay_t<decltype(Handle)>;
			if constexpr (std::is_same_v<T, FDynamicArrayHandle> || std::is_same_v<T, FMapHandle> || std::is_same_v<T, FOrderedMapHandle> || std::is_same_v<T, FSetHandle> || std::is_same_v<T, FQueueHandle> || std::is_same_v<T, FStackHandle> || std::is_same_v<T, FDequeHandle>)
			{
				return Handle.get();
			}
			else
			{
				return nullptr;
			}
		}, Value);
	}

	bool VeroScriptVm::CheckContainerCopyMutation(const FValue& Container, const FSourceLocation& Location)
	{
		if (ActiveContainerCopyReads.empty()) return true;
		const auto Identity = ContainerStorageIdentity(Container);
		if (Identity && std::find(ActiveContainerCopyReads.begin(), ActiveContainerCopyReads.end(), Identity) != ActiveContainerCopyReads.end())
		{
			ReportError("cannot mutate a container while its values are being copied", Location);
			return false;
		}
		return true;
	}
}
