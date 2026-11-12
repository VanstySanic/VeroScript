// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptBytecode.h"

#include <algorithm>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace VeroScript::VmDetail
{
	inline FValue MakeLocalReference(const FValue& Local, std::uint32_t Frame, std::uint16_t Slot)
	{
		// Forwarding an alias preserves its original identity and readonly cell.
		if (std::holds_alternative<FLocalRef>(Local) || std::holds_alternative<FGlobalRef>(Local)
		    || std::holds_alternative<FThisFieldRef>(Local) || std::holds_alternative<FIndexRef>(Local)
		    || std::holds_alternative<FMapEntryRef>(Local) || IsClosureReferenceCell(Local))
		{
			return Local;
		}
		return FLocalRef{Frame, Slot};
	}

	// Stage a new value before publishing the entry or its ordered-map key.
	// The caller holds a container copy scope across user callbacks.
	template <typename TEntries, typename TKey, typename TAssign>
	bool AssignMapEntry(TEntries& Entries, const TKey& Key, TAssign&& Assign,
	    std::vector<typename TEntries::key_type>* KeyOrder = nullptr)
	{
		const auto Existing = Entries.find(Key);
		if (Existing != Entries.end()) return Assign(Existing->second);
		FValue Prepared;
		if (!Assign(Prepared)) return false;
		if (KeyOrder)
		{
			KeyOrder->push_back(Key);
		}
		try
		{
			Entries.emplace(Key, std::move(Prepared));
		}
		catch (...)
		{
			if (KeyOrder)
			{
				KeyOrder->pop_back();
			}
			throw;
		}
		return true;
	}

	// Keep native iterators and assignment slots valid across user value-copy
	// callbacks. Reads and writes to unrelated containers remain available.
	class FContainerCopyReadScope
	{
	public:
		FContainerCopyReadScope(std::vector<const void*>& InReads, const void* Identity)
		    : Reads(InReads), bActive(Identity != nullptr)
		{
			if (bActive)
			{
				Reads.push_back(Identity);
			}
		}
		~FContainerCopyReadScope()
		{
			if (bActive)
			{
				Reads.pop_back();
			}
		}
		FContainerCopyReadScope(const FContainerCopyReadScope&) = delete;
		FContainerCopyReadScope& operator=(const FContainerCopyReadScope&) = delete;

	private:
		std::vector<const void*>& Reads;
		bool bActive;
	};

	inline bool ValueCopyMayInvokeCallbacks(const FValue& Value)
	{
		return std::visit([](const auto& Item)
		{
			using T = std::decay_t<decltype(Item)>;
			if constexpr (std::is_same_v<T, FObjectHandle>)
			{
				return Item && Item->Kind == EScriptObjectKind::Struct;
			}
			else
			{
				return std::is_same_v<T, FNativeArray> || std::is_same_v<T, FHostObjectHandle> || std::is_same_v<T, FScriptVariantHandle> || std::is_same_v<T, FLocalRef> || std::is_same_v<T, FGlobalRef> || std::is_same_v<T, FThisFieldRef> || std::is_same_v<T, FIndexRef> || std::is_same_v<T, FMapEntryRef>;
			}
		}, Value);
	}

	inline std::string_view CanonicalPrimitiveTypeName(std::string_view TypeName)
	{
		if (TypeName == "long") return "int64";
		if (TypeName == "unsigned long") return "uint64";
		if (TypeName == "ref long") return "ref int64";
		if (TypeName == "const ref long") return "const ref int64";
		if (TypeName == "ref unsigned long") return "ref uint64";
		if (TypeName == "const ref unsigned long") return "const ref uint64";
		return TypeName;
	}

	// Compare runtime identities without changing source-facing metadata or
	// relaxing reference qualifiers and script object identities.
	bool RuntimeTypeNamesEqual(std::string_view Left, std::string_view Right);

	std::string ConversionDispatchLeaf(const FBytecodeFunction& Function);
	bool ConversionDispatchLeavesMatch(std::string_view Left, std::string_view Right);
	std::uint16_t FindVirtualConversionMethod(const FBytecodeModule& Module,
	    std::string_view OwnerName, std::string_view DispatchLeaf);

	inline bool IsSigned32KeyStorageType(const std::string_view TypeName)
	{
		return TypeName == "char" || TypeName == "int";
	}

	inline bool TryGetSigned32Key(
	    const FValue& Value,
	    const std::string_view TypeName,
	    std::int32_t& OutKey)
	{
		if (TypeName == "char")
		{
			const auto* const Key = std::get_if<std::int8_t>(&Value);
			if (!Key)
			{
				return false;
			}
			OutKey = *Key;
			return true;
		}
		if (TypeName == "int")
		{
			const auto* const Key = std::get_if<std::int32_t>(&Value);
			if (!Key)
			{
				return false;
			}
			OutKey = *Key;
			return true;
		}
		return false;
	}

	inline FValue MakeSigned32KeyValue(
	    const std::int32_t Key,
	    const std::string_view TypeName)
	{
		return TypeName == "char"
		    ? FValue{static_cast<std::int8_t>(Key)}
		    : FValue{Key};
	}

	inline bool IsValidSigned32KeyStorageValue(
	    const std::int32_t Key,
	    const std::string_view TypeName)
	{
		return TypeName != "char" || (Key >= -128 && Key <= 127);
	}

	inline bool PriorityValueLess(const FValue& Left, const FValue& Right)
	{
		if (const auto* L = std::get_if<std::int8_t>(&Left))
		{
			const auto* const R = std::get_if<std::int8_t>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<std::uint8_t>(&Left))
		{
			const auto* const R = std::get_if<std::uint8_t>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<std::int16_t>(&Left))
		{
			const auto* const R = std::get_if<std::int16_t>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<std::uint16_t>(&Left))
		{
			const auto* const R = std::get_if<std::uint16_t>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<std::int32_t>(&Left))
		{
			const auto* const R = std::get_if<std::int32_t>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<std::uint32_t>(&Left))
		{
			const auto* const R = std::get_if<std::uint32_t>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<std::int64_t>(&Left))
		{
			const auto* const R = std::get_if<std::int64_t>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<std::uint64_t>(&Left))
		{
			const auto* const R = std::get_if<std::uint64_t>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<double>(&Left))
		{
			const auto* const R = std::get_if<double>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<float>(&Left))
		{
			const auto* const R = std::get_if<float>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<bool>(&Left))
		{
			const auto* const R = std::get_if<bool>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<std::string>(&Left))
		{
			const auto* const R = std::get_if<std::string>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		if (const auto* L = std::get_if<std::u16string>(&Left))
		{
			const auto* const R = std::get_if<std::u16string>(&Right);
			return R ? *L < *R : Left.index() < Right.index();
		}
		return Left.index() < Right.index();
	}

	inline bool IsScopedEnumType(
	    const FBytecodeModule* const Module,
	    const std::string_view TypeName)
	{
		if (!Module)
		{
			return false;
		}
		return std::any_of(
		    Module->Enums.begin(),
		    Module->Enums.end(),
		    [TypeName](const FBytecodeEnumDesc& EnumDesc)
		{
			return EnumDesc.bScoped && EnumDesc.Name == TypeName;
		});
	}
}
