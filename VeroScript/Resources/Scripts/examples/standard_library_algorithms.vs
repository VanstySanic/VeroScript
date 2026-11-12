// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// standard_library_algorithms.vs - algorithms, numeric aggregates, and PRNG
// Run from this directory: veroscript standard_library_algorithms.vs

import "algorithm.vs";
import "numeric.vs";
import "random.vs";

int main()
{
	array<int> values = {9, 2, 7, 2, 5};
	std::algorithm::sort(values);

	print("sorted: {} {} {} {} {}", values[0], values[1], values[2], values[3], values[4]);
	print("first 5: {}", std::algorithm::lower_bound(values, 5));
	print("has 7: {}", std::algorithm::binary_search(values, 7));
	print("count 2: {}", std::algorithm::count(values, 2));
	array<int> shuffledA = std::algorithm::copy(values);
	array<int> shuffledB = std::algorithm::copy(values);
	array<unsigned int> shuffleA = std::random::make_engine(2026u);
	array<unsigned int> shuffleB = std::random::make_engine(2026u);
	std::algorithm::shuffle(shuffledA, shuffleA);
	std::algorithm::shuffle(shuffledB, shuffleB);
	print("repeatable shuffle: {}", std::algorithm::equal(shuffledA, shuffledB));

	print("sum: {}", std::numeric::sum(values));
	print("sum with initial: {}", std::numeric::accumulate(values, 10));
	print("average: {}", std::numeric::average(values));

	array<int> wide = {2147483647, 1};
	assert(std::numeric::sum_as<int64>(wide) == 2147483648i64);
	print("wide sum: {}", std::numeric::sum_as<int64>(wide));

	array<unsigned int> first = std::random::make_engine(123u);
	array<unsigned int> second = std::random::make_engine(123u);
	print("repeatable: {}", std::random::next_uint(first) == std::random::next_uint(second));
	double sample = std::random::next_double(first);
	print("unit interval: {}", sample >= 0.0 && sample < 1.0);
	int die = std::random::next_int(first, 1, 7);
	print("die range: {}", die >= 1 && die < 7);
	return 0;
}
