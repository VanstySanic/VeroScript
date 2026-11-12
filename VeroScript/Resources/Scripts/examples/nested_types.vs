// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class Catalog<T>
{
private:
	struct Internal
	{
		int code = 1;
	};

public:
	struct Entry<U>
	{
		T id;
		U label;
	};

	enum class State
	{
		Ready = 1,
		Closed = 2
	};

	using Id = T;

	int code()
	{
		Internal value = Internal();
		return value.code;
	}
};

void main()
{
	Catalog<int>::Entry<string> entry = Catalog<int>::Entry<string>();
	entry.id = 7;
	entry.label = "Save";
	Catalog<int>::Id id = entry.id;
	Catalog<int>::State state = Catalog<int>::State::Ready;
	print("{}: {} (ready: {})", id, entry.label, state == Catalog<int>::State::Ready);
	Catalog<int> catalog = Catalog<int>();
	print("internal code: {}", catalog.code());

	// The catalog also describes child templates without instantiating them.
	auto info = type<Catalog<int>>();
	print("nested declarations: {}", info.nested_type_count());
	print("first: {}, own parameters: {}", info.nested_type_name(0), info.nested_type_parameter_count(0));
}
