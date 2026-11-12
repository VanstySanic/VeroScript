// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

struct JsonGuard { int value=29; };

class Outer
{
private:
	struct Hidden { int value=5; };
public:
	enum class State { Ready=1 };
	struct Item { int value=7; };
	int read() { Hidden item=Hidden(); return item.value; }
};

class ItemAdapter
{
public:
	static Outer::Item create(json::value input,json::mapping_context context) { return Outer::Item(); }
	static bool encode(const Outer::Item& input,json::value& output,json::mapping_context context)
	{
		return json::converter<int>::encode(ref input.value,ref output,context);
	}
	static bool decode(json::value input,Outer::Item& output,json::mapping_context context)
	{
		return json::converter<int>::decode(input,ref output.value,context);
	}
	static json::value schema(json::mapping_context context)
	{
		return json::converter<int>::schema_nested(context);
	}
};

class ScopedBase { public: int marker=2; };
class ScopedCase : public ScopedBase { public: int value=3; };
class Tables
{
public:
	@JsonPolymorphic(base="ScopedBase",variants=(item="ScopedCase"))
	class Table {};
};

int local_value()
{
	struct Local { int value=11; };
	Local item=Local();
	return item.value;
}

int local_table()
{
	@JsonPolymorphic(base="ScopedBase",variants=(item="ScopedCase"))
	class Table {};
	ScopedBase input=ScopedCase();
	json::value data=json::custom_converter<ScopedBase,Table>::write(ref input);
	ScopedBase copy=json::custom_converter<ScopedBase,Table>::read(data);
	assert(data.get("kind").as_string()=="item" && copy.marker==2);
	return 0;
}

int main()
{
	JsonGuard guard=JsonGuard();
	json::value guard_data=json::converter<JsonGuard>::write(ref guard);
	assert(guard_data.get("value").as_int64()==29);
	JsonGuard guard_copy=json::converter<JsonGuard>::read(guard_data);
	assert(guard_copy.value==29);
	Outer outer=Outer();
	assert(outer.read()==5 && local_value()==11 && cast<int>(Outer::State::Ready)==1);
	Outer::Item item=Outer::Item();
	json::value data=json::custom_converter<Outer::Item,ItemAdapter>::write(ref item);
	assert(data.as_int64()==7);
	Outer::Item copy=json::custom_converter<Outer::Item,ItemAdapter>::read(json::from_int(19));
	assert(copy.value==19);
	json::schema schema=json::compile_schema(json::custom_converter<Outer::Item,ItemAdapter>::schema());
	assert(schema.is_valid(data));
	ScopedBase input=ScopedCase();
	data=json::custom_converter<ScopedBase,Tables::Table>::write(ref input);
	ScopedBase restored=json::custom_converter<ScopedBase,Tables::Table>::read(data);
	assert(restored.marker==2 && data.get("value").get("value").as_int64()==3);
	schema=json::compile_schema(json::custom_converter<ScopedBase,Tables::Table>::schema());
	assert(schema.is_valid(data) && local_table()==0);
	return 0;
}
