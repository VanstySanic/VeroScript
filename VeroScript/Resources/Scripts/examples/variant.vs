// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Core dynamic value boxes. Run: veroscript variant.vs
// Core supplies variant, optional and containers; core.vs imports standard-library helpers.
import "core.vs";

struct Point
{
public:
	int x = 1;
};

class Item
{
public:
	int count = 0;
};

variant echo(variant value)
{
	return value;
}

void main()
{
	variant empty;
	assert(!variant_has_value(empty));
	assert(variant_type_name(empty) == "");

	std::variant answer = echo(42);
	assert(variant_is<int>(answer));
	assert(!variant_is<double>(answer));
	assert(cast<int>(answer) == 42);
	optional<double> converted = try_cast<double>(answer);
	assert(converted.has_value() && converted.get() == 42.0);
	optional<int> mismatch = try_cast<int>(variant("text"));
	assert(!mismatch.has_value());

	Point point = Point();
	variant snapshot = point;
	point.x = 9;
	Point first = cast<Point>(snapshot);
	first.x = 5;
	assert(cast<Point>(snapshot).x == 1);

	Item item = Item();
	variant reference = item;
	item.count = 7;
	assert(cast<Item>(reference).count == 7);
	Item nil = nullptr;
	variant typed_null = nil;
	assert(variant_has_value(typed_null) && typed_null != empty);
	assert(cast<Item>(typed_null) == nullptr);

	map<string, variant> properties = map<string, variant>();
	properties["width"] = answer;
	properties["text"] = "Save";
	array<variant> values = array<variant>();
	values.push(properties["width"]);
	values.push(properties["text"]);
	assert(cast<int>(values[0]) == 42);
	assert(cast<string>(values[1]) == "Save");
	assert(answer == variant(42));
	assert(answer != variant(42.0));
	print("variant: {} / {}", cast<int>(values[0]), cast<string>(values[1]));
}
