// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// dynamic_array.vs — array<T>, range-for, and alias range-for
// Run from this directory: veroscript dynamic_array.vs

void main()
{
	array<int> xs = array<int>();
	xs.push(10);
	xs.push(20);
	xs.push(30);
	print(len(xs));

	// By-value: element is copied each iteration.
	for (int x : xs)
	{
		print(x);
	}

	xs[1] = 99;
	print(xs[1]);

	// In-place: mutate container elements via alias binding.
	for (int& x : xs)
	{
		x = x + 1;
	}
	for (int x : xs)
	{
		print(x);
	}

	print(to_int(3.7));
}
