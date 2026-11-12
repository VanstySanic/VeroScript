// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Independent reference model: reborrow after every mutating region.
{
	const auto CheckProbe = [](auto Zero)
	{
		using K = decltype(Zero);
		VeroScript::TScalarFlatSet<K> Values;
		std::unordered_set<K> Reference;
		const auto Check = [&]()
		{
			const auto Probe = Values.BorrowReadOnlyProbe();
			for (int Index = -8; Index < 100; ++Index)
			{
				const K Key = static_cast<K>(Index * 65536 + 123);
				if (Probe.Contains(Key) != (Reference.count(Key) != 0u))
				{
					return false;
				}
			}
			for (const K Key : {K{0}, std::numeric_limits<K>::min(),
				std::numeric_limits<K>::max()})
			{
				if (Probe.Contains(Key) != (Reference.count(Key) != 0u))
				{
					return false;
				}
			}
			return Values.size() == Reference.size();
		};
		if (!Check())
		{
			return false;
		}
		for (int Index = -5; Index < 80; ++Index)
		{
			const K Key = static_cast<K>(Index * 65536 + 123);
			if (Values.Insert(Key) != Reference.insert(Key).second)
			{
				return false;
			}
		}
		for (const K Key : {K{0}, std::numeric_limits<K>::min(),
			std::numeric_limits<K>::max()})
		{
			Values.Insert(Key);
			Reference.insert(Key);
		}
		if (!Check())
		{
			return false;
		}
		for (int Index = -5; Index < 80; Index += 2)
		{
			const K Key = static_cast<K>(Index * 65536 + 123);
			if (Values.Erase(Key) != (Reference.erase(Key) != 0u))
			{
				return false;
			}
		}
		if (!Check())
		{
			return false; // Collision chains survive tombstones.
		}
		Values.reserve(1024);
		if (!Check())
		{
			return false;
		}
		for (int Index = -5; Index < 80; Index += 2)
		{
			const K Key = static_cast<K>(Index * 65536 + 123);
			Values.Insert(Key);
			Reference.insert(Key);
		}
		return Check();
	};
	if (!Expect(CheckProbe(std::int32_t{0}) && CheckProbe(std::uint32_t{0})
		&& CheckProbe(std::int64_t{0}) && CheckProbe(std::uint64_t{0}),
		"read-only scalar set probe matches independent model"))
	{
		return EXIT_FAILURE;
	}
}
