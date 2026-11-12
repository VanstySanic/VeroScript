// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <vector>

namespace Sample
{
	[[veroscript::Enum(Category="Sample")]]
	enum class EState : std::uint8_t
	{
		Idle = 0,
		Running = 1
	};

	using Count = std::int32_t;
	template<typename T> using List = std::vector<T>;

	[[veroscript::Type(Category="Sample")]]
	struct Counter
	{
		explicit Counter(Count Initial);
		[[veroscript::Callable(Category="Counter")]] Count Get() const;
		void Set([[veroscript::Input(Required)]] Count& NewValue);
		static Count Twice(Count Value);

		[[veroscript::Property(Visible)]] Count Value;
	};

	[[veroscript::Utility(Category="Math")]]
	Count Add(Count Left, Count Right);
}
