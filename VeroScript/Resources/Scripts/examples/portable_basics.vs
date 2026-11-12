// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// portable_basics.vs - explicit resources, value captures, and captured output.
// Run from this directory: veroscript --backend=vm portable_basics.vs
// Hosts supply numeric.vs and an output callback; no other side effects are used.

import "numeric.vs";

int main()
{
	array<int> values = {3, 7, 11};
	int factor = 2;
	function(int) -> int multiply = [factor](int value) -> int => value * factor;
	factor = 10;

	int total = std::numeric::sum(values);
	int answer = multiply(total);
	assert(total == 21);
	assert(answer == 42);
	print("portable basics: {}", answer);
	return 0;
}
