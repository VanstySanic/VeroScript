// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// reference_returns.vs - writable and const reference returns
// Run from this directory: veroscript reference_returns.vs

int Counter = 1;
array<int> Values = array<int>();

int& counter_ref()
{
	return Counter;
}

const int& read_counter()
{
	return Counter;
}

int& first_value()
{
	return Values[0];
}

class Item
{
public:
	Item(int input)
	{
		value = input;
	}

	int value;
};

Item Current = Item(10);
Item Replacement = Item(20);

Item& current_handle()
{
	return Current;
}

class Holder
{
public:
	int& get();

	int value = 3;
};

int& Holder::get()
{
	return this.value;
}

void main()
{
	counter_ref() += 4;
	print("counter: {}", read_counter());

	int& alias = counter_ref();
	alias++;
	print("aliased counter: {}", Counter);

	fn() -> int& getter = []() -> int &
	{
		return Counter;
	};
	getter() = 8;
	print("lambda reference: {}", Counter);

	Values.push(40);
	first_value() += 2;
	print("container element: {}", Values[0]);

	// Class values are reference-counted handles. This reference aliases the
	// caller's handle slot, so assignment rebinds Current to Replacement.
	current_handle() = Replacement;
	print("rebound class handle: {}", Current.value);

	Holder holder = Holder();
	holder.get() = 9;
	print("out-of-class method: {}", holder.value);
}
