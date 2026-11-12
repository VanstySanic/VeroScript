// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "attribute.vs";

int reads = 0;
int read_value()
{
	reads += 1;
	return 40 + reads;
}

void main()
{
	std::attribute<int> value = 10;
	std::attribute<int> copy = value;
	copy.set(20);
	assert(value.get() == 10 && copy() == 20);

	value.bind(read_value);
	assert(reads == 0); // Binding does not evaluate the getter.
	int snapshot = value; // One implicit read.
	assert(snapshot == 41 && reads == 1);
	assert(value() == 42 && reads == 2); // A new read evaluates again.

	value.bind([]() -> int
	{
		return 50;
	});
	assert(value.get() == 50);
	value.bind_optional([]() -> optional<int>
	{
		return optional<int>();
	});
	assert(value.is_bound() && !value.try_get().has_value());
	assert(value.get_or(7) == 7);
	value.reset();
	assert(!value.is_set() && !value.is_bound());
	print("attribute: fixed, copied, evaluated and reset");
}
