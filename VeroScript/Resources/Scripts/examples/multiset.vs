// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// multiset.vs - counted sets and insertion-ordered counted sets
// Run from this directory: veroscript multiset.vs

void main()
{
	multiset<int> numbers = {2, 1, 2, 3, 2};
	print("total: {}, distinct membership: {}, count(2): {}", len(numbers), numbers.has(3), numbers.count(2));

	// remove(value) removes one occurrence.
	numbers.remove(2);
	print("after remove: total {}, count(2): {}", len(numbers), numbers.count(2));

	// ordered_multiset keeps first-distinct-value insertion order.
	ordered_multiset<string> words = {"beta", "alpha", "beta", "gamma"};
	array<string> ordered = words.values();
	print("ordered values: [{}, {}, {}, {}]", ordered[0], ordered[1], ordered[2], ordered[3]);

	words.remove("beta");
	print("after removing one beta:");
	for (string word : words)
	{
		print(word);
	}
}
