// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include "VeroScriptValue.h"

namespace VeroScript::VmDetail
{
	inline FValue* FindIndexPathElement(FValue& Container, const std::int64_t Index)
	{
		if (Index < 0) return nullptr;
		if (auto* Native = std::get_if<FNativeArray>(&Container))
		{
			return static_cast<std::uint64_t>(Index) < Native->Length()
			    && static_cast<std::uint64_t>(Index) < Native->Storage().Elements.size()
			    ? &Native->Storage().Elements[static_cast<std::size_t>(Index)] : nullptr;
		}
		if (auto* Array = std::get_if<FDynamicArrayHandle>(&Container); Array && *Array)
		{
			(*Array)->MaterializeBoxedElements();
			return static_cast<std::uint64_t>(Index) < (*Array)->Elements.size()
			    ? &(*Array)->Elements[static_cast<std::size_t>(Index)] : nullptr;
		}
		if (auto* Deque = std::get_if<FDequeHandle>(&Container); Deque && *Deque)
		{
			return static_cast<std::uint64_t>(Index) < (*Deque)->Elements.size()
			    ? &(*Deque)->Elements[static_cast<std::size_t>(Index)] : nullptr;
		}
		return nullptr;
	}

	template <typename TMap>
	FValue* FindReferenceEntry(TMap& Map, const FMapEntryKey& Key)
	{
		return std::visit([&](const auto& Value) -> FValue*
		{
			using TKey = std::decay_t<decltype(Value)>;
			auto& Entries = [&]() -> auto&
			{
				if constexpr (std::is_same_v<TKey, std::int32_t>)
				{
					return Map.IntEntries;
				}
				else if constexpr (std::is_same_v<TKey, std::uint32_t>)
				{
					return Map.UIntEntries;
				}
				else if constexpr (std::is_same_v<TKey, std::int64_t>)
				{
					return Map.Int64Entries;
				}
				else if constexpr (std::is_same_v<TKey, std::uint64_t>)
				{
					return Map.UInt64Entries;
				}
				else if constexpr (std::is_same_v<TKey, std::string>)
				{
					return Map.StrEntries;
				}
				else if constexpr (std::is_same_v<TKey, std::u16string>)
				{
					return Map.WStrEntries;
				}
				else if constexpr (std::is_same_v<TKey, FEnumValue>)
				{
					return Map.EnumEntries;
				}
				else
				{
					return Map.HostEntries;
				}
			}();
			const auto Found = Entries.find(Value);
			return Found == Entries.end() ? nullptr : &Found->second;
		}, Key);
	}

	inline FValue* FindReferenceElement(const FContainerElementReference& Target, const std::int64_t Index)
	{
		FValue* Storage = std::visit([&](const auto& Weak) -> FValue*
		{
			const auto Owner = Weak.lock();
			if (!Owner)
			{
				return nullptr;
			}
			using TContainer = typename decltype(Owner)::element_type;
			if constexpr (std::is_same_v<TContainer, FMap> || std::is_same_v<TContainer, FOrderedMap>)
			{
				return Target.Key ? FindReferenceEntry(*Owner, *Target.Key) : nullptr;
			}
			else
			{
				const auto* Container = [&]()
				{
					if constexpr (std::is_void_v<std::remove_const_t<TContainer>>)
					{
						return Target.NativeStorage;
					}
					else
					{
						return Owner.get();
					}
				}();
				if (!Container || Index < 0 || static_cast<std::uint64_t>(Index) >= Container->Elements.size())
				{
					return nullptr;
				}
				return const_cast<FValue*>(&Container->Elements[static_cast<std::size_t>(Index)]);
			}
		}, Target.Container);
		if (!Storage || !Target.bBound)
		{
			return Storage;
		}
		// A moved/erased element must never silently bind a replacement at its old
		// position. Weak cells borrow storage without prolonging its lifetime.
		const auto Cell = Target.Cell.lock();
		const auto* Current = std::get_if<FDynamicArrayHandle>(Storage);
		return Cell && Current && *Current == Cell ? Storage : nullptr;
	}
}
