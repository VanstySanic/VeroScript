// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

void show<T>(T input)
{
	// Local types reuse visible type parameters, but never capture stack slots.
	struct Value
	{
		T data;
	};

	Value value = Value();
	value.data = input;
	print(value.data);
	print("local: {}", type<Value>().is_local());

	{
		// A nested block may introduce a distinct type with the same simple name.
		struct Value
		{
			string data = "inner";
		};
		Value inner = Value();
		print(inner.data);
	}
	print(value.data);
}

void main()
{
	show<int>(7);
	auto read = []() -> int
	{
		struct Result
		{
			int data = 9;
		};
		Result result = Result();
		return result.data;
	};
	print(read());
}
