// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "Fixtures/alias_scope_model.vs";

namespace PolymorphicAliasScope
{
class Base { public: int value=7; };
class Case : public Base { public: int child=11; };
using BaseAlias=Base;
using CaseAlias=Case;
@JsonPolymorphic(base="BaseAlias",variants=(item="CaseAlias"))
class Table {};

enum class Mode { Ready=1 };
template<Mode M> class EnumBase { public: int value=cast<int>(M); };
template<Mode M> class EnumCase : public EnumBase<M> {};
@JsonPolymorphic(base="EnumBase<Mode::Ready>",variants=(item="EnumCase<Mode::Ready>"))
class EnumTable {};
}

int main()
{
	PolymorphicAliasScope::Anchor anchor=PolymorphicAliasScope::Anchor();
	assert(anchor.value==0);
	PolymorphicAliasScope::Base input=PolymorphicAliasScope::Case();
	json::value data=json::custom_converter<PolymorphicAliasScope::Base,PolymorphicAliasScope::Table>::write(ref input);
	assert(data.get("kind").as_string()=="item");
	assert(data.get("value").get("value").as_int64()==7 && data.get("value").get("child").as_int64()==11);
	PolymorphicAliasScope::Base copy=json::custom_converter<PolymorphicAliasScope::Base,PolymorphicAliasScope::Table>::read(data);
	assert(copy.value==7);
	json::schema schema=json::compile_schema(json::custom_converter<PolymorphicAliasScope::Base,PolymorphicAliasScope::Table>::schema());
	assert(schema.is_valid(data));

	PolymorphicAliasScope::EnumBase<PolymorphicAliasScope::Mode::Ready> enum_input=PolymorphicAliasScope::EnumCase<PolymorphicAliasScope::Mode::Ready>();
	data=json::custom_converter<PolymorphicAliasScope::EnumBase<PolymorphicAliasScope::Mode::Ready>,PolymorphicAliasScope::EnumTable>::write(ref enum_input);
	assert(data.get("kind").as_string()=="item" && data.get("value").get("value").as_int64()==1);
	PolymorphicAliasScope::EnumBase<PolymorphicAliasScope::Mode::Ready> enum_copy=json::custom_converter<PolymorphicAliasScope::EnumBase<PolymorphicAliasScope::Mode::Ready>,PolymorphicAliasScope::EnumTable>::read(data);
	assert(enum_copy.value==1);
	schema=json::compile_schema(json::custom_converter<PolymorphicAliasScope::EnumBase<PolymorphicAliasScope::Mode::Ready>,PolymorphicAliasScope::EnumTable>::schema());
	assert(schema.is_valid(data));
	return 0;
}
