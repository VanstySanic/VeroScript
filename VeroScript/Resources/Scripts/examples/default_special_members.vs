// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Copy construction and assignment are separate operations. Equality is opt-in.
struct Record
{
	int value = 3;
	string label = "record";

	Record() = default;
	Record(const Record& other) = default;
	Record & operator=(const Record& other) = default;
	bool operator==(const Record& other) const = default;
	bool operator!=(const Record& other) const = default;
};

void main()
{
	Record first = Record();
	Record second = first;
	second.value = 7;
	first = second;
	print("{} {} {}", first.value, first == second, first != second);
}
