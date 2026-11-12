// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptInteropInternal.h"
#include "VeroScriptModuleMeta.h"
#include "VeroScriptRuntime.h"

#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

namespace VeroScript::Interop
{
	thread_local FSessionState* ActiveValueSession = nullptr;
	FStagedValueResults::FStagedValueResults(FSessionState& InState, FValue& Return, std::vector<FValue>* Arguments)
	    : State(InState)
	{
		Slots.reserve(1 + (Arguments ? Arguments->size() : 0));
		const auto Reserve = [&](FValue& Value)
		{
			FValueRoot Root;
			std::string Error;
			if (!State.Session.ReserveValueResult(Root, Error))
			{
				throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
			}
			Slots.emplace_back(&Value, std::move(Root));
		};
		Reserve(Return);
		if (Arguments)
		{
			for (auto& Value : *Arguments)
			{
				// A primitive ref/out parameter cannot acquire an object or container.
				if (std::holds_alternative<FObjectHandle>(Value) || std::holds_alternative<FFunctionHandle>(Value) || std::holds_alternative<FScriptVariantHandle>(Value) || std::holds_alternative<FNativeArray>(Value) || std::holds_alternative<FDynamicArrayHandle>(Value) || std::holds_alternative<FMapHandle>(Value) || std::holds_alternative<FOrderedMapHandle>(Value) || std::holds_alternative<FQueueHandle>(Value) || std::holds_alternative<FStackHandle>(Value) || std::holds_alternative<FDequeHandle>(Value))
				{
					Reserve(Value);
				}
			}
		}
	}
	FStagedValueResults::~FStagedValueResults() noexcept
	{
		const auto Retire = [](auto& Slot)
		{
			// This reservation is private cleanup storage, never a C export. The
			// Session export policy can reject replacement-initialization values,
			// but their script cleanup must still run before rollback drops them.
			if (const auto Owner = Slot.second.Owner())
			{
				std::string Error;
				(void)Owner->CommitReserved(Slot.second, *Slot.first, Error);
			}
		};
		for (auto& Slot : Slots)
		{
			// Capacity and nodes were reserved before any script value was created.
			try
			{
				if (ValueRequiresOwner(*Slot.first))
				{
					Retire(Slot);
				}
			}
			catch (...)
			{
				// If graph classification itself runs out of memory, preserve the
				// result for the already-reserved owner cleanup rather than dropping
				// a newly created script object through a raw C++ destructor.
				try
				{
					Retire(Slot);
				}
				catch (...)
				{
				}
			}
		}
	}
	const ::vs_interop_value* FindValueOwner(const ::vs_interop_value* const* Values, std::uint64_t Count)
	{
		const ::vs_interop_value* Result = nullptr;
		for (std::uint64_t I = 0; I < Count; ++I)
		{
			if (!Values[I])
			{
				continue;
			}
			const auto* Data = ReadValue(Values[I]);
			const auto& Identity = Values[I]->Owner;
			if (Result && !Result->Owner.owner_before(Identity) && !Identity.owner_before(Result->Owner))
			{
				continue;
			}
			const auto Owner = Identity.lock();
			if (!Data || !ValueNeedsSessionOwner(*Data, ValueOwnerContext(*Values[I], Owner), Values[I]->NativeContext))
			{
				continue;
			}
			// Borrowed independent values may come from different callbacks. Only
			// module graphs constrain the aggregate, including during final teardown
			// when their weak owner identities still exist but can no longer lock.
			const std::weak_ptr<FSessionState> Empty;
			if ((!Identity.owner_before(Empty) && !Empty.owner_before(Identity)) || (Result && (Result->Owner.owner_before(Identity) || Identity.owner_before(Result->Owner))))
			{
				throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
			}
			Result = Values[I];
		}
		return Result;
	}
	namespace
	{
		bool HasOwnerIdentity(const std::weak_ptr<FSessionState>& Owner) noexcept
		{
			const std::weak_ptr<FSessionState> Empty;
			return Owner.owner_before(Empty) || Empty.owner_before(Owner);
		}

		void DrainAfterValuePublication(const std::shared_ptr<FSessionState>& Owner) noexcept
		{
			if (!Owner || Owner->ActiveSessionCalls != 0) return;
			try
			{
				const FScopedSessionActivity Activity(*Owner);
			}
			catch (...)
			{
				// The replacement/reset is already committed. Keep cleanup errors on
				// the old owner, as at an activity's exit, without reporting a failed
				// publication after the caller's output has changed.
				(void)TranslateException(Owner.get());
			}
		}

		struct FInteropHostObject final : FHostObject
		{
			~FInteropHostObject() noexcept
			{
				if (!bOwnershipCommitted || !Destroy || !Payload)
				{
					return;
				}
				try
				{
					Destroy(Payload);
				}
				catch (...)
				{
				}
			}

			vs_interop_host_object_destroy Destroy = nullptr;
			vs_interop_host_object_is_type IsType = nullptr;
			void* ForeignIsTypeUserData = nullptr;
			bool bOwnershipCommitted = false;
		};

		bool InteropHostObjectIsType(
		    void* const Payload,
		    void* const UserData,
		    const std::string_view ExpectedTypeName)
		{
			auto* const Object = static_cast<FInteropHostObject*>(UserData);
			if (!Object || !Object->IsType)
			{
				return false;
			}
			try
			{
				return Object->IsType(
				           Payload,
				           Object->ForeignIsTypeUserData,
				           {ExpectedTypeName.data(), static_cast<uint64_t>(ExpectedTypeName.size())}) != 0;
			}
			catch (...)
			{
				return false;
			}
		}

		bool IsSaneValueTypeName(const std::string_view TypeName)
		{
			const std::string Canonical = CanonicalizeNativeElementTypeName(TypeName);
			if (Canonical.empty() || Canonical == "void")
			{
				return false;
			}
			std::size_t Angles = 0;
			std::size_t Parentheses = 0;
			std::size_t Brackets = 0;
			for (std::size_t Index = 0; Index < Canonical.size(); ++Index)
			{
				const unsigned char Character =
				    static_cast<unsigned char>(Canonical[Index]);
				if (Character == 0 || Character < 0x20 || Character == 0x7f || Character == ';' || Character == '{' || Character == '}')
				{
					return false;
				}
				switch (Character)
				{
				case '<':
					{
						++Angles;
						break;
					}
				case '>':
					{
						if (Index != 0 && Canonical[Index - 1] == '-')
						{
							break;
						}
						if (Angles == 0)
						{
							return false;
						}
						--Angles;
						break;
					}
				case '(':
					{
						++Parentheses;
						break;
					}
				case ')':
					{
						if (Parentheses == 0)
						{
							return false;
						}
						--Parentheses;
						break;
					}
				case '[':
					{
						++Brackets;
						break;
					}
				case ']':
					{
						if (Brackets == 0)
						{
							return false;
						}
						--Brackets;
						break;
					}
				default:
					{
						break;
					}
				}
			}
			return Angles == 0 && Parentheses == 0 && Brackets == 0;
		}

		bool HasGenericPrefix(const std::string_view TypeName, const std::string_view Prefix)
		{
			return TypeName.size() > Prefix.size() + 1 && TypeName.compare(0, Prefix.size(), Prefix) == 0 && TypeName.back() == '>';
		}

		bool IsSaneMapOrSetKeyTypeName(const std::string_view CanonicalTypeName)
		{
			if (CanonicalTypeName == "variant") return false;
			if (!IsSaneValueTypeName(CanonicalTypeName)) return false;
			if (Runtime::IsPrimitiveValueTypeName(CanonicalTypeName))
			{
				return Runtime::IsAllowedMapOrSetKeyType(CanonicalTypeName);
			}
			// No session catalog is available here. Retain named enum/host types
			// for semantic validation by the consuming session, but never create
			// a builtin container/callable/reference key, including an empty one.
			return !Runtime::IsNativeArrayTypeName(CanonicalTypeName) && !Runtime::IsDynamicArrayTypeName(CanonicalTypeName) && !Runtime::IsCommonContainerTypeName(CanonicalTypeName) && !Runtime::IsPriorityQueueTypeName(CanonicalTypeName) && !Runtime::IsChannelTypeName(CanonicalTypeName) && !Runtime::IsFunctionTypeName(CanonicalTypeName) && !Runtime::IsReferenceTypeName(CanonicalTypeName);
		}

		bool IsPriorityQueueElementType(const std::string_view TypeName)
		{
			return TypeName == "bool" || TypeName == "char" || TypeName == "short" || TypeName == "int" || TypeName == "unsigned int" || TypeName == "long" || TypeName == "int64" || TypeName == "uint64" || TypeName == "string" || TypeName == "wstring";
		}

		bool IsNullableReferenceTypeName(const std::string_view TypeName)
		{
			// `nullptr` is a reference value, never a substitute for scalar or
			// container values. Host/user object names deliberately fall through.
			return TypeName != "variant" && TypeName != "bool" && TypeName != "char" && TypeName != "short" && TypeName != "unsigned char" && TypeName != "unsigned short" && TypeName != "int" && TypeName != "unsigned int" && TypeName != "long" && TypeName != "unsigned long" && TypeName != "int64" && TypeName != "uint64" && TypeName != "float" && TypeName != "double" && TypeName != "string" && TypeName != "wstring" && !HasGenericPrefix(TypeName, "array<") && !HasGenericPrefix(TypeName, "map<") && !HasGenericPrefix(TypeName, "multimap<") && !HasGenericPrefix(TypeName, "ordered_map<") && !HasGenericPrefix(TypeName, "ordered_multimap<") && !HasGenericPrefix(TypeName, "set<") && !HasGenericPrefix(TypeName, "multiset<") && !HasGenericPrefix(TypeName, "ordered_multiset<") && !HasGenericPrefix(TypeName, "queue<") && !HasGenericPrefix(TypeName, "priority_queue<") && !HasGenericPrefix(TypeName, "stack<") && !HasGenericPrefix(TypeName, "deque<") && !HasGenericPrefix(TypeName, "channel<");
		}

		bool ValueMatchesDeclaredType(
		    const FValue& Value,
		    const std::string_view ExpectedTypeName,
		    const ::vs_interop_value* Source)
		{
			const std::string Expected =
			    CanonicalizeNativeElementTypeName(ExpectedTypeName);
			if (Expected == "variant") return std::holds_alternative<FScriptVariantHandle>(Value);
			if (const auto* Object = std::get_if<FObjectHandle>(&Value))
			{
				if (!*Object)
				{
					return IsNullableReferenceTypeName(Expected);
				}
				if (Expected == "object" || (*Object)->GetTypeName() == Expected)
				{
					return true;
				}
				if (Source)
				{
					// A callback may execute an older generation, or on a worker with
					// no Session. Its invoker owns the authoritative subtype metadata.
					const auto* Invoker = Source->NativeContext ? Source->NativeContext->ScriptInvoker : nullptr;
					const auto* Module = Invoker ? Invoker->GetActiveModule() : nullptr;
					const auto Owner = Source->Owner.lock();
					const auto* State = ValueOwnerContext(*Source, Owner);
					if (!Invoker && State)
					{
						Module = State->Session.GetLoadedModule();
					}
					// RuntimeTypeModule is only an immutable cache hint; migrated
					// objects can legitimately retain an earlier module's hint.
					if (Module && ModuleMeta::IsObjectTypeAssignableTo(*Module, (*Object)->GetTypeName(), Expected))
					{
						return true;
					}
				}
				return (*Object)->GetHostBase() && HostObjectMatchesType((*Object)->GetHostBase(), Expected);
			}
			if (const auto* Host = std::get_if<FHostObjectHandle>(&Value))
			{
				if (!*Host)
				{
					return IsNullableReferenceTypeName(Expected);
				}
				return HostObjectMatchesType(*Host, Expected);
			}
			if (const auto* Enum = std::get_if<FEnumValue>(&Value))
			{
				return NativeElementTypeNamesEqual(Enum->TypeName, Expected);
			}
			if (std::holds_alternative<std::int32_t>(Value))
			{
				return NativeTypeMatchesTypeName(ENativeType::Int, Expected);
			}
			if (std::holds_alternative<std::uint32_t>(Value))
			{
				return NativeTypeMatchesTypeName(ENativeType::Unsigned, Expected);
			}
			// Use the core's width/alias contract, not the host C++ long width.
			if (std::holds_alternative<std::int64_t>(Value))
			{
				return NativeTypeMatchesTypeName(ENativeType::Int64, Expected);
			}
			if (std::holds_alternative<std::uint64_t>(Value))
			{
				return NativeTypeMatchesTypeName(ENativeType::UInt64, Expected);
			}
			if (std::holds_alternative<double>(Value))
			{
				return Expected == "double";
			}
			if (std::holds_alternative<float>(Value))
			{
				return Expected == "float";
			}
			if (std::holds_alternative<std::int8_t>(Value))
			{
				return Expected == "char";
			}
			if (std::holds_alternative<std::uint8_t>(Value))
			{
				return Expected == "unsigned char";
			}
			if (std::holds_alternative<std::int16_t>(Value))
			{
				return Expected == "short";
			}
			if (std::holds_alternative<std::uint16_t>(Value))
			{
				return Expected == "unsigned short";
			}
			if (std::holds_alternative<bool>(Value))
			{
				return Expected == "bool";
			}
			if (std::holds_alternative<std::string>(Value))
			{
				return Expected == "string";
			}
			if (std::holds_alternative<std::u16string>(Value))
			{
				return Expected == "wstring";
			}

			std::string Actual;
			if (const auto* NativeArray = std::get_if<FNativeArray>(&Value))
			{
				Actual = NativeArray->TypeName();
			}
			else if (const auto* Array = std::get_if<FDynamicArrayHandle>(&Value); Array && *Array)
			{
				Actual = (*Array)->TypeName;
			}
			else if (const auto* Map = std::get_if<FMapHandle>(&Value); Map && *Map)
			{
				Actual = (*Map)->TypeName;
			}
			else if (const auto* Set = std::get_if<FSetHandle>(&Value); Set && *Set)
			{
				Actual = (*Set)->TypeName;
			}
			else if (const auto* Queue = std::get_if<FQueueHandle>(&Value); Queue && *Queue)
			{
				Actual = (*Queue)->TypeName;
			}
			else if (const auto* Stack = std::get_if<FStackHandle>(&Value); Stack && *Stack)
			{
				Actual = (*Stack)->TypeName;
			}
			else if (const auto* OrderedMap = std::get_if<FOrderedMapHandle>(&Value);
			    OrderedMap && *OrderedMap)
			{
				Actual = (*OrderedMap)->TypeName;
			}
			else if (const auto* Deque = std::get_if<FDequeHandle>(&Value); Deque && *Deque)
			{
				Actual = (*Deque)->TypeName;
			}
			else if (const auto* Channel = std::get_if<FChannelHandle>(&Value); Channel && *Channel)
			{
				std::string ElementType;
				(*Channel)->GetTypeNames(Actual, ElementType);
			}
			else if (const auto* Function = std::get_if<FFunctionHandle>(&Value); Function && *Function)
			{
				Actual = (*Function)->TypeName;
			}
			return !Actual.empty() && NativeElementTypeNamesEqual(Actual, Expected);
		}

