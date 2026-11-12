// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <cstdint>
#include <vector>

namespace Sample
{
	using Count = std::int32_t;
	template<typename T = Count>
	using Values = std::vector<T>;

	template<typename T>
	T Identity(T Value)
	{
		return Value;
	}

	[[veroscript::Sample(Category = "Cpp")]]
	struct Counter
	{
		explicit Counter(Count Initial)
		{
			this->Value = Initial;
		}

		Count Increment(Count Amount)
		{
			Value += Amount;
			return Value;
		}

		Count Value = 0;
		Values<> History;
	};

	Count Add(Count Left, Count Right)
	{
		return static_cast<std::int32_t>(Left + Right);
	}
}

int main()
{
	Sample::Counter Counter(2);
	return Sample::Identity(Sample::Add(Counter.Increment(3), 4)) == 9 ? 0 : 1;
}
