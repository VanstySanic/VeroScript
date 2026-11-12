// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Fixed values and named getters; also used by standalone AOT smoke tests.
import "attribute.vs";

int reads = 0;
int read_value()
{
	reads += 1;
	return 8;
}

void main()
{
	std::attribute<int> value = 3;
	std::attribute<int> copy = value;
	copy.set(5);
	assert(value.get() == 3 && copy.get() == 5);
	value.bind(read_value);
	assert(reads == 0);
	int snapshot = value;
	assert(snapshot == 8 && reads == 1);
	assert(value() == 8 && reads == 2);
	value.reset();
	assert(!value.try_get().has_value() && value.get_or(11) == 11);
	print("attribute functions: ok");
}