		bool ValuesEquivalentForWriteBack(
		    const FValue& Left,
		    const FValue& Right,
		    const std::size_t Depth = 0)
		{
			if (Left.index() != Right.index() || Depth >= 64)
			{
				return false;
			}
			if (const auto* Value = std::get_if<std::int32_t>(&Left))
			{
				return *Value == std::get<std::int32_t>(Right);
			}
			if (const auto* Value = std::get_if<std::int64_t>(&Left))
			{
				return *Value == std::get<std::int64_t>(Right);
			}
			if (const auto* Value = std::get_if<double>(&Left))
			{
				// Numerical equality loses signed-zero changes and treats an unchanged
				// NaN as different. Ref/out write-back must preserve the actual bits.
				return std::memcmp(Value, &std::get<double>(Right), sizeof(*Value)) == 0;
			}
			if (const auto* Value = std::get_if<float>(&Left))
			{
				return std::memcmp(Value, &std::get<float>(Right), sizeof(*Value)) == 0;
			}
			if (const auto* Value = std::get_if<std::int8_t>(&Left))
			{
				return *Value == std::get<std::int8_t>(Right);
			}
			if (const auto* Value = std::get_if<std::uint8_t>(&Left))
			{
				return *Value == std::get<std::uint8_t>(Right);
			}
			if (const auto* Value = std::get_if<std::int16_t>(&Left))
			{
				return *Value == std::get<std::int16_t>(Right);
			}
			if (const auto* Value = std::get_if<std::uint16_t>(&Left))
			{
				return *Value == std::get<std::uint16_t>(Right);
			}
			if (const auto* Value = std::get_if<std::string>(&Left))
			{
				return *Value == std::get<std::string>(Right);
			}
			if (const auto* Value = std::get_if<bool>(&Left))
			{
				return *Value == std::get<bool>(Right);
			}
			if (const auto* Value = std::get_if<std::uint32_t>(&Left))
			{
				return *Value == std::get<std::uint32_t>(Right);
			}
			if (const auto* Value = std::get_if<std::uint64_t>(&Left))
			{
				return *Value == std::get<std::uint64_t>(Right);
			}
			if (const auto* Value = std::get_if<std::u16string>(&Left))
			{
				return *Value == std::get<std::u16string>(Right);
			}
			if (const auto* Value = std::get_if<FEnumValue>(&Left))
			{
				return *Value == std::get<FEnumValue>(Right);
			}
			if (const auto* Value = std::get_if<FNativeArray>(&Left))
			{
				const auto& Other = std::get<FNativeArray>(Right);
				if (Value->TypeName() != Other.TypeName() || Value->ElementTypeName() != Other.ElementTypeName() || Value->Length() != Other.Length() || Value->Storage().Elements.size() != Other.Storage().Elements.size())
				{
					return false;
				}
				for (std::size_t Index = 0; Index < Value->Storage().Elements.size(); ++Index)
				{
					if (!ValuesEquivalentForWriteBack(
					        Value->Storage().Elements[Index],
					        Other.Storage().Elements[Index],
					        Depth + 1))
					{
						return false;
					}
				}
				return true;
			}
			if (const auto* Value = std::get_if<FObjectHandle>(&Left))
			{
				return *Value == std::get<FObjectHandle>(Right);
			}
			if (const auto* Value = std::get_if<FDynamicArrayHandle>(&Left))
			{
				return *Value == std::get<FDynamicArrayHandle>(Right);
			}
			if (const auto* Value = std::get_if<FMapHandle>(&Left))
			{
				return *Value == std::get<FMapHandle>(Right);
			}
			if (const auto* Value = std::get_if<FSetHandle>(&Left))
			{
				return *Value == std::get<FSetHandle>(Right);
			}
			if (const auto* Value = std::get_if<FQueueHandle>(&Left))
			{
				return *Value == std::get<FQueueHandle>(Right);
			}
			if (const auto* Value = std::get_if<FStackHandle>(&Left))
			{
				return *Value == std::get<FStackHandle>(Right);
			}
			if (const auto* Value = std::get_if<FOrderedMapHandle>(&Left))
			{
				return *Value == std::get<FOrderedMapHandle>(Right);
			}
			if (const auto* Value = std::get_if<FDequeHandle>(&Left))
			{
				return *Value == std::get<FDequeHandle>(Right);
			}
			if (const auto* Value = std::get_if<FChannelHandle>(&Left))
			{
				return *Value == std::get<FChannelHandle>(Right);
			}
			if (const auto* Value = std::get_if<FFunctionHandle>(&Left))
			{
				return *Value == std::get<FFunctionHandle>(Right);
			}
			if (const auto* Value = std::get_if<FHostObjectHandle>(&Left))
			{
				return *Value == std::get<FHostObjectHandle>(Right);
			}
			return false;
		}

		template <typename TMap, typename TKey>
		void InsertOrderedEntry(
		    TMap& Entries,
		    std::vector<TKey>& Order,
		    TKey Key,
		    const FValue& Value)
		{
			const auto [It, bInserted] = Entries.emplace(Key, Value);
			if (bInserted)
			{
				Order.push_back(std::move(Key));
			}
			else
			{
				It->second = Value;
			}
		}

		bool InsertMapEntry(FMap& Map, const FValue& Key, const FValue& Value)
		{
			if (Map.KeyTypeName == "char")
			{
				const auto* const KeyI8 = std::get_if<std::int8_t>(&Key);
				if (!KeyI8)
				{
					return false;
				}
				Map.IntEntries[static_cast<std::int32_t>(*KeyI8)] = Value;
			}
			else if (const auto* KeyI32 = std::get_if<std::int32_t>(&Key))
			{
				Map.IntEntries[*KeyI32] = Value;
			}
			else if (const auto* KeyU32 = std::get_if<std::uint32_t>(&Key))
			{
				Map.UIntEntries[*KeyU32] = Value;
			}
			else if (const auto* KeyI64 = std::get_if<std::int64_t>(&Key))
			{
				Map.Int64Entries[*KeyI64] = Value;
			}
			else if (const auto* KeyU64 = std::get_if<std::uint64_t>(&Key))
			{
				Map.UInt64Entries[*KeyU64] = Value;
			}
			else if (const auto* KeyString = std::get_if<std::string>(&Key))
			{
				Map.StrEntries[*KeyString] = Value;
			}
			else if (const auto* KeyWString = std::get_if<std::u16string>(&Key))
			{
				Map.WStrEntries[*KeyWString] = Value;
			}
			else if (const auto* KeyEnum = std::get_if<FEnumValue>(&Key))
			{
				Map.EnumEntries[*KeyEnum] = Value;
			}
			else if (const auto* KeyHost = std::get_if<FHostObjectHandle>(&Key); KeyHost && *KeyHost)
			{
				Map.HostEntries[*KeyHost] = Value;
			}
			else
			{
				return false;
			}
			return true;
		}

		bool InsertOrderedMapEntry(
		    FOrderedMap& Map,
		    const FValue& Key,
		    const FValue& Value)
		{
			if (Map.KeyTypeName == "char")
			{
				const auto* const KeyI8 = std::get_if<std::int8_t>(&Key);
				if (!KeyI8)
				{
					return false;
				}
				const std::int32_t StoredKey = *KeyI8;
				InsertOrderedEntry(Map.IntEntries, Map.IntKeyOrder, StoredKey, Value);
			}
			else if (const auto* KeyI32 = std::get_if<std::int32_t>(&Key))
			{
				InsertOrderedEntry(Map.IntEntries, Map.IntKeyOrder, *KeyI32, Value);
			}
			else if (const auto* KeyU32 = std::get_if<std::uint32_t>(&Key))
			{
				InsertOrderedEntry(Map.UIntEntries, Map.UIntKeyOrder, *KeyU32, Value);
			}
			else if (const auto* KeyI64 = std::get_if<std::int64_t>(&Key))
			{
				InsertOrderedEntry(Map.Int64Entries, Map.Int64KeyOrder, *KeyI64, Value);
			}
			else if (const auto* KeyU64 = std::get_if<std::uint64_t>(&Key))
			{
				InsertOrderedEntry(Map.UInt64Entries, Map.UInt64KeyOrder, *KeyU64, Value);
			}
			else if (const auto* KeyString = std::get_if<std::string>(&Key))
			{
				InsertOrderedEntry(Map.StrEntries, Map.StrKeyOrder, *KeyString, Value);
			}
			else if (const auto* KeyWString = std::get_if<std::u16string>(&Key))
			{
				InsertOrderedEntry(Map.WStrEntries, Map.WStrKeyOrder, *KeyWString, Value);
			}
			else if (const auto* KeyEnum = std::get_if<FEnumValue>(&Key))
			{
				InsertOrderedEntry(Map.EnumEntries, Map.EnumKeyOrder, *KeyEnum, Value);
			}
			else if (const auto* KeyHost = std::get_if<FHostObjectHandle>(&Key); KeyHost && *KeyHost)
			{
				InsertOrderedEntry(Map.HostEntries, Map.HostKeyOrder, *KeyHost, Value);
			}
			else
			{
				return false;
			}
			return true;
		}

		bool InsertSetElement(FSet& Set, const FValue& Element)
		{
			if (Set.ElementTypeName == "char")
			{
				const auto* const ElementI8 = std::get_if<std::int8_t>(&Element);
				if (!ElementI8)
				{
					return false;
				}
				Set.IntElements.insert(static_cast<std::int32_t>(*ElementI8));
			}
			else if (const auto* ElementI32 = std::get_if<std::int32_t>(&Element))
			{
				Set.IntElements.insert(*ElementI32);
			}
			else if (const auto* ElementU32 = std::get_if<std::uint32_t>(&Element))
			{
				Set.UIntElements.insert(*ElementU32);
			}
			else if (const auto* ElementI64 = std::get_if<std::int64_t>(&Element))
			{
				Set.Int64Elements.insert(*ElementI64);
			}
			else if (const auto* ElementU64 = std::get_if<std::uint64_t>(&Element))
			{
				Set.UInt64Elements.insert(*ElementU64);
			}
			else if (const auto* ElementString = std::get_if<std::string>(&Element))
			{
				Set.StrElements.insert(*ElementString);
			}
			else if (const auto* ElementWString = std::get_if<std::u16string>(&Element))
			{
				Set.WStrElements.insert(*ElementWString);
			}
			else if (const auto* ElementEnum = std::get_if<FEnumValue>(&Element))
			{
				Set.EnumElements.insert(*ElementEnum);
			}
			else if (const auto* ElementHost = std::get_if<FHostObjectHandle>(&Element);
			    ElementHost && *ElementHost)
			{
				Set.HostElements.insert(*ElementHost);
			}
			else
			{
				return false;
			}
			return true;
		}

		template <typename TMap>
		const FValue* FindBoxedMapValue(const TMap& Map, const FValue& Key)
		{
			if (Map.KeyTypeName == "char")
			{
				const auto* const Typed = std::get_if<std::int8_t>(&Key);
				if (!Typed)
				{
					return nullptr;
				}
				const auto It = Map.IntEntries.find(static_cast<std::int32_t>(*Typed));
				return It == Map.IntEntries.end() ? nullptr : &It->second;
			}
			#define VS_FIND_MAP_VALUE(variant_type, member_name) \
	if (const auto* Typed = std::get_if<variant_type>(&Key)) \
	{ \
		const auto It = Map.member_name.find(*Typed); \
		return It == Map.member_name.end() ? nullptr : &It->second; \
	}
			VS_FIND_MAP_VALUE(std::int32_t, IntEntries)
			VS_FIND_MAP_VALUE(std::uint32_t, UIntEntries)
			VS_FIND_MAP_VALUE(std::int64_t, Int64Entries)
			VS_FIND_MAP_VALUE(std::uint64_t, UInt64Entries)
			VS_FIND_MAP_VALUE(std::string, StrEntries)
			VS_FIND_MAP_VALUE(std::u16string, WStrEntries)
			VS_FIND_MAP_VALUE(FEnumValue, EnumEntries)
			VS_FIND_MAP_VALUE(FHostObjectHandle, HostEntries)
			#undef VS_FIND_MAP_VALUE
			return nullptr;
		}

		template <typename TMap>
		FValue* FindBoxedMapValue(TMap& Map, const FValue& Key)
		{
			if (Map.KeyTypeName == "char")
			{
				const auto* const Typed = std::get_if<std::int8_t>(&Key);
				if (!Typed)
				{
					return nullptr;
				}
				auto It = Map.IntEntries.find(static_cast<std::int32_t>(*Typed));
				return It == Map.IntEntries.end() ? nullptr : &It->second;
			}
			#define VS_FIND_MUTABLE_MAP_VALUE(variant_type, member_name) \
	if (const auto* Typed = std::get_if<variant_type>(&Key)) \
	{ \
		auto It = Map.member_name.find(*Typed); \
		return It == Map.member_name.end() ? nullptr : &It->second; \
	}
			VS_FIND_MUTABLE_MAP_VALUE(std::int32_t, IntEntries)
			VS_FIND_MUTABLE_MAP_VALUE(std::uint32_t, UIntEntries)
			VS_FIND_MUTABLE_MAP_VALUE(std::int64_t, Int64Entries)
			VS_FIND_MUTABLE_MAP_VALUE(std::uint64_t, UInt64Entries)
			VS_FIND_MUTABLE_MAP_VALUE(std::string, StrEntries)
			VS_FIND_MUTABLE_MAP_VALUE(std::u16string, WStrEntries)
			VS_FIND_MUTABLE_MAP_VALUE(FEnumValue, EnumEntries)
			VS_FIND_MUTABLE_MAP_VALUE(FHostObjectHandle, HostEntries)
			#undef VS_FIND_MUTABLE_MAP_VALUE
			return nullptr;
		}

		template <typename TMap, typename TInsert>
		bool AppendMultiValue(
		    TMap& Map,
		    const FValue& Key,
		    const FValue& Mapped,
		    const std::string& ElementType,
		    TInsert&& Insert)
		{
			if (FValue* const Existing = FindBoxedMapValue(Map, Key))
			{
				auto* const Array = std::get_if<FDynamicArrayHandle>(Existing);
				if (!Array || !*Array || !NativeElementTypeNamesEqual((*Array)->ElementTypeName, ElementType))
				{
					return false;
				}
				(*Array)->Elements.push_back(Mapped);
				return true;
			}

			auto Array = std::make_shared<FDynamicArray>();
			Array->ElementTypeName = ElementType;
			Array->TypeName = "array<" + ElementType + ">";
			Array->Elements.push_back(Mapped);
			return Insert(Map, Key, FValue{std::move(Array)});
		}

