// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptEnumValueIdentity.h"
#include "VeroScriptVm.h"

#include "VeroScriptVmJitBridgeInternal.h"
#include "VeroScriptVmInternal.h"

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
	namespace
	{
		enum class EScalarMapKeyKind : std::uint8_t
		{
			Unsupported,
			I32,
			U32,
			I64,
			U64,
			String,
			WString,
		};

		EScalarMapKeyKind ScalarMapKeyKind(const std::string_view TypeName)
		{
			if (TypeName == "int" || TypeName == "int32")
			{
				return EScalarMapKeyKind::I32;
			}
			if (TypeName == "uint" || TypeName == "uint32" || TypeName == "unsigned int" || TypeName == "unsigned")
			{
				return EScalarMapKeyKind::U32;
			}
			if (TypeName == "int64" || TypeName == "long")
			{
				return EScalarMapKeyKind::I64;
			}
			if (TypeName == "uint64" || TypeName == "unsigned long")
			{
				return EScalarMapKeyKind::U64;
			}
			if (TypeName == "string")
			{
				return EScalarMapKeyKind::String;
			}
			if (TypeName == "wstring")
			{
				return EScalarMapKeyKind::WString;
			}
			return EScalarMapKeyKind::Unsupported;
		}

		template <typename TValue>
		bool EmplaceScalarMapStorage(FMap& Map, const EScalarMapKeyKind KeyKind)
		{
			switch (KeyKind)
			{
			case EScalarMapKeyKind::I32:
				{
					if constexpr (std::is_same_v<TValue, std::int32_t>)
					{
						Map.ScalarEntries.emplace<FMap::FIntI32Entries>();
					}
					else if constexpr (std::is_same_v<TValue, std::uint32_t>)
					{
						Map.ScalarEntries.emplace<FMap::FIntU32Entries>();
					}
					else if constexpr (std::is_same_v<TValue, float>)
					{
						Map.ScalarEntries.emplace<FMap::FIntF32Entries>();
					}
					else if constexpr (std::is_same_v<TValue, double>)
					{
						Map.ScalarEntries.emplace<FMap::FIntF64Entries>();
					}
					else if constexpr (std::is_same_v<TValue, bool>)
					{
						Map.ScalarEntries.emplace<FMap::FIntBoolEntries>();
					}
					else
					{
						Map.ScalarEntries.emplace<FMap::FIntExtendedEntries>();
					}
					return true;
				}
			case EScalarMapKeyKind::U32:
				{
					if constexpr (std::is_same_v<TValue, std::int32_t>)
					{
						Map.ScalarEntries.emplace<FMap::FUIntI32Entries>();
					}
					else if constexpr (std::is_same_v<TValue, std::uint32_t>)
					{
						Map.ScalarEntries.emplace<FMap::FUIntU32Entries>();
					}
					else if constexpr (std::is_same_v<TValue, float>)
					{
						Map.ScalarEntries.emplace<FMap::FUIntF32Entries>();
					}
					else if constexpr (std::is_same_v<TValue, double>)
					{
						Map.ScalarEntries.emplace<FMap::FUIntF64Entries>();
					}
					else if constexpr (std::is_same_v<TValue, bool>)
					{
						Map.ScalarEntries.emplace<FMap::FUIntBoolEntries>();
					}
					else
					{
						Map.ScalarEntries.emplace<FMap::FUIntExtendedEntries>();
					}
					return true;
				}
			case EScalarMapKeyKind::I64:
				{
					Map.ScalarEntries.emplace<FMap::FInt64ExtendedEntries>();
					return true;
				}
			case EScalarMapKeyKind::U64:
				{
					Map.ScalarEntries.emplace<FMap::FUInt64ExtendedEntries>();
					return true;
				}
			case EScalarMapKeyKind::String:
				{
					if constexpr (std::is_same_v<TValue, std::int32_t>)
					{
						Map.ScalarEntries.emplace<FMap::FStringI32Entries>();
					}
					else
					{
						Map.ScalarEntries.emplace<FMap::FStringExtendedEntries>();
					}
					return true;
				}
			case EScalarMapKeyKind::WString:
				{
					if constexpr (std::is_same_v<TValue, std::int32_t>)
					{
						Map.ScalarEntries.emplace<FMap::FWStringI32Entries>();
					}
					else
					{
						Map.ScalarEntries.emplace<FMap::FWStringExtendedEntries>();
					}
					return true;
				}
			default:
				return false;
			}
		}

	}

	bool VeroScriptVm::JitTryCompareStringArrayElements(
	    const std::uint16_t LeftArraySlot,
	    const std::uint16_t LeftIndexSlot,
	    const std::uint16_t RightArraySlot,
	    const std::uint16_t RightIndexSlot,
	    const bool bEqual,
	    bool& OutResult,
	    bool& OutHandled)
	{
		OutHandled = false;
		OutResult = false;
		// Linear execution can use a direct local window that is intentionally
		// distinct from the interpreter frame storage. Always resolve through the
		// active Linear ABI so strict-JIT and Mixed observe the same rooted slots.
		const auto BorrowLocalValue = [&](const std::uint16_t Slot) -> FValue*
		{
			return JitBorrowDirectLinearLocal(Slot);
		};
		FValue* const LeftArrayValue = BorrowLocalValue(LeftArraySlot);
		FValue* const RightArrayValue = BorrowLocalValue(RightArraySlot);
		FValue* const LeftIndexValue = BorrowLocalValue(LeftIndexSlot);
		FValue* const RightIndexValue = BorrowLocalValue(RightIndexSlot);
		const auto* const LeftArray = LeftArrayValue
		    ? std::get_if<FDynamicArrayHandle>(LeftArrayValue)
		    : nullptr;
		const auto* const RightArray = RightArrayValue
		    ? std::get_if<FDynamicArrayHandle>(RightArrayValue)
		    : nullptr;
		const auto* const LeftIndex = LeftIndexValue
		    ? std::get_if<std::int32_t>(LeftIndexValue)
		    : nullptr;
		const auto* const RightIndex = RightIndexValue
		    ? std::get_if<std::int32_t>(RightIndexValue)
		    : nullptr;
		if (!LeftArray || !*LeftArray || !RightArray || !*RightArray || !LeftIndex || !RightIndex)
		{
			return true;
		}
		const bool bStrings = (*LeftArray)->ElementTypeName == "string" && (*RightArray)->ElementTypeName == "string";
		const bool bWStrings = (*LeftArray)->ElementTypeName == "wstring" && (*RightArray)->ElementTypeName == "wstring";
		if (!bStrings && !bWStrings)
		{
			return true;
		}
		OutHandled = true;
		const auto IsInBounds = [](const FDynamicArray& Array, const std::int32_t Index)
		{
			const std::size_t Size = Array.HasScalarElements()
			    ? Array.ScalarSize()
			    : Array.Elements.size();
			return Index >= 0 && static_cast<std::size_t>(Index) < Size;
		};
		if (!IsInBounds(**LeftArray, *LeftIndex) || !IsInBounds(**RightArray, *RightIndex))
		{
			ReportError("array index out of bounds");
			return false;
		}
		bool bSame = false;
		if (bStrings)
		{
			const std::string* const Left = (*LeftArray)->BorrowString(static_cast<std::size_t>(*LeftIndex));
			const std::string* const Right = (*RightArray)->BorrowString(static_cast<std::size_t>(*RightIndex));
			if (!Left || !Right)
			{
				ReportError("array value type mismatch");
				return false;
			}
			bSame = *Left == *Right;
		}
		else
		{
			const std::u16string* const Left = (*LeftArray)->BorrowWString(static_cast<std::size_t>(*LeftIndex));
			const std::u16string* const Right = (*RightArray)->BorrowWString(static_cast<std::size_t>(*RightIndex));
			if (!Left || !Right)
			{
				ReportError("array value type mismatch");
				return false;
			}
			bSame = *Left == *Right;
		}
		OutResult = bEqual ? bSame : !bSame;
		return true;
	}
	bool VeroScriptVm::JitNewDynamicArray(const std::uint16_t TypeNameIndex, FValue& Out)
	{
		if (!ActiveModule || TypeNameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT NewDynamicArray: invalid type name");
			return false;
		}
		const FDynamicArrayHandle Array =
		    CreateDynamicArray(ActiveModule->StringPool[TypeNameIndex], {});
		if (!Array)
		{
			return false;
		}
		Out = Array;
		return true;
	}

	bool VeroScriptVm::JitNewMap(const std::uint16_t TypeNameIndex, FValue& Out)
	{
		if (!ActiveModule || TypeNameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT NewMap: invalid type name");
			return false;
		}
		const FMapHandle Map = CreateMap(ActiveModule->StringPool[TypeNameIndex], {});
		if (!Map)
		{
			return false;
		}
		Out = Map;
		return true;
	}

	bool VeroScriptVm::JitNewContainer(
	    const EOpcode Op,
	    const std::uint16_t TypeNameIndex,
	    FValue& Out)
	{
		if (!ActiveModule || TypeNameIndex >= ActiveModule->StringPool.size())
		{
			ReportError("JIT NewContainer: invalid type name");
			return false;
		}
		const std::string& TypeName = ActiveModule->StringPool[TypeNameIndex];
		const FSourceLocation Loc{};
		switch (Op)
		{
		case EOpcode::NewDynamicArray:
			return JitNewDynamicArray(TypeNameIndex, Out);
		case EOpcode::NewMap:
			return JitNewMap(TypeNameIndex, Out);
		case EOpcode::NewSet:
			{
				const FSetHandle Handle = CreateSet(TypeName, Loc);
				if (!Handle)
				{
					return false;
				}
				Out = Handle;
				return true;
			}
		case EOpcode::NewQueue:
			{
				const FQueueHandle Handle = CreateQueue(TypeName, Loc);
				if (!Handle)
				{
					return false;
				}
				Out = Handle;
				return true;
			}
		case EOpcode::NewStack:
			{
				const FStackHandle Handle = CreateStack(TypeName, Loc);
				if (!Handle)
				{
					return false;
				}
				Out = Handle;
				return true;
			}
		case EOpcode::NewOrderedMap:
			{
				const FOrderedMapHandle Handle = CreateOrderedMap(TypeName, Loc);
				if (!Handle)
				{
					return false;
				}
				Out = Handle;
				return true;
			}
		case EOpcode::NewDeque:
			{
				const FDequeHandle Handle = CreateDeque(TypeName, Loc);
				if (!Handle)
				{
					return false;
				}
				Out = Handle;
				return true;
			}
		case EOpcode::NewChannel:
			{
				const FChannelHandle Handle = CreateChannel(TypeName, Loc);
				if (!Handle)
				{
					return false;
				}
				Out = Handle;
				return true;
			}
		case EOpcode::NewNativeArray:
		case EOpcode::NewUninitializedNativeArray:
			{
				FNativeArray Array = CreateNativeArray(TypeName, Loc, Op == EOpcode::NewUninitializedNativeArray);
				if (bHasError)
				{
					return false;
				}
				Out = std::move(Array);
				return true;
			}
		default:
			{
				ReportError("JIT NewContainer: invalid opcode");
				return false;
			}
		}
	}

	bool VeroScriptVm::JitGetIndex(const FValue& Container, const FValue& Index, FValue& OutValue)
	{
		std::string_view ElementType;
		if (const auto* Fixed = std::get_if<FNativeArray>(&Container))
		{
			ElementType = Fixed->ElementTypeName();
		}
		else if (const auto* Dynamic = std::get_if<FDynamicArrayHandle>(&Container); Dynamic && *Dynamic)
		{
			ElementType = (*Dynamic)->ElementTypeName;
		}
		else if (const auto* Map = std::get_if<FMapHandle>(&Container); Map && *Map)
		{
			ElementType = (*Map)->ValueTypeName;
		}
		else if (const auto* Ordered = std::get_if<FOrderedMapHandle>(&Container); Ordered && *Ordered)
		{
			ElementType = (*Ordered)->ValueTypeName;
		}
		else if (const auto* Deque = std::get_if<FDequeHandle>(&Container); Deque && *Deque)
		{
			ElementType = (*Deque)->ElementTypeName;
		}
		const auto CopyVisible = [&](const FValue& Stored) -> bool
		{
			const FValue* const Visible = UnwrapClosureReferenceCell(&Stored);
			if (!Visible)
			{
				ReportError("invalid captured reference cell");
				return false;
			}
			if (std::holds_alternative<FObjectHandle>(*Visible) || std::holds_alternative<FHostObjectHandle>(*Visible) || std::holds_alternative<FNativeArray>(*Visible))
			{
				return CopyLanguageValue(*Visible, OutValue, {}, false, ElementType);
			}
			OutValue = *Visible;
			return true;
		};
		// Fast path: dynamic array + i32 index.
		if (const auto* Arr = std::get_if<FDynamicArrayHandle>(&Container))
		{
			if (*Arr && std::holds_alternative<std::int32_t>(Index))
			{
				const std::int32_t Idx = std::get<std::int32_t>(Index);
				const std::size_t Size = (*Arr)->HasScalarElements()
				    ? (*Arr)->ScalarSize()
				    : (*Arr)->Elements.size();
				if (Idx >= 0 && static_cast<std::size_t>(Idx) < Size)
				{
					if ((*Arr)->HasScalarElements())
					{
						if (!(*Arr)->TryGetScalar(static_cast<std::size_t>(Idx), OutValue))
						{
							ReportError("invalid scalar array storage");
							return false;
						}
						return true;
					}
					return CopyVisible((*Arr)->Elements[static_cast<std::size_t>(Idx)]);
				}
				ReportError("array index out of bounds");
				return false;
			}
		}
		if (const auto* MapHandle = std::get_if<FMapHandle>(&Container))
		{
			if (!*MapHandle)
			{
				ReportError("index access requires map");
				return false;
			}
			if (VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
			{
				std::int32_t Key = 0;
				if (!VmDetail::TryGetSigned32Key(Index, (*MapHandle)->KeyTypeName, Key))
				{
					ReportError("map key must be " + (*MapHandle)->KeyTypeName);
					return false;
				}
				const auto It = (*MapHandle)->IntEntries.find(Key);
				if (It == (*MapHandle)->IntEntries.end())
				{
					ReportError("map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*MapHandle)->KeyTypeName == "unsigned int")
			{
				const auto* Key = std::get_if<std::uint32_t>(&Index);
				if (!Key)
				{
					ReportError("map key must be unsigned int");
					return false;
				}
				const auto It = (*MapHandle)->UIntEntries.find(*Key);
				if (It == (*MapHandle)->UIntEntries.end())
				{
					ReportError("map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*MapHandle)->KeyTypeName == "int64")
			{
				const auto* Key = std::get_if<std::int64_t>(&Index);
				if (!Key)
				{
					ReportError("map key must be int64");
					return false;
				}
				const auto It = (*MapHandle)->Int64Entries.find(*Key);
				if (It == (*MapHandle)->Int64Entries.end())
				{
					ReportError("map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*MapHandle)->KeyTypeName == "uint64")
			{
				const auto* Key = std::get_if<std::uint64_t>(&Index);
				if (!Key)
				{
					ReportError("map key must be uint64");
					return false;
				}
				const auto It = (*MapHandle)->UInt64Entries.find(*Key);
				if (It == (*MapHandle)->UInt64Entries.end())
				{
					ReportError("map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*MapHandle)->KeyTypeName == "string")
			{
				const auto* Key = std::get_if<std::string>(&Index);
				if (!Key)
				{
					ReportError("map key must be string");
					return false;
				}
				const auto It = (*MapHandle)->StrEntries.find(*Key);
				if (It == (*MapHandle)->StrEntries.end())
				{
					ReportError("map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*MapHandle)->KeyTypeName == "wstring")
			{
				const auto* Key = std::get_if<std::u16string>(&Index);
				if (!Key)
				{
					ReportError("map key must be wstring");
					return false;
				}
				const auto It = (*MapHandle)->WStrEntries.find(*Key);
				if (It == (*MapHandle)->WStrEntries.end())
				{
					ReportError("map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if (const auto* Key = std::get_if<FEnumValue>(&Index);
			    Key && Key->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*MapHandle)->KeyTypeName))
			{
				const auto It = (*MapHandle)->EnumEntries.find(*Key);
				if (It == (*MapHandle)->EnumEntries.end())
				{
					ReportError("map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if (const auto* Key = std::get_if<FHostObjectHandle>(&Index);
			    Key && *Key && ValueMatchesElementType(Index, (*MapHandle)->KeyTypeName, {}))
			{
				const auto It = (*MapHandle)->HostEntries.find(*Key);
				if (It == (*MapHandle)->HostEntries.end())
				{
					ReportError("map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			ReportError("invalid map key type");
			return false;
		}
		if (const auto* OrderedMapHandle = std::get_if<FOrderedMapHandle>(&Container))
		{
			if (!*OrderedMapHandle)
			{
				ReportError("index access requires ordered_map");
				return false;
			}
			if (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
			{
				std::int32_t Key = 0;
				if (!VmDetail::TryGetSigned32Key(
				        Index, (*OrderedMapHandle)->KeyTypeName, Key))
				{
					ReportError("ordered_map key must be " + (*OrderedMapHandle)->KeyTypeName);
					return false;
				}
				const auto It = (*OrderedMapHandle)->IntEntries.find(Key);
				if (It == (*OrderedMapHandle)->IntEntries.end())
				{
					ReportError("ordered_map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
			{
				const auto* Key = std::get_if<std::uint32_t>(&Index);
				if (!Key)
				{
					ReportError("ordered_map key must be unsigned int");
					return false;
				}
				const auto It = (*OrderedMapHandle)->UIntEntries.find(*Key);
				if (It == (*OrderedMapHandle)->UIntEntries.end())
				{
					ReportError("ordered_map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*OrderedMapHandle)->KeyTypeName == "int64")
			{
				const auto* Key = std::get_if<std::int64_t>(&Index);
				if (!Key)
				{
					ReportError("ordered_map key must be int64");
					return false;
				}
				const auto It = (*OrderedMapHandle)->Int64Entries.find(*Key);
				if (It == (*OrderedMapHandle)->Int64Entries.end())
				{
					ReportError("ordered_map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*OrderedMapHandle)->KeyTypeName == "uint64")
			{
				const auto* Key = std::get_if<std::uint64_t>(&Index);
				if (!Key)
				{
					ReportError("ordered_map key must be uint64");
					return false;
				}
				const auto It = (*OrderedMapHandle)->UInt64Entries.find(*Key);
				if (It == (*OrderedMapHandle)->UInt64Entries.end())
				{
					ReportError("ordered_map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*OrderedMapHandle)->KeyTypeName == "string")
			{
				const auto* Key = std::get_if<std::string>(&Index);
				if (!Key)
				{
					ReportError("ordered_map key must be string");
					return false;
				}
				const auto It = (*OrderedMapHandle)->StrEntries.find(*Key);
				if (It == (*OrderedMapHandle)->StrEntries.end())
				{
					ReportError("ordered_map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if ((*OrderedMapHandle)->KeyTypeName == "wstring")
			{
				const auto* Key = std::get_if<std::u16string>(&Index);
				if (!Key)
				{
					ReportError("ordered_map key must be wstring");
					return false;
				}
				const auto It = (*OrderedMapHandle)->WStrEntries.find(*Key);
				if (It == (*OrderedMapHandle)->WStrEntries.end())
				{
					ReportError("ordered_map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if (const auto* Key = std::get_if<FEnumValue>(&Index);
			    Key && Key->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*OrderedMapHandle)->KeyTypeName))
			{
				const auto It = (*OrderedMapHandle)->EnumEntries.find(*Key);
				if (It == (*OrderedMapHandle)->EnumEntries.end())
				{
					ReportError("ordered_map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			if (const auto* Key = std::get_if<FHostObjectHandle>(&Index);
			    Key && *Key && ValueMatchesElementType(Index, (*OrderedMapHandle)->KeyTypeName, {}))
			{
				const auto It = (*OrderedMapHandle)->HostEntries.find(*Key);
				if (It == (*OrderedMapHandle)->HostEntries.end())
				{
					ReportError("ordered_map key not found");
					return false;
				}
				return CopyVisible(It->second);
			}
			ReportError("invalid ordered_map key type");
			return false;
		}
		if (!Runtime::IsIntegerValue(Index))
		{
			ReportError("array index must be an integer");
			return false;
		}
		std::size_t ResolvedIndex = 0;
		if (!Runtime::TryGetSizeValue(Index, ResolvedIndex))
		{
			ReportError(std::holds_alternative<FDequeHandle>(Container)
			        ? "deque index out of bounds"
			        : "array index out of bounds");
			return false;
		}
		const std::size_t Idx = ResolvedIndex;
		if (const auto* NativeArray = std::get_if<FNativeArray>(&Container))
		{
			if (static_cast<std::uint64_t>(Idx) >= NativeArray->Length() || static_cast<std::size_t>(Idx) >= NativeArray->Storage().Elements.size())
			{
				ReportError("array index out of bounds");
				return false;
			}
			return CopyVisible(NativeArray->Storage().Elements[static_cast<std::size_t>(Idx)]);
		}
		if (const auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&Container))
		{
			if (!*DynamicArray)
			{
				ReportError("index access requires array");
				return false;
			}
			const std::size_t Size = (*DynamicArray)->HasScalarElements()
			    ? (*DynamicArray)->ScalarSize()
			    : (*DynamicArray)->Elements.size();
			if (static_cast<std::size_t>(Idx) >= Size)
			{
				ReportError("array index out of bounds");
				return false;
			}
			if ((*DynamicArray)->HasScalarElements())
			{
				FValue Scalar;
				if (!(*DynamicArray)->TryGetScalar(static_cast<std::size_t>(Idx), Scalar))
				{
					ReportError("invalid scalar array storage");
					return false;
				}
				return CopyVisible(Scalar);
			}
			return CopyVisible((*DynamicArray)->Elements[static_cast<std::size_t>(Idx)]);
		}
		if (const auto* DequeHandle = std::get_if<FDequeHandle>(&Container))
		{
			if (!*DequeHandle)
			{
				ReportError("index access requires deque");
				return false;
			}
			if (static_cast<std::size_t>(Idx) >= (*DequeHandle)->Elements.size())
			{
				ReportError("deque index out of bounds");
				return false;
			}
			return CopyVisible((*DequeHandle)->Elements[static_cast<std::size_t>(Idx)]);
		}
		ReportError("index access requires array");
		return false;
	}

	bool VeroScriptVm::JitTryGetDynamicArrayScalar(
	    const FValue& Container,
	    const std::int64_t Index,
	    FValue& OutValue,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		const auto* const Array = std::get_if<FDynamicArrayHandle>(&Container);
		if (!Array || !*Array)
		{
			return true;
		}
		if ((*Array)->HasScalarElements())
		{
			bOutHandled = true;
			if (Index < 0 || static_cast<std::uint64_t>(Index) >= (*Array)->ScalarSize())
			{
				ReportError("array index out of bounds");
				return false;
			}
			if (!(*Array)->TryGetScalar(static_cast<std::size_t>(Index), OutValue))
			{
				ReportError("invalid scalar array storage");
				return false;
			}
			return true;
		}
		const std::string& ElementType = (*Array)->ElementTypeName;
		if (!Runtime::IsPrimitiveValueTypeName(ElementType))
		{
			return true;
		}
		if (Index < 0 || static_cast<std::uint64_t>(Index) >= (*Array)->Elements.size())
		{
			ReportError("array index out of bounds");
			return false;
		}
		const FValue* const Stored = UnwrapClosureReferenceCell(
		    &(*Array)->Elements[static_cast<std::size_t>(Index)]);
		if (!Stored)
		{
			ReportError("invalid captured reference cell");
			return false;
		}
		const FValue& Element = *Stored;
		if (!Runtime::ValueMatchesPrimitiveTypeName(Element, ElementType))
		{
			// Preserve the generic fallback for malformed/hand-authored bytecode.
			return true;
		}
		OutValue = Element;
		bOutHandled = true;
		return true;
	}

	bool VeroScriptVm::JitTrySetDynamicArrayScalar(
	    FValue& Container,
	    const std::int64_t Index,
	    const FValue& Value,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		auto* const Array = std::get_if<FDynamicArrayHandle>(&Container);
		if (!Array || !*Array)
		{
			return true;
		}
		if ((*Array)->HasScalarElements())
		{
			if (Index < 0 || static_cast<std::uint64_t>(Index) >= (*Array)->ScalarSize())
			{
				bOutHandled = true;
				ReportError("array index out of bounds");
				return false;
			}
			if (!(*Array)->TrySetScalar(static_cast<std::size_t>(Index), Value))
			{
				// Preserve the generic validation/error path for malformed bytecode or
				// manually supplied values of the wrong scalar alternative.
				return true;
			}
			bOutHandled = true;
			NotifyContainerMutation(Container, EMutationKind::ContainerSet);
			return !bHasError;
		}
		const std::string& ElementType = (*Array)->ElementTypeName;
		if (!Runtime::ValueMatchesPrimitiveTypeName(Value, ElementType))
		{
			return true;
		}
		if (Index < 0 || static_cast<std::uint64_t>(Index) >= (*Array)->Elements.size())
		{
			ReportError("array index out of bounds");
			return false;
		}
		FValue* const Stored = UnwrapClosureReferenceCell(
		    &(*Array)->Elements[static_cast<std::size_t>(Index)]);
		if (!Stored)
		{
			ReportError("invalid captured reference cell");
			return false;
		}
		*Stored = Value;
		NotifyContainerMutation(Container, EMutationKind::ContainerSet);
		bOutHandled = true;
		return !bHasError;
	}

	bool VeroScriptVm::JitEnableScalarMapStorage(FValue& Container)
	{
		auto* const Handle = std::get_if<FMapHandle>(&Container);
		if (!Handle || !*Handle)
		{
			return false;
		}
		FMap& Map = **Handle;
		if (Map.ScalarEntries.index() != 0 || !Map.IntEntries.empty() || !Map.UIntEntries.empty() || !Map.Int64Entries.empty() || !Map.UInt64Entries.empty() || !Map.StrEntries.empty() || !Map.WStrEntries.empty() || !Map.EnumEntries.empty() || !Map.HostEntries.empty())
		{
			return false;
		}
		const EScalarMapKeyKind KeyKind = ScalarMapKeyKind(Map.KeyTypeName);
		if (KeyKind == EScalarMapKeyKind::Unsupported)
		{
			return false;
		}
		const auto ReserveTypedStorage = [&Map]()
		{
			if (Map.ReservedCapacityHint == 0)
			{
				return;
			}
			std::visit(
			    [&Map](auto& Entries)
			{
				using FEntries = std::decay_t<decltype(Entries)>;
				if constexpr (!std::is_same_v<FEntries, std::monostate>)
				{
					Entries.reserve(Map.ReservedCapacityHint);
				}
			},
			    Map.ScalarEntries);
		};
		bool bEnabled = false;
		const std::string& ValueType = Map.ValueTypeName;
		if (ValueType == "char" || ValueType == "int8")
		{
			bEnabled = EmplaceScalarMapStorage<std::int8_t>(Map, KeyKind);
		}
		else if (ValueType == "unsigned char" || ValueType == "uint8")
		{
			bEnabled = EmplaceScalarMapStorage<std::uint8_t>(Map, KeyKind);
		}
		else if (ValueType == "short" || ValueType == "int16")
		{
			bEnabled = EmplaceScalarMapStorage<std::int16_t>(Map, KeyKind);
		}
		else if (ValueType == "unsigned short" || ValueType == "uint16")
		{
			bEnabled = EmplaceScalarMapStorage<std::uint16_t>(Map, KeyKind);
		}
		else if (ValueType == "int" || ValueType == "int32")
		{
			bEnabled = EmplaceScalarMapStorage<std::int32_t>(Map, KeyKind);
		}
		else if (ValueType == "unsigned int" || ValueType == "uint" || ValueType == "unsigned" || ValueType == "uint32")
		{
			bEnabled = EmplaceScalarMapStorage<std::uint32_t>(Map, KeyKind);
		}
		else if (ValueType == "int64" || ValueType == "long")
		{
			bEnabled = EmplaceScalarMapStorage<std::int64_t>(Map, KeyKind);
		}
		else if (ValueType == "uint64" || ValueType == "unsigned long")
		{
			bEnabled = EmplaceScalarMapStorage<std::uint64_t>(Map, KeyKind);
		}
		else if (ValueType == "float")
		{
			bEnabled = EmplaceScalarMapStorage<float>(Map, KeyKind);
		}
		else if (ValueType == "double")
		{
			bEnabled = EmplaceScalarMapStorage<double>(Map, KeyKind);
		}
		else if (ValueType == "bool")
		{
			bEnabled = EmplaceScalarMapStorage<bool>(Map, KeyKind);
		}
		if (bEnabled)
		{
			ReserveTypedStorage();
		}
		return bEnabled;
	}

	bool VeroScriptVm::JitEnableScalarSetStorage(FValue& Container)
	{
		auto* const Handle = std::get_if<FSetHandle>(&Container);
		if (!Handle || !*Handle)
		{
			return false;
		}
		FSet& Set = **Handle;
		if (Set.ScalarElements.index() != 0 || !Set.IntElements.empty() || !Set.UIntElements.empty() || !Set.Int64Elements.empty() || !Set.UInt64Elements.empty() || !Set.StrElements.empty() || !Set.WStrElements.empty() || !Set.EnumElements.empty() || !Set.HostElements.empty())
		{
			return false;
		}
		switch (ScalarMapKeyKind(Set.ElementTypeName))
		{
		case EScalarMapKeyKind::I32:
			{
				Set.ScalarElements.emplace<FSet::FIntScalarElements>();
				break;
			}
		case EScalarMapKeyKind::U32:
			{
				Set.ScalarElements.emplace<FSet::FUIntScalarElements>();
				break;
			}
		case EScalarMapKeyKind::I64:
			{
				Set.ScalarElements.emplace<FSet::FInt64ScalarElements>();
				break;
			}
		case EScalarMapKeyKind::U64:
			{
				Set.ScalarElements.emplace<FSet::FUInt64ScalarElements>();
				break;
			}
		case EScalarMapKeyKind::String:
			{
				Set.ScalarElements.emplace<FSet::FStringScalarElements>();
				break;
			}
		case EScalarMapKeyKind::WString:
			{
				Set.ScalarElements.emplace<FSet::FWStringScalarElements>();
				break;
			}
		default:
			return false;
		}
		if (Set.ReservedCapacityHint != 0)
		{
			std::visit(
			    [&Set](auto& Elements)
			{
				using TElements = std::decay_t<decltype(Elements)>;
				if constexpr (!std::is_same_v<TElements, std::monostate>)
				{
					Elements.reserve(Set.ReservedCapacityHint);
				}
			},
			    Set.ScalarElements);
		}
		return true;
	}

	bool VeroScriptVm::JitEnableScalarArrayStorage(FValue& Container)
	{
		auto* const Handle = std::get_if<FDynamicArrayHandle>(&Container);
		if (!Handle || !*Handle)
		{
			return false;
		}
		FDynamicArray& Array = **Handle;
		return Array.EnableScalarStorage();
	}

	bool VeroScriptVm::JitTryGetScalarMap(
	    const FValue& Container,
	    const FValue& Index,
	    FValue& OutValue,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		const auto* const Handle = std::get_if<FMapHandle>(&Container);
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
			const auto FindGeneric = [&](const auto& Entries, const auto* const Key) -> bool
			{
				if (!Key)
				{
					return true;
				}
				bOutHandled = true;
				const auto It = Entries.find(*Key);
				if (It == Entries.end())
				{
					ReportError("map key not found");
					return false;
				}
				OutValue = It->second;
				return true;
			};
			if ((*Handle)->KeyTypeName == "string")
			{
				return FindGeneric(
				    (*Handle)->StrEntries, std::get_if<std::string>(&Index));
			}
			if ((*Handle)->KeyTypeName == "wstring")
			{
				return FindGeneric(
				    (*Handle)->WStrEntries, std::get_if<std::u16string>(&Index));
			}
			return true;
		}
		bOutHandled = true;
		if (const auto* const Key = std::get_if<std::int32_t>(&Index))
		{
			if (const auto* const Entries =
			        std::get_if<FMap::FIntI32Entries>(&(*Handle)->ScalarEntries))
			{
				const std::int32_t* const Found = Entries->Find(*Key);
				if (!Found)
				{
					ReportError("map key not found");
					return false;
				}
				OutValue = *Found;
				return true;
			}
		}
		bool bOk = true;
		std::visit(
		    [&](const auto& Entries)
		{
			using FEntries = std::decay_t<decltype(Entries)>;
			if constexpr (!std::is_same_v<FEntries, std::monostate>)
			{
				using FKey = typename FEntries::key_type;
				using FMapped = typename FEntries::mapped_type;
				const auto* const Key = std::get_if<FKey>(&Index);
				if (!Key)
				{
					ReportError("map key type mismatch for " + (*Handle)->TypeName);
					bOk = false;
					return;
				}
				const auto* const Found = Entries.Find(*Key);
				if (!Found)
				{
					ReportError("map key not found");
					bOk = false;
					return;
				}
				if constexpr (std::is_same_v<FMapped, FMap::FPrimitiveScalar>)
				{
					OutValue = FMap::UnpackPrimitiveScalar(*Found);
				}
				else
				{
					OutValue = *Found;
				}
			}
		},
		    (*Handle)->ScalarEntries);
		return bOk;
	}

	bool VeroScriptVm::JitTrySetScalarMap(
	    FValue& Container,
	    const FValue& Index,
	    const FValue& Value,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		auto* const Handle = std::get_if<FMapHandle>(&Container);
		if (!Handle || !*Handle)
		{
			return true;
		}
		if ((*Handle)->ScalarEntries.index() == 0)
		{
			if (!Runtime::IsPrimitiveValueTypeName((*Handle)->ValueTypeName) || (*Handle)->ValueTypeName == "string" || (*Handle)->ValueTypeName == "wstring" || !Runtime::ValueMatchesPrimitiveTypeName(Value, (*Handle)->ValueTypeName))
			{
				return true;
			}
			const auto StoreGeneric = [&](auto& Entries, const auto* const Key) -> bool
			{
				if (!Key)
				{
					return true;
				}
				Entries.insert_or_assign(*Key, Value);
				bOutHandled = true;
				if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
				{
					NotifyContainerMutation(Container, EMutationKind::ContainerSet);
				}
				return !bHasError;
			};
			if ((*Handle)->KeyTypeName == "string")
			{
				return StoreGeneric(
				    (*Handle)->StrEntries, std::get_if<std::string>(&Index));
			}
			if ((*Handle)->KeyTypeName == "wstring")
			{
				return StoreGeneric(
				    (*Handle)->WStrEntries, std::get_if<std::u16string>(&Index));
			}
			return true;
		}
		bOutHandled = true;
		if (const auto* const Key = std::get_if<std::int32_t>(&Index))
		{
			if (const auto* const Mapped = std::get_if<std::int32_t>(&Value))
			{
				if (auto* const Entries =
				        std::get_if<FMap::FIntI32Entries>(&(*Handle)->ScalarEntries))
				{
					Entries->InsertOrAssign(*Key, *Mapped);
					if ((*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
					{
						NotifyContainerMutation(Container, EMutationKind::ContainerSet);
					}
					return !bHasError;
				}
			}
		}
		bool bOk = true;
		std::visit(
		    [&](auto& Entries)
		{
			using FEntries = std::decay_t<decltype(Entries)>;
			if constexpr (!std::is_same_v<FEntries, std::monostate>)
			{
				using FKey = typename FEntries::key_type;
				using FMapped = typename FEntries::mapped_type;
				const auto* const Key = std::get_if<FKey>(&Index);
				if (!Key)
				{
					ReportError("map key type mismatch for " + (*Handle)->TypeName);
					bOk = false;
					return;
				}
				if constexpr (std::is_same_v<FMapped, FMap::FPrimitiveScalar>)
				{
					FMap::FPrimitiveScalar Mapped;
					if (!Runtime::ValueMatchesPrimitiveTypeName(
					        Value, (*Handle)->ValueTypeName) ||
					    !FMap::TryPackPrimitiveScalar(Value, Mapped))
					{
						ReportError("map value type mismatch for " + (*Handle)->TypeName);
						bOk = false;
						return;
					}
					Entries.InsertOrAssign(*Key, Mapped);
				}
				else
				{
					const auto* const Mapped = std::get_if<FMapped>(&Value);
					if (!Mapped)
					{
						ReportError("map value type mismatch for " + (*Handle)->TypeName);
						bOk = false;
						return;
					}
					Entries.InsertOrAssign(*Key, *Mapped);
				}
			}
		},
		    (*Handle)->ScalarEntries);
		if (bOk && (*Handle)->MutationOrigin.FieldIndex != static_cast<std::uint16_t>(-1))
		{
			NotifyContainerMutation(Container, EMutationKind::ContainerSet);
		}
		return bOk && !bHasError;
	}

	bool VeroScriptVm::JitSetIndex(
	    FValue Container,
	    const FValue& Index,
	    const FValue& Value,
	    FValue& OutContainer)
	{
		OutContainer = std::int32_t{0};
		if (!CheckContainerCopyMutation(Container)) return false;
		VmDetail::FContainerCopyReadScope CopyReadScope(ActiveContainerCopyReads,
		    VmDetail::ValueCopyMayInvokeCallbacks(Value) ? ContainerStorageIdentity(Container) : nullptr);
		struct FIndexMutationGuard
		{
			~FIndexMutationGuard()
			{
				if (Vm && bError && !*bError && Container)
				{
					Vm->NotifyContainerMutation(
					    *Container, EMutationKind::ContainerSet);
				}
			}

			VeroScriptVm* Vm = nullptr;
			const bool* bError = nullptr;
			const FValue* Container = nullptr;

		} MutationGuard{
		    this,
		    &bHasError,
		    &OutContainer};
		const auto AssignEntryValue = [&](FValue& Destination)
		{
			return AssignClonedValue(Destination, Value, {});
		};
		// Fast path: dynamic array + i32 index.
		if (auto* Arr = std::get_if<FDynamicArrayHandle>(&Container))
		{
			if (*Arr && std::holds_alternative<std::int32_t>(Index))
			{
				const std::int32_t Idx = std::get<std::int32_t>(Index);
				const std::size_t Size = (*Arr)->HasScalarElements()
				    ? (*Arr)->ScalarSize()
				    : (*Arr)->Elements.size();
				if (Idx < 0 || static_cast<std::size_t>(Idx) >= Size)
				{
					ReportError("array index out of bounds");
					return false;
				}
				if (!ValueMatchesElementType(Value, (*Arr)->ElementTypeName, {}))
				{
					ReportError(
					    "array element type mismatch for array<" + (*Arr)->ElementTypeName + ">");
					return false;
				}
				if ((*Arr)->HasScalarElements())
				{
					if (!(*Arr)->TrySetScalar(static_cast<std::size_t>(Idx), Value))
					{
						ReportError("invalid scalar array storage");
						return false;
					}
				}
				else if (!AssignClonedValue(
				             (*Arr)->Elements[static_cast<std::size_t>(Idx)],
				             Value,
				             {}))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
		}
		if (auto* MapHandle = std::get_if<FMapHandle>(&Container))
		{
			if (!*MapHandle)
			{
				ReportError("index assign requires map");
				return false;
			}
			if (!ValueMatchesElementType(Value, (*MapHandle)->ValueTypeName, {}))
			{
				ReportError("map value type mismatch for " + (*MapHandle)->TypeName);
				return false;
			}
			if (VmDetail::IsSigned32KeyStorageType((*MapHandle)->KeyTypeName))
			{
				std::int32_t Key = 0;
				if (!VmDetail::TryGetSigned32Key(Index, (*MapHandle)->KeyTypeName, Key))
				{
					ReportError("map key must be " + (*MapHandle)->KeyTypeName);
					return false;
				}
				if (!VmDetail::AssignMapEntry((*MapHandle)->IntEntries, Key, AssignEntryValue))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*MapHandle)->KeyTypeName == "unsigned int")
			{
				const auto* Key = std::get_if<std::uint32_t>(&Index);
				if (!Key)
				{
					ReportError("map key must be unsigned int");
					return false;
				}
				if (!VmDetail::AssignMapEntry((*MapHandle)->UIntEntries, *Key, AssignEntryValue))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*MapHandle)->KeyTypeName == "int64")
			{
				const auto* Key = std::get_if<std::int64_t>(&Index);
				if (!Key)
				{
					ReportError("map key must be int64");
					return false;
				}
				if (!VmDetail::AssignMapEntry((*MapHandle)->Int64Entries, *Key, AssignEntryValue))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*MapHandle)->KeyTypeName == "uint64")
			{
				const auto* Key = std::get_if<std::uint64_t>(&Index);
				if (!Key)
				{
					ReportError("map key must be uint64");
					return false;
				}
				if (!VmDetail::AssignMapEntry((*MapHandle)->UInt64Entries, *Key, AssignEntryValue))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*MapHandle)->KeyTypeName == "string")
			{
				const auto* Key = std::get_if<std::string>(&Index);
				if (!Key)
				{
					ReportError("map key must be string");
					return false;
				}
				if (!VmDetail::AssignMapEntry((*MapHandle)->StrEntries, *Key, AssignEntryValue))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*MapHandle)->KeyTypeName == "wstring")
			{
				const auto* Key = std::get_if<std::u16string>(&Index);
				if (!Key)
				{
					ReportError("map key must be wstring");
					return false;
				}
				if (!VmDetail::AssignMapEntry((*MapHandle)->WStrEntries, *Key, AssignEntryValue))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if (const auto* Key = std::get_if<FEnumValue>(&Index);
			    Key && Key->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*MapHandle)->KeyTypeName))
			{
				if (!VmDetail::AssignMapEntry((*MapHandle)->EnumEntries, *Key, AssignEntryValue))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if (const auto* Key = std::get_if<FHostObjectHandle>(&Index);
			    Key && *Key && ValueMatchesElementType(Index, (*MapHandle)->KeyTypeName, {}))
			{
				StampHostValueCallbacks(*Key);
				const auto Existing = (*MapHandle)->HostEntries.find(*Key);
				if (Existing != (*MapHandle)->HostEntries.end())
				{
					if (!AssignClonedValue(Existing->second, Value, {}))
					{
						return false;
					}
				}
				else
				{
					FHostObjectHandle StoredKey;
					if (!PrepareHostKeyForInsert(*Key, StoredKey, {}) || !VmDetail::AssignMapEntry((*MapHandle)->HostEntries, StoredKey, AssignEntryValue))
					{
						return false;
					}
				}
				OutContainer = std::move(Container);
				return true;
			}
			ReportError("invalid map key type");
			return false;
		}
		if (auto* OrderedMapHandle = std::get_if<FOrderedMapHandle>(&Container))
		{
			if (!*OrderedMapHandle)
			{
				ReportError("index assign requires ordered_map");
				return false;
			}
			if (!ValueMatchesElementType(Value, (*OrderedMapHandle)->ValueTypeName, {}))
			{
				ReportError("ordered_map value type mismatch for " + (*OrderedMapHandle)->TypeName);
				return false;
			}
			if (VmDetail::IsSigned32KeyStorageType((*OrderedMapHandle)->KeyTypeName))
			{
				std::int32_t Key = 0;
				if (!VmDetail::TryGetSigned32Key(
				        Index, (*OrderedMapHandle)->KeyTypeName, Key))
				{
					ReportError("ordered_map key must be " + (*OrderedMapHandle)->KeyTypeName);
					return false;
				}
				if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->IntEntries, Key, AssignEntryValue, &(*OrderedMapHandle)->IntKeyOrder))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*OrderedMapHandle)->KeyTypeName == "unsigned int")
			{
				const auto* KeyValue = std::get_if<std::uint32_t>(&Index);
				if (!KeyValue)
				{
					ReportError("ordered_map key must be unsigned int");
					return false;
				}
				const std::uint32_t Key = *KeyValue;
				if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->UIntEntries, Key, AssignEntryValue, &(*OrderedMapHandle)->UIntKeyOrder))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*OrderedMapHandle)->KeyTypeName == "int64")
			{
				const auto* KeyValue = std::get_if<std::int64_t>(&Index);
				if (!KeyValue)
				{
					ReportError("ordered_map key must be int64");
					return false;
				}
				const std::int64_t Key = *KeyValue;
				if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->Int64Entries, Key, AssignEntryValue, &(*OrderedMapHandle)->Int64KeyOrder))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*OrderedMapHandle)->KeyTypeName == "uint64")
			{
				const auto* KeyValue = std::get_if<std::uint64_t>(&Index);
				if (!KeyValue)
				{
					ReportError("ordered_map key must be uint64");
					return false;
				}
				const std::uint64_t Key = *KeyValue;
				if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->UInt64Entries, Key, AssignEntryValue, &(*OrderedMapHandle)->UInt64KeyOrder))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*OrderedMapHandle)->KeyTypeName == "string")
			{
				const auto* Key = std::get_if<std::string>(&Index);
				if (!Key)
				{
					ReportError("ordered_map key must be string");
					return false;
				}
				if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->StrEntries, *Key, AssignEntryValue, &(*OrderedMapHandle)->StrKeyOrder))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if ((*OrderedMapHandle)->KeyTypeName == "wstring")
			{
				const auto* Key = std::get_if<std::u16string>(&Index);
				if (!Key)
				{
					ReportError("ordered_map key must be wstring");
					return false;
				}
				if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->WStrEntries, *Key, AssignEntryValue, &(*OrderedMapHandle)->WStrKeyOrder))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if (const auto* Key = std::get_if<FEnumValue>(&Index);
			    Key && Key->TypeName == Runtime::EnumValueTypeName(*ActiveModule, (*OrderedMapHandle)->KeyTypeName))
			{
				if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->EnumEntries, *Key, AssignEntryValue, &(*OrderedMapHandle)->EnumKeyOrder))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			if (const auto* Key = std::get_if<FHostObjectHandle>(&Index);
			    Key && *Key && ValueMatchesElementType(Index, (*OrderedMapHandle)->KeyTypeName, {}))
			{
				StampHostValueCallbacks(*Key);
				const auto Existing = (*OrderedMapHandle)->HostEntries.find(*Key);
				if (Existing == (*OrderedMapHandle)->HostEntries.end())
				{
					FHostObjectHandle StoredKey;
					if (!PrepareHostKeyForInsert(*Key, StoredKey, {}))
					{
						return false;
					}
					if (!VmDetail::AssignMapEntry((*OrderedMapHandle)->HostEntries, StoredKey, AssignEntryValue, &(*OrderedMapHandle)->HostKeyOrder))
					{
						return false;
					}
				}
				else if (!AssignClonedValue(Existing->second, Value, {}))
				{
					return false;
				}
				OutContainer = std::move(Container);
				return true;
			}
			ReportError("invalid ordered_map key type");
			return false;
		}
		if (!Runtime::IsIntegerValue(Index))
		{
			ReportError("array index must be an integer");
			return false;
		}
		std::size_t ResolvedIndex = 0;
		if (!Runtime::TryGetSizeValue(Index, ResolvedIndex))
		{
			ReportError(std::holds_alternative<FDequeHandle>(Container)
			        ? "deque index out of bounds"
			        : "array index out of bounds");
			return false;
		}
		const std::size_t Idx = ResolvedIndex;
		if (auto* NativeArray = std::get_if<FNativeArray>(&Container))
		{
			if (static_cast<std::uint64_t>(Idx) >= NativeArray->Length() || static_cast<std::size_t>(Idx) >= NativeArray->Storage().Elements.size())
			{
				ReportError("array index out of bounds");
				return false;
			}
			if (!ValueMatchesElementType(Value, NativeArray->ElementTypeName(), {}))
			{
				ReportError("native array element type mismatch for " + NativeArray->TypeName());
				return false;
			}
			if (!AssignClonedValue(
			        NativeArray->Storage().Elements[static_cast<std::size_t>(Idx)],
			        Value,
			        {}))
			{
				return false;
			}
			OutContainer = std::move(Container);
			return true;
		}
		if (auto* DequeHandle = std::get_if<FDequeHandle>(&Container))
		{
			if (!*DequeHandle)
			{
				ReportError("index assign requires deque");
				return false;
			}
			if (static_cast<std::size_t>(Idx) >= (*DequeHandle)->Elements.size())
			{
				ReportError("deque index out of bounds");
				return false;
			}
			if (!ValueMatchesElementType(Value, (*DequeHandle)->ElementTypeName, {}))
			{
				ReportError("deque element type mismatch for " + (*DequeHandle)->TypeName);
				return false;
			}
			if (!AssignClonedValue(
			        (*DequeHandle)->Elements[static_cast<std::size_t>(Idx)],
			        Value,
			        {}))
			{
				return false;
			}
			OutContainer = std::move(Container);
			return true;
		}
		if (auto* DynamicArray = std::get_if<FDynamicArrayHandle>(&Container))
		{
			if (!*DynamicArray)
			{
				ReportError("index assign requires array");
				return false;
			}
			const std::size_t Size = (*DynamicArray)->HasScalarElements()
			    ? (*DynamicArray)->ScalarSize()
			    : (*DynamicArray)->Elements.size();
			if (static_cast<std::size_t>(Idx) >= Size)
			{
				ReportError("array index out of bounds");
				return false;
			}
			if (!ValueMatchesElementType(Value, (*DynamicArray)->ElementTypeName, {}))
			{
				ReportError(
				    "array element type mismatch for array<" + (*DynamicArray)->ElementTypeName + ">");
				return false;
			}
			if ((*DynamicArray)->HasScalarElements())
			{
				if (!(*DynamicArray)->TrySetScalar(static_cast<std::size_t>(Idx), Value))
				{
					ReportError("invalid scalar array storage");
					return false;
				}
			}
			else if (!AssignClonedValue(
			             (*DynamicArray)->Elements[static_cast<std::size_t>(Idx)],
			             Value,
			             {}))
			{
				return false;
			}
			OutContainer = std::move(Container);
			return true;
		}
		ReportError("index assign requires array");
		return false;
	}

	bool VeroScriptVm::JitTryPushDynamicArrayScalar(
	    const FValue& Container,
	    const FValue& Value,
	    bool& bOutHandled)
	{
		bOutHandled = false;
		const auto* const Array = std::get_if<FDynamicArrayHandle>(&Container);
		if (!Array)
		{
			return true;
		}
		if (!*Array)
		{
			ReportError("method call requires object");
			return false;
		}
		if ((*Array)->HasScalarElements())
		{
			if (!(*Array)->TryPushScalar(Value))
			{
				return true;
			}
			NotifyContainerMutation(Container, EMutationKind::ContainerAdd);
			bOutHandled = true;
			return !bHasError;
		}
		if (!Runtime::ValueMatchesPrimitiveTypeName(
		        Value, (*Array)->ElementTypeName))
		{
			return true;
		}
		(*Array)->Elements.push_back(Value);
		NotifyContainerMutation(Container, EMutationKind::ContainerAdd);
		bOutHandled = true;
		return !bHasError;
	}

}
