// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "attribute.vs";

struct Counter
{
public:

	int read() const
	{
		return value;
	}

	int value = 2;
};

class Model
{
public:

	int read() const
	{
		return value;
	}

	int value = 7;
};
int read_model(Model model)
{
	return model.value;
}

void main()
{
	Model model = Model();
	weak<Model> observer = weak<Model>(model);
	auto value = std::make_weak_attribute<int>(observer, read_model);
	auto named = std::make_weak_attribute_by_name<int>(observer, "read");
	assert(value() == 7 && named() == 7);
	model.value = 9;
	assert(value() == 9 && named() == 9);
	model = nullptr;
	assert(!value.try_get().has_value() && named.get_or(13) == 13);
	assert(named.is_bound()); // Configured even though the weak target expired.

	shared<Counter> counter = shared<Counter>(Counter());
	auto current = std::make_weak_attribute_by_name<int>(weak<shared<Counter>>(counter), "read");
	assert(current() == 2);
	counter.value.value = 4;
	assert(current() == 4);
	counter = nullptr;
	assert(!current.try_get().has_value());
	print("attribute weak: ok");
}