		std::size_t BoxedMapCount(const FMap& Map)
		{
			return Map.IntEntries.size() + Map.UIntEntries.size() + Map.Int64Entries.size() + Map.UInt64Entries.size() + Map.StrEntries.size() + Map.WStrEntries.size() + Map.EnumEntries.size() + Map.HostEntries.size();
		}

		std::size_t BoxedMapCount(const FOrderedMap& Map)
		{
			return Map.IntEntries.size() + Map.UIntEntries.size() + Map.Int64Entries.size() + Map.UInt64Entries.size() + Map.StrEntries.size() + Map.WStrEntries.size() + Map.EnumEntries.size() + Map.HostEntries.size();
		}

		std::size_t SetCount(const FSet& Set)
		{
			return Set.IntElements.size() + Set.UIntElements.size() + Set.Int64Elements.size() + Set.UInt64Elements.size() + Set.StrElements.size() + Set.WStrElements.size() + Set.EnumElements.size() + Set.HostElements.size();
		}

		bool SetContains(const FSet& Set, const FValue& Element, bool& bSupported)
		{
			bSupported = true;
			if (Set.ElementTypeName == "char")
			{
				const auto* const Typed = std::get_if<std::int8_t>(&Element);
				if (!Typed)
				{
					bSupported = false;
					return false;
				}
				return Set.IntElements.find(static_cast<std::int32_t>(*Typed)) != Set.IntElements.end();
			}
			#define VS_SET_CONTAINS(variant_type, member_name) \
				if (const auto* Typed = std::get_if<variant_type>(&Element)) \
				{ \
					return Set.member_name.find(*Typed) != Set.member_name.end(); \
				}
			VS_SET_CONTAINS(std::int32_t, IntElements)
			VS_SET_CONTAINS(std::uint32_t, UIntElements)
			VS_SET_CONTAINS(std::int64_t, Int64Elements)
			VS_SET_CONTAINS(std::uint64_t, UInt64Elements)
			VS_SET_CONTAINS(std::string, StrElements)
			VS_SET_CONTAINS(std::u16string, WStrElements)
			VS_SET_CONTAINS(FEnumValue, EnumElements)
			VS_SET_CONTAINS(FHostObjectHandle, HostElements)
			#undef VS_SET_CONTAINS
			bSupported = false;
			return false;
		}

		std::size_t DynamicArrayElementCount(const FValue& Value, bool& bValid)
		{
			const auto* const Array = std::get_if<FDynamicArrayHandle>(&Value);
			if (!Array || !*Array)
			{
				bValid = false;
				return 0;
			}
			return (*Array)->HasScalarElements()
			    ? (*Array)->ScalarSize()
			    : (*Array)->Elements.size();
		}

		template <typename TEntries>
		std::size_t MultiSetEntryOccurrenceCount(const TEntries& Entries, bool& bValid)
		{
			std::size_t Count = 0;
			for (const auto& [Key, Occurrences] : Entries)
			{
				Count += DynamicArrayElementCount(Occurrences, bValid);
			}
			return Count;
		}

		template <typename TMap>
		std::size_t MultiSetMapOccurrenceCount(const TMap& Map, bool& bValid)
		{
			return MultiSetEntryOccurrenceCount(Map.IntEntries, bValid) + MultiSetEntryOccurrenceCount(Map.UIntEntries, bValid) + MultiSetEntryOccurrenceCount(Map.Int64Entries, bValid) + MultiSetEntryOccurrenceCount(Map.UInt64Entries, bValid) + MultiSetEntryOccurrenceCount(Map.StrEntries, bValid) + MultiSetEntryOccurrenceCount(Map.WStrEntries, bValid) + MultiSetEntryOccurrenceCount(Map.EnumEntries, bValid) + MultiSetEntryOccurrenceCount(Map.HostEntries, bValid);
		}

		template <typename TEntries>
		void AppendMultiSetElements(const TEntries& Entries, std::vector<FValue>& Out)
		{
			for (const auto& [Key, Occurrences] : Entries)
			{
				const auto* const Array = std::get_if<FDynamicArrayHandle>(&Occurrences);
				if (!Array || !*Array)
				{
					continue;
				}
				for (std::size_t Index = 0; Index < (*Array)->Elements.size(); ++Index)
				{
					Out.emplace_back(Key);
				}
			}
		}

		template <typename TEntries, typename TOrder>
		void AppendOrderedMultiSetElements(
		    const TEntries& Entries,
		    const TOrder& Order,
		    std::vector<FValue>& Out)
		{
			for (const auto& Key : Order)
			{
				const auto It = Entries.find(Key);
				if (It == Entries.end())
				{
					continue;
				}
				const auto* const Array = std::get_if<FDynamicArrayHandle>(&It->second);
				if (!Array || !*Array)
				{
					continue;
				}
				for (std::size_t Index = 0; Index < (*Array)->Elements.size(); ++Index)
				{
					Out.emplace_back(Key);
				}
			}
		}

		using FMapEntrySnapshot = std::vector<std::pair<FValue, FValue>>;

		template <typename TMap>
		void AppendMapEntries(const TMap& Entries, FMapEntrySnapshot& Out)
		{
			for (const auto& [Key, Value] : Entries)
			{
				Out.emplace_back(FValue{Key}, FValue{Value});
			}
		}

		void AppendSigned32MapEntries(
		    const std::unordered_map<std::int32_t, FValue>& Entries,
		    const bool bCharacterKeys,
		    FMapEntrySnapshot& Out)
		{
			for (const auto& [Key, Value] : Entries)
			{
				Out.emplace_back(
				    bCharacterKeys
				        ? FValue{static_cast<std::int8_t>(Key)}
				        : FValue{Key},
				    Value);
			}
		}

		template <typename TMap, typename TOrder>
		void AppendOrderedMapEntries(
		    const TMap& Entries,
		    const TOrder& Order,
		    FMapEntrySnapshot& Out)
		{
			for (const auto& Key : Order)
			{
				const auto It = Entries.find(Key);
				if (It != Entries.end())
				{
					Out.emplace_back(FValue{Key}, It->second);
				}
			}
		}

		void AppendSigned32OrderedMapEntries(
		    const std::unordered_map<std::int32_t, FValue>& Entries,
		    const std::vector<std::int32_t>& Order,
		    const bool bCharacterKeys,
		    FMapEntrySnapshot& Out)
		{
			for (const std::int32_t Key : Order)
			{
				const auto It = Entries.find(Key);
				if (It == Entries.end())
				{
					continue;
				}
				Out.emplace_back(
				    bCharacterKeys
				        ? FValue{static_cast<std::int8_t>(Key)}
				        : FValue{Key},
				    It->second);
			}
		}

		template <typename TSet>
		void AppendSetElements(const TSet& Elements, std::vector<FValue>& Out)
		{
			for (const auto& Element : Elements)
			{
				Out.emplace_back(Element);
			}
		}

		void AppendSigned32SetElements(
		    const std::unordered_set<std::int32_t>& Elements,
		    const bool bCharacterElements,
		    std::vector<FValue>& Out)
		{
			for (const std::int32_t Element : Elements)
			{
				Out.emplace_back(bCharacterElements
				        ? FValue{static_cast<std::int8_t>(Element)}
				        : FValue{Element});
			}
		}

		template <typename TEntries>
		void AppendMultiSetEntries(const TEntries& Entries, std::vector<FValue>& Out, bool& bValid)
		{
			for (const auto& [Key, Values] : Entries)
			{
				const auto* const Array = std::get_if<FDynamicArrayHandle>(&Values);
				if (!Array || !*Array)
				{
					bValid = false;
					return;
				}
				const std::size_t Count = DynamicArrayElementCount(Values, bValid);
				if (!bValid)
				{
					return;
				}
				Out.insert(Out.end(), Count, FValue{Key});
			}
		}

		template <typename TEntries, typename TOrder>
		void AppendOrderedMultiSetEntries(
		    const TEntries& Entries,
		    const TOrder& Order,
		    std::vector<FValue>& Out,
		    bool& bValid)
		{
			for (const auto& Key : Order)
			{
				const auto It = Entries.find(Key);
				if (It == Entries.end())
				{
					bValid = false;
					return;
				}
				const std::size_t Count = DynamicArrayElementCount(It->second, bValid);
				if (!bValid)
				{
					return;
				}
				Out.insert(Out.end(), Count, FValue{Key});
			}
		}

		bool AppendMultiSetSnapshot(const FMap& Set, std::vector<FValue>& Out)
		{
			bool bValid = true;
			if (Set.KeyTypeName == "char")
			{
				for (const auto& [Key, Values] : Set.IntEntries)
				{
					const std::size_t Count = DynamicArrayElementCount(Values, bValid);
					if (!bValid)
					{
						return false;
					}
					Out.insert(Out.end(), Count, FValue{static_cast<std::int8_t>(Key)});
				}
			}
			else
			{
				AppendMultiSetEntries(Set.IntEntries, Out, bValid);
			}
			AppendMultiSetEntries(Set.UIntEntries, Out, bValid);
			AppendMultiSetEntries(Set.Int64Entries, Out, bValid);
			AppendMultiSetEntries(Set.UInt64Entries, Out, bValid);
			AppendMultiSetEntries(Set.StrEntries, Out, bValid);
			AppendMultiSetEntries(Set.WStrEntries, Out, bValid);
			AppendMultiSetEntries(Set.EnumEntries, Out, bValid);
			AppendMultiSetEntries(Set.HostEntries, Out, bValid);
			return bValid;
		}

		bool AppendOrderedMultiSetSnapshot(const FOrderedMap& Set, std::vector<FValue>& Out)
		{
			bool bValid = true;
			if (Set.KeyTypeName == "char")
			{
				for (const std::int32_t Key : Set.IntKeyOrder)
				{
					const auto It = Set.IntEntries.find(Key);
					if (It == Set.IntEntries.end())
					{
						return false;
					}
					const std::size_t Count = DynamicArrayElementCount(It->second, bValid);
					if (!bValid)
					{
						return false;
					}
					Out.insert(Out.end(), Count, FValue{static_cast<std::int8_t>(Key)});
				}
			}
			else
			{
				AppendOrderedMultiSetEntries(
				    Set.IntEntries, Set.IntKeyOrder, Out, bValid);
			}
			AppendOrderedMultiSetEntries(Set.UIntEntries, Set.UIntKeyOrder, Out, bValid);
			AppendOrderedMultiSetEntries(Set.Int64Entries, Set.Int64KeyOrder, Out, bValid);
			AppendOrderedMultiSetEntries(Set.UInt64Entries, Set.UInt64KeyOrder, Out, bValid);
			AppendOrderedMultiSetEntries(Set.StrEntries, Set.StrKeyOrder, Out, bValid);
			AppendOrderedMultiSetEntries(Set.WStrEntries, Set.WStrKeyOrder, Out, bValid);
			AppendOrderedMultiSetEntries(Set.EnumEntries, Set.EnumKeyOrder, Out, bValid);
			AppendOrderedMultiSetEntries(Set.HostEntries, Set.HostKeyOrder, Out, bValid);
			return bValid;
		}
	}

