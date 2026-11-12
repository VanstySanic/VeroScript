// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include "VeroScriptValue.h"

#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <limits>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	template <typename K>
	bool CheckStorage()
	{
		for (const std::uint64_t Stride : {1ull, 65536ull, 65537ull, 4294967296ull})
		{
			VeroScript::TScalarFlatMap<K, std::int32_t> Map;
			VeroScript::TScalarFlatSet<K> Set;
			std::unordered_map<K, std::int32_t> Reference;
			std::unordered_set<K> ReferenceSet;
			const auto Assign = [&](const K Key, const std::int32_t Value)
			{
				Map.InsertOrAssign(Key, Value);
				Reference.insert_or_assign(Key, Value);
				return Set.Insert(Key) == ReferenceSet.insert(Key).second;
			};
			const auto Check = [&]()
			{
				if (Map.size() != Reference.size() || Set.size() != ReferenceSet.size())
				{
					return false;
				}
				const auto Probe = Set.BorrowReadOnlyProbe();
				for (const auto& Entry : Reference)
				{
					const auto* const Value = Map.Find(Entry.first);
					if (!Value || *Value != Entry.second || !Probe.Contains(Entry.first))
					{
						return false;
					}
				}
				std::size_t Visited = 0;
				Map.ForEach([&](const K Key, const std::int32_t Value)
				{
					const auto Found = Reference.find(Key);
					if (Found != Reference.end() && Found->second == Value)
					{
						++Visited;
					}
				});
				return Visited == Reference.size();
			};
			Map.reserve(4096);
			Set.reserve(4096);
			for (std::uint64_t Index = 0; Index < 4096; ++Index)
			{
				// Unsigned construction keeps wraparound defined for 32-bit key types.
				using U = std::make_unsigned_t<K>;
				const K Key = static_cast<K>(static_cast<U>(Index * Stride));
				if (!Assign(Key, static_cast<std::int32_t>(Index))) return false;
			}
			for (const K Key : {K{0}, std::numeric_limits<K>::min(), std::numeric_limits<K>::max()})
			{
				if (!Assign(Key, 17)) return false;
			}
			if (!Check()) return false;
			for (std::uint64_t Index = 0; Index < 4096; Index += 2)
			{
				using U = std::make_unsigned_t<K>;
				const K Key = static_cast<K>(static_cast<U>(Index * Stride));
				if (Map.Erase(Key) != (Reference.erase(Key) != 0)
					|| Set.Erase(Key) != (ReferenceSet.erase(Key) != 0)
					|| Map.Find(Key) || Set.Contains(Key))
				{
					return false;
				}
			}
			if (!Check()) return false;
			Map.reserve(16384);
			Set.reserve(16384);
			if (!Check()) return false;
			for (std::uint64_t Index = 0; Index < 4096; ++Index)
			{
				using U = std::make_unsigned_t<K>;
				if (!Assign(static_cast<K>(static_cast<U>(Index * Stride)), -3)) return false;
			}
			if (!Check()) return false;
		}
		return true;
	}

	bool CheckDenseViews()
	{
		VeroScript::TScalarFlatMap<std::int64_t, std::int32_t> Map;
		Map.InsertOrAssignGenerated(1024, [](const std::size_t Index, auto& Key, auto& Value)
		{
			Key = static_cast<std::int64_t>(1023 - Index);
			Value = static_cast<std::int32_t>(Key * 2);
		});
		std::vector<std::int32_t*> Dense;
		if (!Map.TryGetDenseZeroBasedValues(1024, Dense) || Dense.size() != 1024) return false;
		for (std::size_t Index = 0; Index < Dense.size(); ++Index)
		{
			if (!Dense[Index] || *Dense[Index] != static_cast<std::int32_t>(Index * 2)) return false;
			*Dense[Index] += 1;
			if (*Map.Find(static_cast<std::int64_t>(Index)) != static_cast<std::int32_t>(Index * 2 + 1)) return false;
		}
		Map.Erase(0);
		Map.InsertOrAssign(-1, 7);
		std::int32_t Sentinel = 5;
		Dense = {&Sentinel};
		return !Map.TryGetDenseZeroBasedValues(1024, Dense)
			&& Dense.size() == 1 && Dense.front() == &Sentinel;
	}

	bool CheckGeneratedStorageReuse()
	{
		VeroScript::TScalarFlatSet<std::int64_t> Set;
		VeroScript::TScalarFlatMap<std::int64_t, std::int32_t> Map;
		Set.reserve(4);
		Map.reserve(4);
		for (std::int64_t Round = 0; Round < 256; ++Round)
		{
			Set.InsertGenerated(4, [Round](const std::size_t Index, auto& Key)
			{
				Key = Round * 4 + static_cast<std::int64_t>(Index);
			});
			Map.InsertOrAssignGenerated(4, [Round](const std::size_t Index, auto& Key, auto& Value)
			{
				Key = Round * 4 + static_cast<std::int64_t>(Index);
				Value = static_cast<std::int32_t>(Index);
			});
			if (Set.size() != 4 || Map.size() != 4) return false;
			for (std::int64_t Index = 0; Index < 4; ++Index)
			{
				const auto Key = Round * 4 + Index;
				if (!Set.Contains(Key) || !Set.Erase(Key) || Set.Contains(Key)) return false;
				const auto* const Value = Map.Find(Key);
				if (!Value || *Value != Index || !Map.Erase(Key) || Map.Find(Key)) return false;
			}
			if (Set.size() != 0 || Map.size() != 0) return false;
		}
		return true;
	}

	bool CheckErasedValueLifetime()
	{
		VeroScript::TScalarFlatMap<int, std::shared_ptr<int>> Map;
		auto Value = std::make_shared<int>(7);
		std::weak_ptr<int> Observer = Value;
		Map.InsertOrAssign(1, Value);
		Value.reset();
		return Map.Erase(1) && Observer.expired();
	}
}

int main()
{
	if (!CheckStorage<std::int32_t>() || !CheckStorage<std::uint32_t>()
		|| !CheckStorage<std::int64_t>() || !CheckStorage<std::uint64_t>()
		|| !CheckDenseViews() || !CheckGeneratedStorageReuse() || !CheckErasedValueLifetime())
	{
		std::cerr << "scalar storage differs from the independent container model\n";
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}
