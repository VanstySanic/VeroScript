// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "Fixtures/value_scope_model.vs";

// The generated helpers must use the declaration's namespace, even when a
// same-named root constant or constexpr function would otherwise be valid.
// Relative namespace members stay lexical; an explicit :: preserves the root.
const int Amount=99;
constexpr int adjusted(int value) { return value+99; }
namespace Names { const int Amount=77; }

namespace MappingValues
{
const int Amount=3;
constexpr int adjusted(int value) { return value+1; }

template<int N> struct Number { int value=N; };
struct Record { Number<Amount> direct; Number<adjusted(Amount)> computed; Number<Names::Amount> qualified; Number<MappingValues::Names::Amount> nested; Number<::Names::Amount> absolute; };

class Property
{
private:
	Number<adjusted(Amount)> stored;
	Number<Names::Amount> stored_ns;
	Number<::Names::Amount> stored_root;
public:
	@JsonProperty(name="child",setter="set")
	Number<adjusted(Amount)> get() const { return stored; }
	void set(Number<adjusted(Amount)> value) { stored=value; }
	@JsonProperty(name="scoped",setter="set_scoped")
	Number<Names::Amount> scoped() const { return stored_ns; }
	void set_scoped(Number<Names::Amount> value) { stored_ns=value; }
	@JsonProperty(name="root",setter="set_root")
	Number<::Names::Amount> rooted() const { return stored_root; }
	void set_root(Number<::Names::Amount> value) { stored_root=value; }
};

template<int N=Amount> class DefaultBase
{
private:
	Number<N> stored;
public:
	@JsonProperty(name="child",setter="set") Number<N> get() const { return stored; }
	void set(Number<N> value) { stored=value; }
};
class DefaultDerived : public DefaultBase<> {};
}

int main()
{
	MappingValues::Record source=MappingValues::Record();
	assert(source.direct.value==3 && source.computed.value==4 && source.qualified.value==5 && source.nested.value==5 && source.absolute.value==77);
	json::value data=json::converter<MappingValues::Record>::write(ref source);
	MappingValues::Record copy=json::converter<MappingValues::Record>::read(data);
	assert(copy.direct.value==3 && copy.computed.value==4 && copy.qualified.value==5 && copy.nested.value==5 && copy.absolute.value==77);
	json::schema schema=json::compile_schema(json::converter<MappingValues::Record>::schema());
	assert(schema.is_valid(data));

	MappingValues::Property property=json::converter<MappingValues::Property>::read(json::parse("{\"child\":{\"value\":17},\"scoped\":{\"value\":19},\"root\":{\"value\":23}}"));
	assert(property.get().value==17 && property.scoped().value==19 && property.rooted().value==23);
	data=json::converter<MappingValues::Property>::write(ref property);
	schema=json::compile_schema(json::converter<MappingValues::Property>::schema());
	assert(schema.is_valid(data));

	MappingValues::DefaultDerived defaults=MappingValues::DefaultDerived();
	assert(defaults.get().value==3);
	defaults=json::converter<MappingValues::DefaultDerived>::read(json::parse("{\"child\":{\"value\":21}}"));
	assert(defaults.get().value==21);
	data=json::converter<MappingValues::DefaultDerived>::write(ref defaults);
	schema=json::compile_schema(json::converter<MappingValues::DefaultDerived>::schema());
	assert(schema.is_valid(data));
	return 0;
}