	const FValue* ReadValue(const ::vs_interop_value* Value, FSessionState* ExpectedOwner)
	{
		if (!Value)
		{
			return nullptr;
		}
		SynchronizeValueOwnership(*Value);
		const auto ReadStored = [&]() -> const FValue*
		{
			if (!Value->Root.IsRegistered()) return Value->Storage ? &*Value->Storage : Value->BorrowedRead;
			const auto* Stored = Value->Root.Read();
			if (!Stored)
			{
				const auto Owner = Value->Root.Owner();
				throw FValueAccessFailure{Owner && !Owner->IsOwnerThread() ? VS_INTEROP_STATUS_WRONG_THREAD : VS_INTEROP_STATUS_ENGINE_ERROR};
			}
			return Stored;
		};
		const FValue* Stored = ReadStored();
		if (Stored && ExpectedOwner)
		{
			const auto ExpectedIdentity = ExpectedOwner->weak_from_this();
			const auto Owner = Value->Owner.lock();
			// Scalars and independent host values remain transferable. Language-owned
			// graphs retain their identity even during final shared-state destruction.
			if (HasOwnerIdentity(Value->Owner) && (Value->Owner.owner_before(ExpectedIdentity) || ExpectedIdentity.owner_before(Value->Owner)) && ValueNeedsSessionOwner(*Stored, ValueOwnerContext(*Value, Owner), Value->NativeContext))
			{
				throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR,
				    "VeroScriptInterop value belongs to a foreign session"};
			}
			// Bind shared C storage before execution: ordinary parameter/global
			// assignment can mutate it even when execution later fails or exits.
			const bool SameSession = !Value->Owner.owner_before(ExpectedIdentity) && !ExpectedIdentity.owner_before(Value->Owner);
			BindValueOwnership(*Stored, *ExpectedOwner, SameSession ? Value->NativeContext : nullptr);
			SynchronizeValueOwnership(*Value);
			Stored = ReadStored();
		}
		return UnwrapClosureReferenceCell(Stored);
	}

	bool IsWritableValue(const ::vs_interop_value* Value)
	{
		return Value && (!Value->BorrowedRead || Value->BorrowedWrite);
	}

	void AssignValue(::vs_interop_value& Value, FValue Data, const ::vs_interop_value* Source)
	{
		SynchronizeValueOwnership(Value);
		if (Value.Root.IsRegistered())
		{
			(void)ReadValue(&Value);
		}
		const auto PreviousOwner = Value.Owner.lock();
		const FValue* const Visible = UnwrapClosureReferenceCell(&Data);
		if (!Visible)
		{
			// A cyclic internal cell can only come from malformed core/host data.
			// Never copy its private handle across the ABI boundary.
			Data = std::int32_t{0};
		}
		else if (Visible != &Data)
		{
			Data = *Visible;
		}
		auto OriginIdentity = Source ? Source->Owner : std::weak_ptr<FSessionState>{};
		if (!HasOwnerIdentity(OriginIdentity) && ActiveValueSession)
		{
			OriginIdentity = ActiveValueSession->weak_from_this();
		}
		const auto Origin = OriginIdentity.lock();
		auto Owner = Source ? Origin : PreviousOwner;
		if (!Owner)
		{
			Owner = Origin;
		}
		const auto* OriginContext = Source ? ValueOwnerContext(*Source, Origin) : Origin.get();
		const auto* Native = Source ? Source->NativeContext : nullptr;
		if (!Native && Value.NativeContext && (!Source || !HasOwnerIdentity(Source->Owner)) && (!HasOwnerIdentity(OriginIdentity) || (!Value.Owner.owner_before(OriginIdentity) && !OriginIdentity.owner_before(Value.Owner))))
		{
			Native = Value.NativeContext;
		}
		const bool NeedsOwner = ValueNeedsSessionOwner(Data,
		    OriginContext ? OriginContext : Owner ? Owner.get()
		                                          : ActiveValueSession,
		    Native);
		if (HasOwnerIdentity(Value.Owner) && HasOwnerIdentity(OriginIdentity) && (Value.Owner.owner_before(OriginIdentity) || OriginIdentity.owner_before(Value.Owner)) && NeedsOwner)
		{
			throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR,
			    "VeroScriptInterop value belongs to a foreign session"};
		}
		// Only external C wrappers create tracking sidecars. Private callback
		// return slots remain forwardable during replacement initialization.
		auto SharedOwnership = Value.bOwnedHandle ? GetValueSharedOwnership(Data) : nullptr;
		if (Owner)
		{
			BindValueOwnership(Data, *Owner, Native);
		}
		else
		{
			RejectUnownedModuleStorage(Data, Value.NativeContext);
		}
		Value.Utf16View.clear();
		Value.bUtf16ViewValid = false;
		if (Value.NativeContext && !Value.bOwnedHandle && !Value.BorrowedRead)
		{
			if (!Value.Storage)
			{
				Value.Storage.emplace(std::int32_t{0});
			}
			Value.NativeContext->AssignValue(*Value.Storage, std::move(Data));
			return;
		}
		if (Value.BorrowedRead)
		{
			if (Value.BorrowedWrite)
			{
				if (Value.NativeContext)
				{
					Value.NativeContext->AssignValue(*Value.BorrowedWrite, std::move(Data));
				}
				else
				{
					*Value.BorrowedWrite = std::move(Data);
				}
			}
			return;
		}
		if (NeedsOwner)
		{
			if (!Owner)
			{
				throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
			}
			if (Owner->OwnerThread != std::this_thread::get_id())
			{
				throw FValueAccessFailure{VS_INTEROP_STATUS_WRONG_THREAD};
			}
			FValueRoot Prepared;
			std::string Error;
			if (Value.bOwnedHandle)
			{
				if (!Owner->Session.CanExportModuleValues())
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
				}
				// A shared backing has one lease across aliases. Reuse it instead
				// of registering a separate module root for every C clone/getter.
				if (SharedOwnership)
				{
					::vs_interop_value Candidate;
					Candidate.SharedOwnership = SharedOwnership;
					Candidate.BorrowedRead = &Data;
					SynchronizeValueOwnership(Candidate);
					Prepared = std::move(Candidate.Root);
				}
				if (!Prepared.IsRegistered() && !Owner->Session.RetainValue(Data, Prepared, Error))
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
				}
			}
			else
			{
				// An interceptor return is borrowed for this callback, like a native
				// return above. Keep its graph until the VM consumes it without
				// publishing an external root during replacement initialization.
				if (!Owner->Session.ReserveValueResult(Prepared, Error) || !Prepared.Owner()->CommitReserved(Prepared, Data, Error))
				{
					throw FValueAccessFailure{VS_INTEROP_STATUS_ENGINE_ERROR};
				}
			}
			Value.Owner = Owner;
			Value.Root = std::move(Prepared);
			Value.Storage.reset();
		}
		else
		{
			if (Value.NativeContext && Value.Storage)
			{
				Value.NativeContext->AssignValue(*Value.Storage, std::move(Data));
			}
			else
			{
				Value.Storage = std::move(Data);
			}
			Value.Root.Reset();
			// An owned handle becomes independent with its replacement value. A
			// borrowed writable return slot keeps the callback's session identity.
			if (Value.bOwnedHandle)
			{
				Value.Owner.reset();
			}
		}
		Value.SharedOwnership = std::move(SharedOwnership);
		DrainAfterValuePublication(PreviousOwner);
	}

	void ResetValue(::vs_interop_value& Value)
	{
		SynchronizeValueOwnership(Value);
		if (Value.Root.IsRegistered())
		{
			(void)ReadValue(&Value);
		}
		const auto PreviousOwner = Value.Owner.lock();
		Value.Utf16View.clear();
		Value.bUtf16ViewValid = false;
		if (Value.BorrowedRead)
		{
			return;
		}
		Value.Storage.reset();
		Value.Root.Reset();
		Value.SharedOwnership.reset();
		if (Value.bOwnedHandle)
		{
			Value.Owner.reset();
		}
		DrainAfterValuePublication(PreviousOwner);
	}

	vs_interop_value_kind KindOf(const FValue& Value)
	{
		if (std::holds_alternative<FScriptVariantHandle>(Value)) return VS_INTEROP_VALUE_VARIANT;
		if (const auto* Object = std::get_if<FObjectHandle>(&Value); Object && !*Object)
		{
			return VS_INTEROP_VALUE_NULL;
		}
		if (const auto* HostObject = std::get_if<FHostObjectHandle>(&Value);
		    HostObject && !*HostObject)
		{
			return VS_INTEROP_VALUE_NULL;
		}
		if (std::holds_alternative<std::int32_t>(Value))
		{
			return VS_INTEROP_VALUE_INT32;
		}
		if (std::holds_alternative<std::int64_t>(Value))
		{
			return VS_INTEROP_VALUE_INT64;
		}
		if (std::holds_alternative<double>(Value))
		{
			return VS_INTEROP_VALUE_DOUBLE;
		}
		if (std::holds_alternative<std::string>(Value))
		{
			return VS_INTEROP_VALUE_STRING;
		}
		if (std::holds_alternative<bool>(Value))
		{
			return VS_INTEROP_VALUE_BOOL;
		}
		if (std::holds_alternative<FObjectHandle>(Value))
		{
			return VS_INTEROP_VALUE_OBJECT;
		}
		if (std::holds_alternative<FNativeArray>(Value))
		{
			return VS_INTEROP_VALUE_NATIVE_ARRAY;
		}
		if (std::holds_alternative<FDynamicArrayHandle>(Value))
		{
			return VS_INTEROP_VALUE_ARRAY;
		}
		if (std::holds_alternative<std::uint32_t>(Value))
		{
			return VS_INTEROP_VALUE_UINT32;
		}
		if (std::holds_alternative<std::uint64_t>(Value))
		{
			return VS_INTEROP_VALUE_UINT64;
		}
		if (std::holds_alternative<std::u16string>(Value))
		{
			return VS_INTEROP_VALUE_WSTRING;
		}
		if (const auto* Map = std::get_if<FMapHandle>(&Value); Map && *Map)
		{
			if (HasGenericPrefix((*Map)->TypeName, "multimap<"))
			{
				return VS_INTEROP_VALUE_MULTI_MAP;
			}
			if (HasGenericPrefix((*Map)->TypeName, "multiset<"))
			{
				return VS_INTEROP_VALUE_MULTI_SET;
			}
			return VS_INTEROP_VALUE_MAP;
		}
		if (std::holds_alternative<FSetHandle>(Value))
		{
			return VS_INTEROP_VALUE_SET;
		}
		if (const auto* Queue = std::get_if<FQueueHandle>(&Value); Queue && *Queue)
		{
			return HasGenericPrefix((*Queue)->TypeName, "priority_queue<")
			    ? VS_INTEROP_VALUE_PRIORITY_QUEUE
			    : VS_INTEROP_VALUE_QUEUE;
		}
		if (std::holds_alternative<FStackHandle>(Value))
		{
			return VS_INTEROP_VALUE_STACK;
		}
		if (const auto* Map = std::get_if<FOrderedMapHandle>(&Value); Map && *Map)
		{
			if (HasGenericPrefix((*Map)->TypeName, "ordered_multimap<"))
			{
				return VS_INTEROP_VALUE_ORDERED_MULTI_MAP;
			}
			if (HasGenericPrefix((*Map)->TypeName, "ordered_multiset<"))
			{
				return VS_INTEROP_VALUE_ORDERED_MULTI_SET;
			}
			return VS_INTEROP_VALUE_ORDERED_MAP;
		}
		if (std::holds_alternative<FDequeHandle>(Value))
		{
			return VS_INTEROP_VALUE_DEQUE;
		}
		if (std::holds_alternative<FChannelHandle>(Value))
		{
			return VS_INTEROP_VALUE_CHANNEL;
		}
		if (std::holds_alternative<FFunctionHandle>(Value))
		{
			return VS_INTEROP_VALUE_FUNCTION;
		}
		if (std::holds_alternative<FEnumValue>(Value))
		{
			return VS_INTEROP_VALUE_ENUM;
		}
		if (std::holds_alternative<FHostObjectHandle>(Value))
		{
			return VS_INTEROP_VALUE_HOST_OBJECT;
		}
		if (std::holds_alternative<float>(Value))
		{
			return VS_INTEROP_VALUE_FLOAT;
		}
		if (std::holds_alternative<std::int8_t>(Value))
		{
			return VS_INTEROP_VALUE_INT8;
		}
		if (std::holds_alternative<std::uint8_t>(Value))
		{
			return VS_INTEROP_VALUE_UINT8;
		}
		if (std::holds_alternative<std::int16_t>(Value))
		{
			return VS_INTEROP_VALUE_INT16;
		}
		if (std::holds_alternative<std::uint16_t>(Value))
		{
			return VS_INTEROP_VALUE_UINT16;
		}
		return VS_INTEROP_VALUE_INVALID;
	}

	vs_interop_status BuildArguments(
	    ::vs_interop_value* const* Arguments,
	    const std::uint64_t ArgumentCount,
	    std::vector<FValue>& OutArguments,
	    const bool bRequireWritable,
	    FSessionState* ExpectedOwner)
	{
		if ((ArgumentCount != 0 && !Arguments) || ArgumentCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		OutArguments.clear();
		OutArguments.reserve(static_cast<std::size_t>(ArgumentCount));
		for (std::uint64_t Index = 0; Index < ArgumentCount; ++Index)
		{
			if (bRequireWritable && !IsWritableValue(Arguments[Index]))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			const FValue* Value = ReadValue(Arguments[Index], ExpectedOwner);
			if (!Value)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			OutArguments.push_back(*Value);
		}
		return VS_INTEROP_STATUS_OK;
	}

	vs_interop_status CommitArguments(
	    ::vs_interop_value* const* Arguments,
	    const std::uint64_t ArgumentCount,
	    const std::vector<FValue>& Values)
	{
		if (ArgumentCount != Values.size())
		{
			return VS_INTEROP_STATUS_INTERNAL_ERROR;
		}
		for (std::uint64_t Index = 0; Index < ArgumentCount; ++Index)
		{
			if (!IsWritableValue(Arguments[Index]))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
		}
		for (std::uint64_t Index = 0; Index < ArgumentCount; ++Index)
		{
			const FValue& Next = Values[static_cast<std::size_t>(Index)];
			const FValue* Existing = ReadValue(Arguments[Index]);
			if (!Existing || !ValuesEquivalentForWriteBack(*Existing, Next))
			{
				AssignValue(*Arguments[Index], Next);
			}
		}
		return VS_INTEROP_STATUS_OK;
	}
}

