// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "attribute.vs";
import "delegate.vs";

class Model
{
public:

	int read() const
	{
		return value;
	}
	int add(int amount)
	{
		return value + amount;
	}

	int value = 12;
};

void main()
{
	Model model = Model();
	auto value = std::make_attribute_by_name<int>(model, "read");
	fn(int) -> int add = std::bind_method < fn(int) -> int > (model, "add");
	std::delegate<int> reader = std::delegate<int>();
	reader.bind(std::bind_method < fn() -> int > (model, "read"));
	assert(value() == 12 && add(3) == 15 && reader.execute() == 12);
	model.value = 20;
	assert(value.get() == 20 && reader.execute() == 20);
	model = nullptr; // Strong bindings retain the receiver.
	assert(value() == 20 && add(3) == 23);
	value.reset();
	reader.unbind();
	print("attribute methods: ok");
}
