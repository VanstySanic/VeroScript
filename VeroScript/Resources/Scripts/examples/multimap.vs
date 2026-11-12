// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// multimap.vs — grouped hash and insertion-ordered multi-value maps
// Run from this directory: veroscript multimap.vs

void main()
{
	// A multimap index is array<V>. add() appends instead of replacing.
	multimap<string, int> grouped();
	grouped.add("alpha", 1).add("alpha", 2);
	grouped.add("beta", 3);
	print("distinct keys: {}, alpha values: [{}, {}]", len(grouped), grouped["alpha"][0], grouped["alpha"][1]);

	// Brace initialization groups each key's values explicitly.
	multimap<string, int> initialized = {"alpha": {4, 5}, "beta": {6}};
	print("initialized alpha count: {}", len(initialized["alpha"]));

	// ordered_multimap preserves first insertion order for distinct keys.
	ordered_multimap<string, int> ordered();
	ordered.add("second", 20);
	ordered.add("first", 10);
	ordered.add("second", 21);
	print("ordered keys: [{}, {}], second last: {}", ordered.keys()[0], ordered.keys()[1], ordered["second"][1]);

	// remove(key) removes every value stored for that key.
	ordered.remove("first");
	print("remaining distinct keys: {}", len(ordered));
}