extern "C"
{
	vs_interop_status VS_INTEROP_CALL vs_interop_value_create(vs_interop_value** out_value)
	{
		if (!out_value)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_value = nullptr;
		try
		{
			auto Value = std::make_unique<vs_interop_value>();
			Value->bOwnedHandle = true;
			*out_value = Value.release();
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_clone(
	    const vs_interop_value* value,
	    vs_interop_value** out_value)
	{
		if (!value || !out_value)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_value = nullptr;
		try
		{
			auto Copy = std::make_unique<vs_interop_value>();
			Copy->bOwnedHandle = true;
			if (const auto* Data = VeroScript::Interop::ReadValue(value))
			{
				VeroScript::Interop::AssignValue(*Copy, *Data, value);
			}
			*out_value = Copy.release();
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	void VS_INTEROP_CALL vs_interop_value_destroy(vs_interop_value* value)
	{
		if (!value || !value->bOwnedHandle)
		{
			return;
		}
		try
		{
			// Destruction must still release an invalid or cross-thread wrapper.
			// Synchronization only discovers a lazily bound alias for owner cleanup.
			try
			{
				VeroScript::Interop::SynchronizeValueOwnership(*value);
			}
			catch (...)
			{
			}
			const auto RootOwner = value->Root.Owner();
			auto Owner = RootOwner && RootOwner->IsOwnerThread() ? value->Owner.lock() : nullptr;
			delete value;
			if (Owner && std::this_thread::get_id() == Owner->OwnerThread && Owner->ActiveSessionCalls == 0)
			{
				const VeroScript::Interop::FScopedSessionActivity Activity(*Owner);
			}
		}
		catch (...)
		{
		}
	}

	vs_interop_value_kind VS_INTEROP_CALL vs_interop_value_get_kind(
	    const vs_interop_value* value)
	{
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			return Data ? VeroScript::Interop::KindOf(*Data) : VS_INTEROP_VALUE_INVALID;
		}
		catch (...)
		{
			return VS_INTEROP_VALUE_INVALID;
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_assign(
	    vs_interop_value* destination,
	    const vs_interop_value* source)
	{
		if (!destination || !source || destination == source)
		{
			return destination == source && destination != nullptr
			    ? VS_INTEROP_STATUS_OK
			    : VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Source = VeroScript::Interop::ReadValue(source);
			if (!VeroScript::Interop::IsWritableValue(destination) || !Source)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::AssignValue(*destination, *Source, source);
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_get_type_name(
	    const vs_interop_value* value,
	    vs_interop_string_view* out_type_name)
	{
		if (!out_type_name)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			if (!Data)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			std::string_view Name;
			if (std::holds_alternative<VeroScript::FScriptVariantHandle>(*Data))
			{
				*out_type_name = VeroScript::Interop::ViewOf("variant");
				return VS_INTEROP_STATUS_OK;
			}
			if (std::holds_alternative<std::int32_t>(*Data))
			{
				Name = "int";
			}
			else if (std::holds_alternative<std::int64_t>(*Data))
			{
				Name = "int64";
			}
			else if (std::holds_alternative<double>(*Data))
			{
				Name = "double";
			}
			else if (std::holds_alternative<std::string>(*Data))
			{
				Name = "string";
			}
			else if (std::holds_alternative<bool>(*Data))
			{
				Name = "bool";
			}
			else if (std::holds_alternative<std::uint32_t>(*Data))
			{
				Name = "unsigned int";
			}
			else if (std::holds_alternative<std::uint64_t>(*Data))
			{
				Name = "uint64";
			}
			else if (std::holds_alternative<float>(*Data))
			{
				Name = "float";
			}
			else if (std::holds_alternative<std::int8_t>(*Data))
			{
				Name = "char";
			}
			else if (std::holds_alternative<std::uint8_t>(*Data))
			{
				Name = "unsigned char";
			}
			else if (std::holds_alternative<std::int16_t>(*Data))
			{
				Name = "short";
			}
			else if (std::holds_alternative<std::uint16_t>(*Data))
			{
				Name = "unsigned short";
			}
			else if (std::holds_alternative<std::u16string>(*Data))
			{
				Name = "wstring";
			}
			else if (const auto* Object = std::get_if<VeroScript::FObjectHandle>(Data); Object && *Object)
			{
				Name = (*Object)->GetTypeName();
			}
			else if (const auto* NativeArray = std::get_if<VeroScript::FNativeArray>(Data))
			{
				Name = NativeArray->TypeName();
			}
			else if (const auto* DynamicArray = std::get_if<VeroScript::FDynamicArrayHandle>(Data); DynamicArray && *DynamicArray)
			{
				Name = (*DynamicArray)->TypeName;
			}
			else if (const auto* Map = std::get_if<VeroScript::FMapHandle>(Data); Map && *Map)
			{
				Name = (*Map)->TypeName;
			}
			else if (const auto* Set = std::get_if<VeroScript::FSetHandle>(Data); Set && *Set)
			{
				Name = (*Set)->TypeName;
			}
			else if (const auto* Queue = std::get_if<VeroScript::FQueueHandle>(Data); Queue && *Queue)
			{
				Name = (*Queue)->TypeName;
			}
			else if (const auto* Stack = std::get_if<VeroScript::FStackHandle>(Data); Stack && *Stack)
			{
				Name = (*Stack)->TypeName;
			}
			else if (const auto* OrderedMap = std::get_if<VeroScript::FOrderedMapHandle>(Data); OrderedMap && *OrderedMap)
			{
				Name = (*OrderedMap)->TypeName;
			}
			else if (const auto* Deque = std::get_if<VeroScript::FDequeHandle>(Data); Deque && *Deque)
			{
				Name = (*Deque)->TypeName;
			}
			else if (const auto* Channel = std::get_if<VeroScript::FChannelHandle>(Data); Channel && *Channel)
			{
				Name = (*Channel)->GetTypeNameView();
			}
			else if (const auto* Function = std::get_if<VeroScript::FFunctionHandle>(Data); Function && *Function)
			{
				Name = (*Function)->TypeName;
			}
			else if (const auto* Enum = std::get_if<VeroScript::FEnumValue>(Data))
			{
				Name = Enum->TypeName;
			}
			else if (const auto* HostObject = std::get_if<VeroScript::FHostObjectHandle>(Data); HostObject && *HostObject)
			{
				Name = (*HostObject)->TypeName;
			}
			else
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_type_name = VeroScript::Interop::ViewOf(Name);
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_reset(vs_interop_value* value)
	{
		if (!value || !value->bOwnedHandle)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			VeroScript::Interop::ResetValue(*value);
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_null(vs_interop_value* value)
	{
		try
		{
			const bool Slot = VeroScript::Interop::IsWritableValue(value);
			if (!Slot)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::AssignValue(*value, VeroScript::FObjectHandle{});
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_is_null(
	    const vs_interop_value* value,
	    uint8_t* out_is_null)
	{
		if (!out_is_null)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* const Slot = VeroScript::Interop::ReadValue(value);
			if (!Slot)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			const bool bNull =
			    (std::holds_alternative<VeroScript::FObjectHandle>(*Slot) && !std::get<VeroScript::FObjectHandle>(*Slot)) || (std::holds_alternative<VeroScript::FHostObjectHandle>(*Slot) && !std::get<VeroScript::FHostObjectHandle>(*Slot));
			*out_is_null = bNull ? 1 : 0;
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

#define VS_INTEROP_SCALAR_ACCESSORS(Name, CType, CppType) \
	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_##Name(vs_interop_value* value, CType data) \
	{ \
		try \
		{ \
			const bool Slot = VeroScript::Interop::IsWritableValue(value); \
			if (!Slot) \
			{ \
				return VS_INTEROP_STATUS_INVALID_ARGUMENT; \
			} \
			VeroScript::Interop::AssignValue(*value, static_cast<CppType>(data)); \
			return VS_INTEROP_STATUS_OK; \
		} \
		catch (const std::bad_alloc&) \
		{ \
			return VS_INTEROP_STATUS_OUT_OF_MEMORY; \
		} \
		catch (...) \
		{ \
			return VeroScript::Interop::TranslateException(nullptr); \
		} \
	} \
	vs_interop_status VS_INTEROP_CALL vs_interop_value_get_##Name(const vs_interop_value* value, CType* out_data) \
	{ \
		if (!out_data) \
		{ \
			return VS_INTEROP_STATUS_INVALID_ARGUMENT; \
		} \
		try \
		{ \
			const auto* Slot = VeroScript::Interop::ReadValue(value); \
			const auto* Typed = Slot ? std::get_if<CppType>(Slot) : nullptr; \
			if (!Typed) \
			{ \
				return VS_INTEROP_STATUS_TYPE_MISMATCH; \
			} \
			*out_data = static_cast<CType>(*Typed); \
			return VS_INTEROP_STATUS_OK; \
		} \
		catch (...) \
		{ \
			return VeroScript::Interop::TranslateException(nullptr); \
		} \
	}

	VS_INTEROP_SCALAR_ACCESSORS(int32, int32_t, std::int32_t)
	VS_INTEROP_SCALAR_ACCESSORS(uint32, uint32_t, std::uint32_t)
	VS_INTEROP_SCALAR_ACCESSORS(int64, int64_t, std::int64_t)
	VS_INTEROP_SCALAR_ACCESSORS(uint64, uint64_t, std::uint64_t)
	VS_INTEROP_SCALAR_ACCESSORS(double, double, double)
	VS_INTEROP_SCALAR_ACCESSORS(float, float, float)
	VS_INTEROP_SCALAR_ACCESSORS(int8, int8_t, std::int8_t)
	VS_INTEROP_SCALAR_ACCESSORS(uint8, uint8_t, std::uint8_t)
	VS_INTEROP_SCALAR_ACCESSORS(int16, int16_t, std::int16_t)
	VS_INTEROP_SCALAR_ACCESSORS(uint16, uint16_t, std::uint16_t)

#undef VS_INTEROP_SCALAR_ACCESSORS

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_bool(
	    vs_interop_value* value,
	    const uint8_t data)
	{
		try
		{
			const bool Slot = VeroScript::Interop::IsWritableValue(value);
			if (!Slot)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::AssignValue(*value, data != 0);
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_get_bool(
	    const vs_interop_value* value,
	    uint8_t* out_data)
	{
		if (!out_data)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Slot = VeroScript::Interop::ReadValue(value);
			const auto* Typed = Slot ? std::get_if<bool>(Slot) : nullptr;
			if (!Typed)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_data = *Typed ? 1u : 0u;
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_string(
	    vs_interop_value* value,
	    const vs_interop_string_view data)
	{
		if (!VeroScript::Interop::IsValidString(data))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const bool Slot = VeroScript::Interop::IsWritableValue(value);
			if (!Slot)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::AssignValue(*value, VeroScript::Interop::CopyString(data));
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_get_string(
	    const vs_interop_value* value,
	    vs_interop_string_view* out_data)
	{
		if (!out_data)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Slot = VeroScript::Interop::ReadValue(value);
			const auto* Typed = Slot ? std::get_if<std::string>(Slot) : nullptr;
			if (!Typed)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_data = VeroScript::Interop::ViewOf(*Typed);
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_wstring(
	    vs_interop_value* value,
	    const uint16_t* data,
	    const uint64_t size)
	{
		if (!VeroScript::Interop::IsWritableValue(value) || (!data && size != 0) || size > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) || size > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			std::u16string Text;
			Text.reserve(static_cast<std::size_t>(size));
			for (std::uint64_t Index = 0; Index < size; ++Index)
			{
				Text.push_back(static_cast<char16_t>(data[Index]));
			}
			// The input may be this value's borrowed UTF-16 view. Copy it before
			// WriteValue invalidates that view, including on allocation failure.
			const bool Slot = VeroScript::Interop::IsWritableValue(value);
			if (!Slot)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::AssignValue(*value, std::move(Text));
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_get_wstring(
	    const vs_interop_value* value,
	    const uint16_t** out_data,
	    uint64_t* out_size)
	{
		if (!out_data || !out_size)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Slot = VeroScript::Interop::ReadValue(value);
			const auto* Typed = Slot ? std::get_if<std::u16string>(Slot) : nullptr;
			if (!Typed)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			if (!value->bUtf16ViewValid)
			{
				value->Utf16View.clear();
				value->Utf16View.reserve(Typed->size());
				for (const char16_t Unit : *Typed)
				{
					value->Utf16View.push_back(static_cast<std::uint16_t>(Unit));
				}
				value->bUtf16ViewValid = true;
			}
			*out_data = value->Utf16View.data();
			*out_size = static_cast<uint64_t>(value->Utf16View.size());
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_enum(
	    vs_interop_value* value,
	    const vs_interop_string_view type_name,
	    const int64_t data)
	{
		if (!VeroScript::Interop::IsValidText(type_name) || type_name.size == 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const bool Slot = VeroScript::Interop::IsWritableValue(value);
			if (!Slot)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::AssignValue(*value, VeroScript::FEnumValue{VeroScript::Interop::CopyString(type_name), data});
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_get_enum(
	    const vs_interop_value* value,
	    vs_interop_string_view* out_type_name,
	    int64_t* out_data)
	{
		if (!out_type_name || !out_data)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Slot = VeroScript::Interop::ReadValue(value);
			const auto* Typed = Slot ? std::get_if<VeroScript::FEnumValue>(Slot) : nullptr;
			if (!Typed)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_type_name = VeroScript::Interop::ViewOf(Typed->TypeName);
			*out_data = Typed->Value;
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_host_object(
	    vs_interop_value* value,
	    const uint32_t type_id,
	    const vs_interop_string_view type_name,
	    void* payload,
	    const vs_interop_host_object_destroy destroy)
	{
		return vs_interop_value_set_host_object_ex(
		    value, type_id, type_name, payload, destroy, nullptr, nullptr);
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_host_object_ex(
	    vs_interop_value* value,
	    const uint32_t type_id,
	    const vs_interop_string_view type_name,
	    void* payload,
	    const vs_interop_host_object_destroy destroy,
	    const vs_interop_host_object_is_type is_type,
	    void* const is_type_user_data)
	{
		if (!VeroScript::Interop::IsValidText(type_name) || type_name.size == 0)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			// Complete every fallible allocation before ownership is committed. The
			// derived shared control block also contains foreign destructor failures.
			auto Object = std::make_shared<VeroScript::Interop::FInteropHostObject>();
			Object->TypeId = type_id;
			Object->TypeName = VeroScript::Interop::CopyString(type_name);
			Object->Payload = payload;
			Object->Destroy = destroy;
			Object->IsType = is_type;
			Object->ForeignIsTypeUserData = is_type_user_data;
			if (is_type)
			{
				Object->IsTypeCallback = &VeroScript::Interop::InteropHostObjectIsType;
				Object->FHostObject::IsTypeUserData = Object.get();
			}
			const bool Slot = VeroScript::Interop::IsWritableValue(value);
			if (!Slot)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::AssignValue(*value, VeroScript::FHostObjectHandle(Object));
			Object->bOwnershipCommitted = true;
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_get_host_object(
	    const vs_interop_value* value,
	    uint32_t* out_type_id,
	    vs_interop_string_view* out_type_name,
	    void** out_payload)
	{
		if (!out_type_id || !out_type_name || !out_payload)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Slot = VeroScript::Interop::ReadValue(value);
			const auto* Typed = Slot ? std::get_if<VeroScript::FHostObjectHandle>(Slot) : nullptr;
			if (!Typed || !*Typed)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_type_id = (*Typed)->TypeId;
			*out_type_name = VeroScript::Interop::ViewOf((*Typed)->TypeName);
			*out_payload = (*Typed)->Payload;
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_create_array(
	    vs_interop_value* value,
	    const vs_interop_string_view element_type_name,
	    const vs_interop_value* const* elements,
	    const uint64_t element_count)
	{
		if (!VeroScript::Interop::IsWritableValue(value) || !VeroScript::Interop::IsValidText(element_type_name) || element_type_name.size == 0 || (element_count != 0 && !elements) || element_count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) || element_count > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			auto Array = std::make_shared<VeroScript::FDynamicArray>();
			Array->ElementTypeName = VeroScript::CanonicalizeNativeElementTypeName(
			    VeroScript::Interop::CopyString(element_type_name));
			if (!VeroScript::Interop::IsSaneValueTypeName(Array->ElementTypeName))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			Array->TypeName = "array<" + Array->ElementTypeName + ">";
			(void)Array->EnableScalarStorage();
			if (Array->HasScalarElements())
			{
				Array->ReserveScalar(static_cast<std::size_t>(element_count));
			}
			else
			{
				Array->Elements.reserve(static_cast<std::size_t>(element_count));
			}
			for (uint64_t Index = 0; Index < element_count; ++Index)
			{
				const auto* Element = VeroScript::Interop::ReadValue(elements[Index]);
				if (!Element)
				{
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}
				if (!VeroScript::Interop::ValueMatchesDeclaredType(
				        *Element, Array->ElementTypeName, elements[Index]))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				if (Array->HasScalarElements())
				{
					if (!Array->TryPushScalar(*Element))
					{
						return VS_INTEROP_STATUS_TYPE_MISMATCH;
					}
				}
				else
				{
					Array->Elements.push_back(*Element);
				}
			}
			VeroScript::Interop::AssignValue(*value, std::move(Array), VeroScript::Interop::FindValueOwner(elements, element_count));
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_create_sequence(
	    vs_interop_value* value,
	    const vs_interop_value_kind kind,
	    const vs_interop_string_view element_type_name,
	    const vs_interop_value* const* elements,
	    const uint64_t element_count)
	{
		if (kind == VS_INTEROP_VALUE_ARRAY)
		{
			return vs_interop_value_create_array(
			    value, element_type_name, elements, element_count);
		}
		if (!VeroScript::Interop::IsWritableValue(value) || (kind != VS_INTEROP_VALUE_QUEUE && kind != VS_INTEROP_VALUE_PRIORITY_QUEUE && kind != VS_INTEROP_VALUE_STACK && kind != VS_INTEROP_VALUE_DEQUE) || !VeroScript::Interop::IsValidText(element_type_name) || element_type_name.size == 0 || (element_count != 0 && !elements) || element_count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) || element_count > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const std::string ElementType = VeroScript::CanonicalizeNativeElementTypeName(
			    VeroScript::Interop::CopyString(element_type_name));
			if (!VeroScript::Interop::IsSaneValueTypeName(ElementType))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			std::vector<VeroScript::FValue> Prepared;
			Prepared.reserve(static_cast<std::size_t>(element_count));
			for (uint64_t Index = 0; Index < element_count; ++Index)
			{
				const auto* Element = VeroScript::Interop::ReadValue(elements[Index]);
				if (!Element)
				{
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}
				if (!VeroScript::Interop::ValueMatchesDeclaredType(*Element, ElementType, elements[Index]))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				Prepared.push_back(*Element);
			}
			if (kind == VS_INTEROP_VALUE_QUEUE || kind == VS_INTEROP_VALUE_PRIORITY_QUEUE)
			{
				if (kind == VS_INTEROP_VALUE_PRIORITY_QUEUE && !VeroScript::Interop::IsPriorityQueueElementType(ElementType))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				auto Sequence = std::make_shared<VeroScript::FQueue>();
				Sequence->ElementTypeName = ElementType;
				Sequence->TypeName = kind == VS_INTEROP_VALUE_PRIORITY_QUEUE
				    ? "priority_queue<" + ElementType + ">"
				    : "queue<" + ElementType + ">";
				Sequence->Elements.insert(Sequence->Elements.end(),
				    Prepared.begin(), Prepared.end());
				VeroScript::Interop::AssignValue(*value, std::move(Sequence), VeroScript::Interop::FindValueOwner(elements, element_count));
			}
			else if (kind == VS_INTEROP_VALUE_STACK)
			{
				auto Sequence = std::make_shared<VeroScript::FStack>();
				Sequence->ElementTypeName = ElementType;
				Sequence->TypeName = "stack<" + ElementType + ">";
				Sequence->Elements = std::move(Prepared);
				VeroScript::Interop::AssignValue(*value, std::move(Sequence), VeroScript::Interop::FindValueOwner(elements, element_count));
			}
			else
			{
				auto Sequence = std::make_shared<VeroScript::FDeque>();
				Sequence->ElementTypeName = ElementType;
				Sequence->TypeName = "deque<" + ElementType + ">";
				Sequence->Elements.insert(Sequence->Elements.end(),
				    Prepared.begin(), Prepared.end());
				VeroScript::Interop::AssignValue(*value, std::move(Sequence), VeroScript::Interop::FindValueOwner(elements, element_count));
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_sequence_count(
	    const vs_interop_value* value,
	    uint64_t* out_count)
	{
		if (!out_count)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			if (!Data)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			if (const auto* NativeArray = std::get_if<VeroScript::FNativeArray>(Data))
			{
				*out_count = NativeArray->Storage().Elements.size();
			}
			else if (const auto* DynamicArray = std::get_if<VeroScript::FDynamicArrayHandle>(Data); DynamicArray && *DynamicArray)
			{
				*out_count = (*DynamicArray)->HasScalarElements()
				    ? (*DynamicArray)->ScalarSize()
				    : (*DynamicArray)->Elements.size();
			}
			else if (const auto* Queue = std::get_if<VeroScript::FQueueHandle>(Data); Queue && *Queue)
			{
				*out_count = (*Queue)->Elements.size();
			}
			else if (const auto* Stack = std::get_if<VeroScript::FStackHandle>(Data); Stack && *Stack)
			{
				*out_count = (*Stack)->Elements.size();
			}
			else if (const auto* Deque = std::get_if<VeroScript::FDequeHandle>(Data); Deque && *Deque)
			{
				*out_count = (*Deque)->Elements.size();
			}
			else
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_sequence_get(
	    const vs_interop_value* value,
	    const uint64_t index,
	    vs_interop_value* out_element)
	{
		if (!VeroScript::Interop::IsWritableValue(out_element))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			if (!Data)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			const VeroScript::FValue* Element = nullptr;
			VeroScript::FValue ScalarElement;
			bool bSequence = false;
			if (const auto* NativeArray = std::get_if<VeroScript::FNativeArray>(Data);
			    NativeArray)
			{
				bSequence = true;
				if (index < NativeArray->Storage().Elements.size())
				{
					Element = &NativeArray->Storage().Elements[static_cast<std::size_t>(index)];
				}
			}
			else if (const auto* DynamicArray = std::get_if<VeroScript::FDynamicArrayHandle>(Data);
			    DynamicArray && *DynamicArray)
			{
				bSequence = true;
				if ((*DynamicArray)->HasScalarElements())
				{
					if (index < (*DynamicArray)->ScalarSize() && (*DynamicArray)->TryGetScalar(static_cast<std::size_t>(index), ScalarElement))
					{
						Element = &ScalarElement;
					}
				}
				else if (index < (*DynamicArray)->Elements.size())
				{
					Element = &(*DynamicArray)->Elements[static_cast<std::size_t>(index)];
				}
			}
			else if (const auto* Queue = std::get_if<VeroScript::FQueueHandle>(Data);
			    Queue && *Queue)
			{
				bSequence = true;
				if (index < (*Queue)->Elements.size())
				{
					Element = &(*Queue)->Elements[static_cast<std::size_t>(index)];
				}
			}
			else if (const auto* Stack = std::get_if<VeroScript::FStackHandle>(Data);
			    Stack && *Stack)
			{
				bSequence = true;
				if (index < (*Stack)->Elements.size())
				{
					Element = &(*Stack)->Elements[static_cast<std::size_t>(index)];
				}
			}
			else if (const auto* Deque = std::get_if<VeroScript::FDequeHandle>(Data);
			    Deque && *Deque)
			{
				bSequence = true;
				if (index < (*Deque)->Elements.size())
				{
					Element = &(*Deque)->Elements[static_cast<std::size_t>(index)];
				}
			}
			if (!bSequence)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			if (!Element)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			VeroScript::Interop::AssignValue(*out_element, *Element, value);
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_create_map(
	    vs_interop_value* value,
	    const vs_interop_string_view key_type_name,
	    const vs_interop_string_view value_type_name,
	    const uint8_t ordered,
	    const vs_interop_value* const* keys,
	    const vs_interop_value* const* values,
	    const uint64_t entry_count)
	{
		if (!VeroScript::Interop::IsWritableValue(value) || !VeroScript::Interop::IsValidText(key_type_name) || key_type_name.size == 0 || !VeroScript::Interop::IsValidText(value_type_name) || value_type_name.size == 0 || (entry_count != 0 && (!keys || !values)) || entry_count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) || entry_count > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const std::string KeyType = VeroScript::CanonicalizeNativeElementTypeName(
			    VeroScript::Interop::CopyString(key_type_name));
			const std::string ValueType = VeroScript::CanonicalizeNativeElementTypeName(
			    VeroScript::Interop::CopyString(value_type_name));
			if (!VeroScript::Interop::IsSaneMapOrSetKeyTypeName(KeyType) || !VeroScript::Interop::IsSaneValueTypeName(ValueType))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			if (ordered != 0)
			{
				auto Map = std::make_shared<VeroScript::FOrderedMap>();
				Map->KeyTypeName = KeyType;
				Map->ValueTypeName = ValueType;
				Map->TypeName = "ordered_map<" + KeyType + "," + ValueType + ">";
				for (uint64_t Index = 0; Index < entry_count; ++Index)
				{
					const auto* Key = VeroScript::Interop::ReadValue(keys[Index]);
					const auto* Mapped = VeroScript::Interop::ReadValue(values[Index]);
					if (!Key || !Mapped)
					{
						return VS_INTEROP_STATUS_INVALID_ARGUMENT;
					}
					if (!VeroScript::Interop::ValueMatchesDeclaredType(*Key, KeyType, keys[Index]) || !VeroScript::Interop::ValueMatchesDeclaredType(*Mapped, ValueType, values[Index]))
					{
						return VS_INTEROP_STATUS_TYPE_MISMATCH;
					}
					if (!VeroScript::Interop::InsertOrderedMapEntry(*Map, *Key, *Mapped))
					{
						return VS_INTEROP_STATUS_TYPE_MISMATCH;
					}
				}
				VeroScript::Interop::AssignValue(*value, std::move(Map), VeroScript::Interop::FindValueOwner(values, entry_count));
			}
			else
			{
				auto Map = std::make_shared<VeroScript::FMap>();
				Map->KeyTypeName = KeyType;
				Map->ValueTypeName = ValueType;
				Map->TypeName = "map<" + KeyType + "," + ValueType + ">";
				for (uint64_t Index = 0; Index < entry_count; ++Index)
				{
					const auto* Key = VeroScript::Interop::ReadValue(keys[Index]);
					const auto* Mapped = VeroScript::Interop::ReadValue(values[Index]);
					if (!Key || !Mapped)
					{
						return VS_INTEROP_STATUS_INVALID_ARGUMENT;
					}
					if (!VeroScript::Interop::ValueMatchesDeclaredType(*Key, KeyType, keys[Index]) || !VeroScript::Interop::ValueMatchesDeclaredType(*Mapped, ValueType, values[Index]))
					{
						return VS_INTEROP_STATUS_TYPE_MISMATCH;
					}
					if (!VeroScript::Interop::InsertMapEntry(*Map, *Key, *Mapped))
					{
						return VS_INTEROP_STATUS_TYPE_MISMATCH;
					}
				}
				VeroScript::Interop::AssignValue(*value, std::move(Map), VeroScript::Interop::FindValueOwner(values, entry_count));
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_create_map_kind(
	    vs_interop_value* value,
	    const vs_interop_value_kind kind,
	    const vs_interop_string_view key_type_name,
	    const vs_interop_string_view value_type_name,
	    const vs_interop_value* const* keys,
	    const vs_interop_value* const* values,
	    const uint64_t entry_count)
	{
		if (kind == VS_INTEROP_VALUE_MAP || kind == VS_INTEROP_VALUE_ORDERED_MAP)
		{
			return vs_interop_value_create_map(
			    value, key_type_name, value_type_name,
			    kind == VS_INTEROP_VALUE_ORDERED_MAP ? 1 : 0,
			    keys, values, entry_count);
		}
		if (!VeroScript::Interop::IsWritableValue(value) || (kind != VS_INTEROP_VALUE_MULTI_MAP && kind != VS_INTEROP_VALUE_ORDERED_MULTI_MAP) || !VeroScript::Interop::IsValidText(key_type_name) || key_type_name.size == 0 || !VeroScript::Interop::IsValidText(value_type_name) || value_type_name.size == 0 || (entry_count != 0 && (!keys || !values)) || entry_count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) || entry_count > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const std::string KeyType = VeroScript::CanonicalizeNativeElementTypeName(
			    VeroScript::Interop::CopyString(key_type_name));
			const std::string ValueType = VeroScript::CanonicalizeNativeElementTypeName(
			    VeroScript::Interop::CopyString(value_type_name));
			if (!VeroScript::Interop::IsSaneMapOrSetKeyTypeName(KeyType) || !VeroScript::Interop::IsSaneValueTypeName(ValueType))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			const std::string StoredValueType = "array<" + ValueType + ">";
			if (kind == VS_INTEROP_VALUE_MULTI_MAP)
			{
				auto Map = std::make_shared<VeroScript::FMap>();
				Map->KeyTypeName = KeyType;
				Map->ValueTypeName = StoredValueType;
				Map->TypeName = "multimap<" + KeyType + "," + ValueType + ">";
				for (uint64_t Index = 0; Index < entry_count; ++Index)
				{
					const auto* const Key = VeroScript::Interop::ReadValue(keys[Index]);
					const auto* const Mapped = VeroScript::Interop::ReadValue(values[Index]);
					if (!Key || !Mapped)
					{
						return VS_INTEROP_STATUS_INVALID_ARGUMENT;
					}
					if (!VeroScript::Interop::ValueMatchesDeclaredType(*Key, KeyType, keys[Index]) || !VeroScript::Interop::ValueMatchesDeclaredType(*Mapped, ValueType, values[Index]) || !VeroScript::Interop::AppendMultiValue(*Map, *Key, *Mapped, ValueType, [](VeroScript::FMap& Target, const VeroScript::FValue& InKey, const VeroScript::FValue& InValue)
					{
						return VeroScript::Interop::InsertMapEntry(Target, InKey, InValue);
					}))
					{
						return VS_INTEROP_STATUS_TYPE_MISMATCH;
					}
				}
				VeroScript::Interop::AssignValue(*value, std::move(Map), VeroScript::Interop::FindValueOwner(values, entry_count));
			}
			else
			{
				auto Map = std::make_shared<VeroScript::FOrderedMap>();
				Map->KeyTypeName = KeyType;
				Map->ValueTypeName = StoredValueType;
				Map->TypeName = "ordered_multimap<" + KeyType + "," + ValueType + ">";
				for (uint64_t Index = 0; Index < entry_count; ++Index)
				{
					const auto* const Key = VeroScript::Interop::ReadValue(keys[Index]);
					const auto* const Mapped = VeroScript::Interop::ReadValue(values[Index]);
					if (!Key || !Mapped)
					{
						return VS_INTEROP_STATUS_INVALID_ARGUMENT;
					}
					if (!VeroScript::Interop::ValueMatchesDeclaredType(*Key, KeyType, keys[Index]) || !VeroScript::Interop::ValueMatchesDeclaredType(*Mapped, ValueType, values[Index]) || !VeroScript::Interop::AppendMultiValue(*Map, *Key, *Mapped, ValueType, [](VeroScript::FOrderedMap& Target, const VeroScript::FValue& InKey, const VeroScript::FValue& InValue)
					{
						return VeroScript::Interop::InsertOrderedMapEntry(Target, InKey, InValue);
					}))
					{
						return VS_INTEROP_STATUS_TYPE_MISMATCH;
					}
				}
				VeroScript::Interop::AssignValue(*value, std::move(Map), VeroScript::Interop::FindValueOwner(values, entry_count));
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_map_count(
	    const vs_interop_value* value,
	    uint64_t* out_count)
	{
		if (!out_count)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			if (const auto* Map = Data ? std::get_if<VeroScript::FMapHandle>(Data) : nullptr;
			    Map && *Map)
			{
				const std::size_t ScalarCount = std::visit([](const auto& Entries) -> std::size_t
				{
					using T = std::decay_t<decltype(Entries)>;
					if constexpr (std::is_same_v<T, std::monostate>)
					{
						return 0;
					}
					else
					{
						return Entries.size();
					}
				}, (*Map)->ScalarEntries);
				*out_count = static_cast<uint64_t>(
				    ScalarCount + VeroScript::Interop::BoxedMapCount(**Map));
				return VS_INTEROP_STATUS_OK;
			}
			if (const auto* Map = Data ? std::get_if<VeroScript::FOrderedMapHandle>(Data) : nullptr;
			    Map && *Map)
			{
				*out_count = static_cast<uint64_t>(VeroScript::Interop::BoxedMapCount(**Map));
				return VS_INTEROP_STATUS_OK;
			}
			return VS_INTEROP_STATUS_TYPE_MISMATCH;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_map_get(
	    const vs_interop_value* value,
	    const vs_interop_value* key,
	    vs_interop_value* out_mapped_value,
	    uint8_t* out_found)
	{
		if (!VeroScript::Interop::IsWritableValue(out_mapped_value) || !out_found)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_found = 0;
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			const auto* Key = VeroScript::Interop::ReadValue(key);
			if (!Key)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			const VeroScript::FValue* Found = nullptr;
			VeroScript::FValue ScalarValue = std::int32_t{0};
			if (const auto* Map = Data ? std::get_if<VeroScript::FMapHandle>(Data) : nullptr;
			    Map && *Map)
			{
				if (!VeroScript::Interop::ValueMatchesDeclaredType(*Key, (*Map)->KeyTypeName, key))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				Found = VeroScript::Interop::FindBoxedMapValue(**Map, *Key);
				if (!Found)
				{
					VeroScript::FValue StorageKey = *Key;
					if ((*Map)->KeyTypeName == "char")
					{
						StorageKey = static_cast<std::int32_t>(
						    std::get<std::int8_t>(*Key));
					}
					std::visit([&](const auto& Entries)
					{
						using T = std::decay_t<decltype(Entries)>;
						if constexpr (!std::is_same_v<T, std::monostate>)
						{
							using TKey = typename T::key_type;
							if (const auto* TypedKey = std::get_if<TKey>(&StorageKey))
							{
								if (const auto* const TypedValue = Entries.Find(*TypedKey))
								{
									using TValue = typename T::mapped_type;
									if constexpr (std::is_same_v<
									                  TValue, VeroScript::FMap::FPrimitiveScalar>)
									{
										ScalarValue =
										    VeroScript::FMap::UnpackPrimitiveScalar(*TypedValue);
									}
									else
									{
										ScalarValue = *TypedValue;
									}
									Found = &ScalarValue;
								}
							}
						}
					}, (*Map)->ScalarEntries);
				}
			}
			else if (const auto* OrderedMap = Data
			        ? std::get_if<VeroScript::FOrderedMapHandle>(Data)
			        : nullptr;
			    OrderedMap && *OrderedMap)
			{
				if (!VeroScript::Interop::ValueMatchesDeclaredType(
				        *Key, (*OrderedMap)->KeyTypeName, key))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				Found = VeroScript::Interop::FindBoxedMapValue(**OrderedMap, *Key);
			}
			else
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			if (Found)
			{
				VeroScript::Interop::AssignValue(*out_mapped_value, *Found, value);
				*out_found = 1;
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_map_enumerate(
	    const vs_interop_value* value,
	    const vs_interop_map_entry_visitor visitor,
	    void* user_data)
	{
		if (!visitor)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			const auto SourceOwner = value ? value->Owner : std::weak_ptr<VeroScript::Interop::FSessionState>{};
			const auto Owner = SourceOwner.lock();
			const auto BorrowedOwner = value ? VeroScript::Interop::ValueOwnerContext(*value, Owner) : nullptr;
			const auto NativeContext = value ? value->NativeContext : nullptr;
			std::optional<VeroScript::Interop::FScopedSessionActivity> Activity;
			// A visitor may retire or destroy the source wrapper. Keep its registered
			// graph alive until all snapshot views are gone, then drain at a safe point.
			const auto SourceRoot = value ? value->Root : VeroScript::FValueRoot{};
			const auto SourceOwnership = value ? value->SharedOwnership : nullptr;
			if (Owner && Owner->ActiveSessionCalls == 0)
			{
				Activity.emplace(*Owner);
			}
			VeroScript::Interop::FMapEntrySnapshot Snapshot;
			if (const auto* UnorderedMap = Data
			        ? std::get_if<VeroScript::FMapHandle>(Data)
			        : nullptr;
			    UnorderedMap && *UnorderedMap)
			{
				Snapshot.reserve(VeroScript::Interop::BoxedMapCount(**UnorderedMap) + std::visit([](const auto& Entries) -> std::size_t
				{
					using T = std::decay_t<decltype(Entries)>;
					if constexpr (std::is_same_v<T, std::monostate>)
					{
						return 0;
					}
					else
					{
						return Entries.size();
					}
				}, (*UnorderedMap)->ScalarEntries));
				std::visit([&](const auto& Entries)
				{
					using T = std::decay_t<decltype(Entries)>;
					if constexpr (!std::is_same_v<T, std::monostate>)
					{
						Entries.ForEach([&](const auto Key, const auto Mapped)
						{
							VeroScript::FValue VisibleKey = Key;
							using TKey = std::decay_t<decltype(Key)>;
							if constexpr (std::is_same_v<TKey, std::int32_t>)
							{
								if ((*UnorderedMap)->KeyTypeName == "char")
								{
									VisibleKey = static_cast<std::int8_t>(Key);
								}
							}
							using TValue = typename T::mapped_type;
							if constexpr (std::is_same_v<
							                  TValue, VeroScript::FMap::FPrimitiveScalar>)
							{
								Snapshot.emplace_back(
								    std::move(VisibleKey),
								    VeroScript::FMap::UnpackPrimitiveScalar(Mapped));
							}
							else
							{
								Snapshot.emplace_back(
								    std::move(VisibleKey), VeroScript::FValue{Mapped});
							}
						});
					}
				}, (*UnorderedMap)->ScalarEntries);
				VeroScript::Interop::AppendSigned32MapEntries(
				    (*UnorderedMap)->IntEntries,
				    (*UnorderedMap)->KeyTypeName == "char",
				    Snapshot);
				VeroScript::Interop::AppendMapEntries((*UnorderedMap)->UIntEntries, Snapshot);
				VeroScript::Interop::AppendMapEntries((*UnorderedMap)->Int64Entries, Snapshot);
				VeroScript::Interop::AppendMapEntries((*UnorderedMap)->UInt64Entries, Snapshot);
				VeroScript::Interop::AppendMapEntries((*UnorderedMap)->StrEntries, Snapshot);
				VeroScript::Interop::AppendMapEntries((*UnorderedMap)->WStrEntries, Snapshot);
				VeroScript::Interop::AppendMapEntries((*UnorderedMap)->EnumEntries, Snapshot);
				VeroScript::Interop::AppendMapEntries((*UnorderedMap)->HostEntries, Snapshot);
			}
			else if (const auto* OrderedMap = Data
			        ? std::get_if<VeroScript::FOrderedMapHandle>(Data)
			        : nullptr;
			    OrderedMap && *OrderedMap)
			{
				Snapshot.reserve(VeroScript::Interop::BoxedMapCount(**OrderedMap));
				VeroScript::Interop::AppendSigned32OrderedMapEntries(
				    (*OrderedMap)->IntEntries,
				    (*OrderedMap)->IntKeyOrder,
				    (*OrderedMap)->KeyTypeName == "char",
				    Snapshot);
				VeroScript::Interop::AppendOrderedMapEntries(
				    (*OrderedMap)->UIntEntries, (*OrderedMap)->UIntKeyOrder, Snapshot);
				VeroScript::Interop::AppendOrderedMapEntries(
				    (*OrderedMap)->Int64Entries, (*OrderedMap)->Int64KeyOrder, Snapshot);
				VeroScript::Interop::AppendOrderedMapEntries(
				    (*OrderedMap)->UInt64Entries, (*OrderedMap)->UInt64KeyOrder, Snapshot);
				VeroScript::Interop::AppendOrderedMapEntries(
				    (*OrderedMap)->StrEntries, (*OrderedMap)->StrKeyOrder, Snapshot);
				VeroScript::Interop::AppendOrderedMapEntries(
				    (*OrderedMap)->WStrEntries, (*OrderedMap)->WStrKeyOrder, Snapshot);
				VeroScript::Interop::AppendOrderedMapEntries(
				    (*OrderedMap)->EnumEntries, (*OrderedMap)->EnumKeyOrder, Snapshot);
				VeroScript::Interop::AppendOrderedMapEntries(
				    (*OrderedMap)->HostEntries, (*OrderedMap)->HostKeyOrder, Snapshot);
			}
			else
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}

			for (const auto& [Key, Mapped] : Snapshot)
			{
				::vs_interop_value KeyView;
				::vs_interop_value MappedView;
				KeyView.BorrowedRead = &Key;
				MappedView.BorrowedRead = &Mapped;
				KeyView.Owner = SourceOwner;
				MappedView.Owner = SourceOwner;
				KeyView.BorrowedOwner = BorrowedOwner;
				MappedView.BorrowedOwner = KeyView.BorrowedOwner;
				// Worker views need the callback's module metadata too: an empty
				// mapped container can acquire script values after this visit.
				KeyView.NativeContext = NativeContext;
				MappedView.NativeContext = NativeContext;
				MappedView.SharedOwnership = VeroScript::Interop::GetValueSharedOwnership(Mapped, false);
				try
				{
					if (visitor(&KeyView, &MappedView, user_data) != VS_INTEROP_CALLBACK_OK)
					{
						return VS_INTEROP_STATUS_CALLBACK_ERROR;
					}
				}
				catch (...)
				{
					return VS_INTEROP_STATUS_CALLBACK_ERROR;
				}
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_create_set(
	    vs_interop_value* value,
	    const vs_interop_string_view element_type_name,
	    const vs_interop_value* const* elements,
	    const uint64_t element_count)
	{
		if (!VeroScript::Interop::IsWritableValue(value) || !VeroScript::Interop::IsValidText(element_type_name) || element_type_name.size == 0 || (element_count != 0 && !elements) || element_count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) || element_count > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			auto Set = std::make_shared<VeroScript::FSet>();
			Set->ElementTypeName = VeroScript::CanonicalizeNativeElementTypeName(
			    VeroScript::Interop::CopyString(element_type_name));
			if (!VeroScript::Interop::IsSaneMapOrSetKeyTypeName(Set->ElementTypeName))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			Set->TypeName = "set<" + Set->ElementTypeName + ">";
			for (uint64_t Index = 0; Index < element_count; ++Index)
			{
				const auto* Element = VeroScript::Interop::ReadValue(elements[Index]);
				if (!Element)
				{
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}
				if (!VeroScript::Interop::ValueMatchesDeclaredType(
				        *Element, Set->ElementTypeName, elements[Index]))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				if (!VeroScript::Interop::InsertSetElement(*Set, *Element))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
			}
			VeroScript::Interop::AssignValue(*value, std::move(Set));
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_create_set_kind(
	    vs_interop_value* value,
	    const vs_interop_value_kind kind,
	    const vs_interop_string_view element_type_name,
	    const vs_interop_value* const* elements,
	    const uint64_t element_count)
	{
		if (kind == VS_INTEROP_VALUE_SET)
		{
			return vs_interop_value_create_set(
			    value, element_type_name, elements, element_count);
		}
		if (!VeroScript::Interop::IsWritableValue(value) || (kind != VS_INTEROP_VALUE_MULTI_SET && kind != VS_INTEROP_VALUE_ORDERED_MULTI_SET) || !VeroScript::Interop::IsValidText(element_type_name) || element_type_name.size == 0 || (element_count != 0 && !elements) || element_count > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) || element_count > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const std::string ElementType = VeroScript::CanonicalizeNativeElementTypeName(
			    VeroScript::Interop::CopyString(element_type_name));
			if (!VeroScript::Interop::IsSaneMapOrSetKeyTypeName(ElementType))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			const VeroScript::FValue Multiplicity = true;
			if (kind == VS_INTEROP_VALUE_MULTI_SET)
			{
				auto Set = std::make_shared<VeroScript::FMap>();
				Set->KeyTypeName = ElementType;
				Set->ValueTypeName = "array<bool>";
				Set->TypeName = "multiset<" + ElementType + ">";
				for (uint64_t Index = 0; Index < element_count; ++Index)
				{
					const auto* const Element = VeroScript::Interop::ReadValue(elements[Index]);
					if (!Element)
					{
						return VS_INTEROP_STATUS_INVALID_ARGUMENT;
					}
					if (!VeroScript::Interop::ValueMatchesDeclaredType(*Element, ElementType, elements[Index]) || !VeroScript::Interop::AppendMultiValue(*Set, *Element, Multiplicity, "bool", [](VeroScript::FMap& Target, const VeroScript::FValue& Key, const VeroScript::FValue& Value)
					{
						return VeroScript::Interop::InsertMapEntry(Target, Key, Value);
					}))
					{
						return VS_INTEROP_STATUS_TYPE_MISMATCH;
					}
				}
				VeroScript::Interop::AssignValue(*value, std::move(Set));
			}
			else
			{
				auto Set = std::make_shared<VeroScript::FOrderedMap>();
				Set->KeyTypeName = ElementType;
				Set->ValueTypeName = "array<bool>";
				Set->TypeName = "ordered_multiset<" + ElementType + ">";
				for (uint64_t Index = 0; Index < element_count; ++Index)
				{
					const auto* const Element = VeroScript::Interop::ReadValue(elements[Index]);
					if (!Element)
					{
						return VS_INTEROP_STATUS_INVALID_ARGUMENT;
					}
					if (!VeroScript::Interop::ValueMatchesDeclaredType(*Element, ElementType, elements[Index]) || !VeroScript::Interop::AppendMultiValue(*Set, *Element, Multiplicity, "bool", [](VeroScript::FOrderedMap& Target, const VeroScript::FValue& Key, const VeroScript::FValue& Value)
					{
						return VeroScript::Interop::InsertOrderedMapEntry(Target, Key, Value);
					}))
					{
						return VS_INTEROP_STATUS_TYPE_MISMATCH;
					}
				}
				VeroScript::Interop::AssignValue(*value, std::move(Set));
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_count(
	    const vs_interop_value* value,
	    uint64_t* out_count)
	{
		if (!out_count)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			const auto* Set = Data ? std::get_if<VeroScript::FSetHandle>(Data) : nullptr;
			if (Set && *Set)
			{
				*out_count = static_cast<uint64_t>(VeroScript::Interop::SetCount(**Set));
				return VS_INTEROP_STATUS_OK;
			}
			bool bValid = true;
			if (const auto* MultiSet = Data ? std::get_if<VeroScript::FMapHandle>(Data) : nullptr;
			    MultiSet && *MultiSet && VeroScript::Interop::HasGenericPrefix((*MultiSet)->TypeName, "multiset<"))
			{
				const auto Count = VeroScript::Interop::MultiSetMapOccurrenceCount(**MultiSet, bValid);
				if (!bValid) return VS_INTEROP_STATUS_TYPE_MISMATCH;
				*out_count = static_cast<uint64_t>(Count);
				return VS_INTEROP_STATUS_OK;
			}
			if (const auto* MultiSet = Data
			        ? std::get_if<VeroScript::FOrderedMapHandle>(Data)
			        : nullptr;
			    MultiSet && *MultiSet && VeroScript::Interop::HasGenericPrefix((*MultiSet)->TypeName, "ordered_multiset<"))
			{
				const auto Count = VeroScript::Interop::MultiSetMapOccurrenceCount(**MultiSet, bValid);
				if (!bValid) return VS_INTEROP_STATUS_TYPE_MISMATCH;
				*out_count = static_cast<uint64_t>(Count);
				return VS_INTEROP_STATUS_OK;
			}
			return VS_INTEROP_STATUS_TYPE_MISMATCH;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_contains(
	    const vs_interop_value* value,
	    const vs_interop_value* element,
	    uint8_t* out_contains)
	{
		if (!out_contains)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_contains = 0;
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			const auto* Element = VeroScript::Interop::ReadValue(element);
			if (!Element)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			if (const auto* Set = Data ? std::get_if<VeroScript::FSetHandle>(Data) : nullptr;
			    Set && *Set)
			{
				if (!VeroScript::Interop::ValueMatchesDeclaredType(
				        *Element, (*Set)->ElementTypeName, element))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				bool bSupported = false;
				const bool bContains = VeroScript::Interop::SetContains(**Set, *Element, bSupported);
				if (!bSupported)
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				*out_contains = bContains ? 1u : 0u;
				return VS_INTEROP_STATUS_OK;
			}
			if (const auto* MultiSet = Data ? std::get_if<VeroScript::FMapHandle>(Data) : nullptr;
			    MultiSet && *MultiSet && VeroScript::Interop::HasGenericPrefix((*MultiSet)->TypeName, "multiset<"))
			{
				if (!VeroScript::Interop::ValueMatchesDeclaredType(
				        *Element, (*MultiSet)->KeyTypeName, element))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				bool bValid = true;
				const VeroScript::FValue* const Values =
				    VeroScript::Interop::FindBoxedMapValue(**MultiSet, *Element);
				*out_contains = Values && VeroScript::Interop::DynamicArrayElementCount(*Values, bValid) > 0
				    ? 1u
				    : 0u;
				return bValid ? VS_INTEROP_STATUS_OK : VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			if (const auto* OrderedMultiSet = Data
			        ? std::get_if<VeroScript::FOrderedMapHandle>(Data)
			        : nullptr;
			    OrderedMultiSet && *OrderedMultiSet && VeroScript::Interop::HasGenericPrefix((*OrderedMultiSet)->TypeName, "ordered_multiset<"))
			{
				if (!VeroScript::Interop::ValueMatchesDeclaredType(
				        *Element, (*OrderedMultiSet)->KeyTypeName, element))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
				bool bValid = true;
				const VeroScript::FValue* const Values =
				    VeroScript::Interop::FindBoxedMapValue(**OrderedMultiSet, *Element);
				*out_contains = Values && VeroScript::Interop::DynamicArrayElementCount(*Values, bValid) > 0
				    ? 1u
				    : 0u;
				return bValid ? VS_INTEROP_STATUS_OK : VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			return VS_INTEROP_STATUS_TYPE_MISMATCH;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_set_enumerate(
	    const vs_interop_value* value,
	    const vs_interop_set_element_visitor visitor,
	    void* user_data)
	{
		if (!visitor)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(value);
			const auto SourceOwner = value ? value->Owner : std::weak_ptr<VeroScript::Interop::FSessionState>{};
			const auto Owner = SourceOwner.lock();
			const auto BorrowedOwner = value ? VeroScript::Interop::ValueOwnerContext(*value, Owner) : nullptr;
			std::optional<VeroScript::Interop::FScopedSessionActivity> Activity;
			const auto SourceRoot = value ? value->Root : VeroScript::FValueRoot{};
			const auto SourceOwnership = value ? value->SharedOwnership : nullptr;
			if (Owner && Owner->ActiveSessionCalls == 0)
			{
				Activity.emplace(*Owner);
			}
			std::vector<VeroScript::FValue> Snapshot;
			if (const auto* Set = Data ? std::get_if<VeroScript::FSetHandle>(Data) : nullptr;
			    Set && *Set)
			{
				Snapshot.reserve(VeroScript::Interop::SetCount(**Set));
				VeroScript::Interop::AppendSigned32SetElements(
				    (*Set)->IntElements,
				    (*Set)->ElementTypeName == "char",
				    Snapshot);
				VeroScript::Interop::AppendSetElements((*Set)->UIntElements, Snapshot);
				VeroScript::Interop::AppendSetElements((*Set)->Int64Elements, Snapshot);
				VeroScript::Interop::AppendSetElements((*Set)->UInt64Elements, Snapshot);
				VeroScript::Interop::AppendSetElements((*Set)->StrElements, Snapshot);
				VeroScript::Interop::AppendSetElements((*Set)->WStrElements, Snapshot);
				VeroScript::Interop::AppendSetElements((*Set)->EnumElements, Snapshot);
				VeroScript::Interop::AppendSetElements((*Set)->HostElements, Snapshot);
			}
			else if (const auto* MultiSet = Data ? std::get_if<VeroScript::FMapHandle>(Data) : nullptr;
			    MultiSet && *MultiSet && VeroScript::Interop::HasGenericPrefix((*MultiSet)->TypeName, "multiset<"))
			{
				if (!VeroScript::Interop::AppendMultiSetSnapshot(**MultiSet, Snapshot))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
			}
			else if (const auto* OrderedMultiSet = Data
			        ? std::get_if<VeroScript::FOrderedMapHandle>(Data)
			        : nullptr;
			    OrderedMultiSet && *OrderedMultiSet && VeroScript::Interop::HasGenericPrefix((*OrderedMultiSet)->TypeName, "ordered_multiset<"))
			{
				if (!VeroScript::Interop::AppendOrderedMultiSetSnapshot(
				        **OrderedMultiSet, Snapshot))
				{
					return VS_INTEROP_STATUS_TYPE_MISMATCH;
				}
			}
			else
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			for (const VeroScript::FValue& Element : Snapshot)
			{
				::vs_interop_value ElementView;
				ElementView.BorrowedRead = &Element;
				ElementView.Owner = SourceOwner;
				ElementView.BorrowedOwner = BorrowedOwner;
				try
				{
					if (visitor(&ElementView, user_data) != VS_INTEROP_CALLBACK_OK)
					{
						return VS_INTEROP_STATUS_CALLBACK_ERROR;
					}
				}
				catch (...)
				{
					return VS_INTEROP_STATUS_CALLBACK_ERROR;
				}
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_create_channel(
	    vs_interop_value* value,
	    const vs_interop_string_view element_type_name)
	{
		return vs_interop_value_create_bounded_channel(value, element_type_name, 0);
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_create_bounded_channel(
	    vs_interop_value* value,
	    const vs_interop_string_view element_type_name,
	    const std::uint64_t capacity)
	{
		if (!VeroScript::Interop::IsWritableValue(value) || !VeroScript::Interop::IsValidText(element_type_name) || element_type_name.size == 0 || capacity > static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)()) || capacity > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const std::string DeclaredElementType =
			    VeroScript::Interop::CopyString(element_type_name);
			if (!VeroScript::NativeChannelElementTypeIsValid(DeclaredElementType))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			const std::string ElementType =
			    VeroScript::CanonicalizeNativeElementTypeName(DeclaredElementType);
			auto Channel = std::make_shared<VeroScript::FChannel>();
			if (!Channel->TryBindTypeNames(
			        "channel<" + ElementType + ">", ElementType))
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			if (capacity != 0)
			{
				std::string Error;
				if (!Channel->ConfigureCapacity(static_cast<std::size_t>(capacity), Error))
				{
					return VS_INTEROP_STATUS_INVALID_ARGUMENT;
				}
			}
			VeroScript::Interop::AssignValue(*value, std::move(Channel));
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_try_send(
	    const vs_interop_value* channel,
	    const vs_interop_value* value,
	    std::uint8_t* out_sent)
	{
		if (!out_sent)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_sent = 0;
		try
		{
			const auto* ChannelValue = VeroScript::Interop::ReadValue(channel);
			const auto* Data = VeroScript::Interop::ReadValue(value);
			const auto* Channel = ChannelValue
			    ? std::get_if<VeroScript::FChannelHandle>(ChannelValue)
			    : nullptr;
			if (!Channel || !*Channel || (*Channel)->GetRole() != VeroScript::FChannel::ERole::Ordinary)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			if (!Data)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			std::string ChannelTypeName;
			std::string ElementTypeName;
			(*Channel)->GetTypeNames(ChannelTypeName, ElementTypeName);
			if (!VeroScript::Interop::ValueMatchesDeclaredType(
			        *Data, ElementTypeName, value))
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_sent = (*Channel)->TrySend(*Data) ? 1 : 0;
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_send_for(
	    const vs_interop_value* channel,
	    const vs_interop_value* value,
	    const std::uint64_t timeout_ms,
	    std::uint8_t* out_sent)
	{
		if (!out_sent || timeout_ms > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_sent = 0;
		try
		{
			const auto* ChannelValue = VeroScript::Interop::ReadValue(channel);
			const auto* Data = VeroScript::Interop::ReadValue(value);
			const auto* Channel = ChannelValue
			    ? std::get_if<VeroScript::FChannelHandle>(ChannelValue)
			    : nullptr;
			if (!Channel || !*Channel || (*Channel)->GetRole() != VeroScript::FChannel::ERole::Ordinary)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			if (!Data)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			std::string ChannelTypeName;
			std::string ElementTypeName;
			(*Channel)->GetTypeNames(ChannelTypeName, ElementTypeName);
			if (!VeroScript::Interop::ValueMatchesDeclaredType(
			        *Data, ElementTypeName, value))
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			bool bTimedOut = false;
			std::string Error;
			const bool bSent = (*Channel)->SendFor(
			    *Data, static_cast<std::int64_t>(timeout_ms), bTimedOut, Error);
			*out_sent = bSent ? 1 : 0;
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_send(
	    const vs_interop_value* channel,
	    const vs_interop_value* value,
	    vs_interop_buffer** out_error_utf8)
	{
		if (!out_error_utf8)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_error_utf8 = nullptr;
		try
		{
			const auto* ChannelValue = VeroScript::Interop::ReadValue(channel);
			const auto* Data = VeroScript::Interop::ReadValue(value);
			const auto* Channel = ChannelValue
			    ? std::get_if<VeroScript::FChannelHandle>(ChannelValue)
			    : nullptr;
			if (!Channel || !*Channel || (*Channel)->GetRole() != VeroScript::FChannel::ERole::Ordinary)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			if (!Data)
			{
				return VS_INTEROP_STATUS_INVALID_ARGUMENT;
			}
			std::string ChannelTypeName;
			std::string ElementTypeName;
			(*Channel)->GetTypeNames(ChannelTypeName, ElementTypeName);
			if (!VeroScript::Interop::ValueMatchesDeclaredType(
			        *Data, ElementTypeName, value))
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			std::string Error;
			if (!(*Channel)->Send(*Data, Error))
			{
				const auto BufferStatus = VeroScript::Interop::MakeTextBuffer(
				    std::move(Error), out_error_utf8);
				return BufferStatus == VS_INTEROP_STATUS_OK
				    ? VS_INTEROP_STATUS_ENGINE_ERROR
				    : BufferStatus;
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_receive(
	    const vs_interop_value* channel,
	    vs_interop_value* out_value,
	    vs_interop_buffer** out_error_utf8)
	{
		if (!VeroScript::Interop::IsWritableValue(out_value) || !out_error_utf8)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_error_utf8 = nullptr;
		try
		{
			const auto* ChannelValue = VeroScript::Interop::ReadValue(channel);
			const auto* Channel = ChannelValue
			    ? std::get_if<VeroScript::FChannelHandle>(ChannelValue)
			    : nullptr;
			if (!Channel || !*Channel || (*Channel)->GetRole() != VeroScript::FChannel::ERole::Ordinary)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			VeroScript::FValue Data = std::int32_t{0};
			std::string Error;
			if (!(*Channel)->Recv(Data, Error))
			{
				const auto BufferStatus = VeroScript::Interop::MakeTextBuffer(
				    std::move(Error), out_error_utf8);
				return BufferStatus == VS_INTEROP_STATUS_OK
				    ? VS_INTEROP_STATUS_ENGINE_ERROR
				    : BufferStatus;
			}
			VeroScript::Interop::AssignValue(*out_value, std::move(Data));
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_try_receive(
	    const vs_interop_value* channel,
	    vs_interop_value* out_value,
	    std::uint8_t* out_received)
	{
		if (!VeroScript::Interop::IsWritableValue(out_value) || !out_received)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_received = 0;
		try
		{
			const auto* ChannelValue = VeroScript::Interop::ReadValue(channel);
			const auto* Channel = ChannelValue
			    ? std::get_if<VeroScript::FChannelHandle>(ChannelValue)
			    : nullptr;
			if (!Channel || !*Channel || (*Channel)->GetRole() != VeroScript::FChannel::ERole::Ordinary)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			VeroScript::FValue Data = std::int32_t{0};
			if ((*Channel)->TryRecv(Data))
			{
				VeroScript::Interop::AssignValue(*out_value, std::move(Data));
				*out_received = 1;
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_receive_for(
	    const vs_interop_value* channel,
	    const std::uint64_t timeout_ms,
	    vs_interop_value* out_value,
	    std::uint8_t* out_received)
	{
		if (!VeroScript::Interop::IsWritableValue(out_value) || !out_received || timeout_ms > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		*out_received = 0;
		try
		{
			const auto* ChannelValue = VeroScript::Interop::ReadValue(channel);
			const auto* Channel = ChannelValue
			    ? std::get_if<VeroScript::FChannelHandle>(ChannelValue)
			    : nullptr;
			if (!Channel || !*Channel || (*Channel)->GetRole() != VeroScript::FChannel::ERole::Ordinary)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			VeroScript::FValue Data = std::int32_t{0};
			bool bTimedOut = false;
			std::string Error;
			if ((*Channel)->RecvFor(
			        Data, static_cast<std::int64_t>(timeout_ms), bTimedOut, Error))
			{
				VeroScript::Interop::AssignValue(*out_value, std::move(Data));
				*out_received = 1;
			}
			return VS_INTEROP_STATUS_OK;
		}
		catch (const std::bad_alloc&)
		{
			return VS_INTEROP_STATUS_OUT_OF_MEMORY;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_count(
	    const vs_interop_value* channel,
	    std::uint64_t* out_count)
	{
		if (!out_count)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(channel);
			const auto* Channel = Data ? std::get_if<VeroScript::FChannelHandle>(Data) : nullptr;
			if (!Channel || !*Channel)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_count = static_cast<std::uint64_t>((*Channel)->Size());
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_capacity(
	    const vs_interop_value* channel,
	    std::uint64_t* out_capacity)
	{
		if (!out_capacity)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(channel);
			const auto* Channel = Data ? std::get_if<VeroScript::FChannelHandle>(Data) : nullptr;
			if (!Channel || !*Channel)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_capacity = static_cast<std::uint64_t>((*Channel)->Capacity());
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_is_closed(
	    const vs_interop_value* channel,
	    std::uint8_t* out_closed)
	{
		if (!out_closed)
		{
			return VS_INTEROP_STATUS_INVALID_ARGUMENT;
		}
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(channel);
			const auto* Channel = Data ? std::get_if<VeroScript::FChannelHandle>(Data) : nullptr;
			if (!Channel || !*Channel)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			*out_closed = (*Channel)->IsClosed() ? 1 : 0;
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}

	vs_interop_status VS_INTEROP_CALL vs_interop_value_channel_close(
	    const vs_interop_value* channel)
	{
		try
		{
			const auto* Data = VeroScript::Interop::ReadValue(channel);
			const auto* Channel = Data ? std::get_if<VeroScript::FChannelHandle>(Data) : nullptr;
			if (!Channel || !*Channel)
			{
				return VS_INTEROP_STATUS_TYPE_MISMATCH;
			}
			(*Channel)->Close();
			return VS_INTEROP_STATUS_OK;
		}
		catch (...)
		{
			return VeroScript::Interop::TranslateException(nullptr);
		}
	}
}
