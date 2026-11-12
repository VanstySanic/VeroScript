// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "Fixtures/alias_base_model.vs";
import "Fixtures/alias_bridge_model.vs";

namespace LocalAlias
{
template<typename T> class Base { public: T inherited; };
using NamedBase=Base<int>;
using AliasChain=NamedBase;
class Derived : public AliasChain { public: int own=5; };
}

struct ImportedDerived : public AliasModel::AliasChain { int own=9; };

namespace OverloadedBase
{
class Base<T,U=int> { public: T first; U second; };
struct Base<T> { T exact; };
struct One : public Base<int> {};
class Two : public Base<int,string> {};
}

int main()
{
	LocalAlias::Derived local=LocalAlias::Derived();
	local.inherited=17;
	json::value data=json::converter<LocalAlias::Derived>::write(ref local);
	assert(data.get("inherited").as_int64()==17 && data.get("own").as_int64()==5);
	LocalAlias::Derived local_copy=json::converter<LocalAlias::Derived>::read(data);
	assert(local_copy.inherited==17 && local_copy.own==5);
	json::schema schema=json::compile_schema(json::converter<LocalAlias::Derived>::schema());
	assert(schema.is_valid(data) && !schema.is_valid(json::parse("{\"inherited\":false}")));

	ImportedDerived imported=ImportedDerived();
	imported.inherited="base";
	data=json::converter<ImportedDerived>::write(ref imported);
	assert(data.get("inherited").as_string()=="base" && data.get("count").as_int64()==7);
	ImportedDerived imported_copy=json::converter<ImportedDerived>::read(data);
	assert(imported_copy.inherited=="base" && imported_copy.count==7 && imported_copy.own==9);
	schema=json::compile_schema(json::converter<ImportedDerived>::schema());
	assert(schema.is_valid(data) && !schema.is_valid(json::parse("{\"inherited\":false}")));

	OverloadedBase::One one=OverloadedBase::One();
	one.exact=23;
	data=json::converter<OverloadedBase::One>::write(ref one);
	assert(data.get("exact").as_int64()==23 && !data.contains("first") && !data.contains("second"));
	OverloadedBase::One one_copy=json::converter<OverloadedBase::One>::read(data);
	assert(one_copy.exact==23);
	schema=json::compile_schema(json::converter<OverloadedBase::One>::schema());
	assert(schema.is_valid(data));

	OverloadedBase::Two two=OverloadedBase::Two();
	two.first=31;
	two.second="two";
	data=json::converter<OverloadedBase::Two>::write(ref two);
	assert(data.get("first").as_int64()==31 && data.get("second").as_string()=="two" && !data.contains("exact"));
	OverloadedBase::Two two_copy=json::converter<OverloadedBase::Two>::read(data);
	assert(two_copy.first==31 && two_copy.second=="two");
	schema=json::compile_schema(json::converter<OverloadedBase::Two>::schema());
	assert(schema.is_valid(data) && !schema.is_valid(json::parse("{\"second\":false}")));
	return 0;
}
