// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "Fixtures/value_template_model.vs";

int main()
{
	JsonGuard guard=JsonGuard::Ready;
	assert(json::converter<JsonGuard>::write(ref guard).as_string()=="Ready");
	assert(json::converter<JsonGuard>::read(json::from_string("Ready"))==guard);
	json::schema guard_schema=json::compile_schema(json::converter<JsonGuard>::schema());
	assert(guard_schema.is_valid(json::from_string("Ready")));
	ValueModel::Record<string,9,true> source=ValueModel::Record<string,9,true>();
	source.payload="value-template";
	source.inherited="base";
	json::value data=json::converter<ValueModel::Record<string,9,true>>::write(ref source);
	assert(data.get("count").as_int64()==9 && data.get("enabled").as_bool());
	assert(data.get("payload").as_string()=="value-template" && !data.contains("limit"));
	assert(data.get("inherited").as_string()=="base");

	ValueModel::Record<string,9,true> copy=json::converter<ValueModel::Record<string,9,true>>::read(data);
	assert(copy.count==9 && copy.enabled && copy.payload==source.payload && copy.inherited==source.inherited);
	ValueModel::Record<int,3,false> other=json::converter<ValueModel::Record<int,3,false>>::read(json::parse("{}"));
	assert(other.count==3 && !other.enabled);
	json::schema schema=json::compile_schema(json::converter<ValueModel::Record<string,9,true>>::schema());
	assert(schema.is_valid(data));

	ValueModel::Base<> defaults=json::converter<ValueModel::Base<>>::read(json::parse("{}"));
	assert(defaults.count==7);
	ValueModel::Tagged<ValueModel::Mode::Last> tagged=ValueModel::Tagged<ValueModel::Mode::Last>();
	data=json::converter<ValueModel::Tagged<ValueModel::Mode::Last>>::write(ref tagged);
	assert(data.get("selected").as_string()=="Last");
	ValueModel::Tagged<ValueModel::Mode::Last> tagged_copy=json::converter<ValueModel::Tagged<ValueModel::Mode::Last>>::read(data);
	assert(tagged_copy.selected==ValueModel::Mode::Last);

	ValueModel::Wide<18446744073709551615u64> wide=ValueModel::Wide<18446744073709551615u64>();
	data=json::converter<ValueModel::Wide<18446744073709551615u64>>::write(ref wide);
	assert(data.get("value").as_uint64()==18446744073709551615u64);
	ValueModel::Wide<18446744073709551615u64> wide_copy=json::converter<ValueModel::Wide<18446744073709551615u64>>::read(data);
	assert(wide_copy.value==wide.value);

	ValueModel::Colliding<int,41,2,true> colliding=ValueModel::Colliding<int,41,2,true>();
	colliding.payload=31;
	data=json::converter<ValueModel::Colliding<int,41,2,true>>::write(ref colliding);
	assert(data.get("payload").as_int64()==31 && data.get("total").as_int64()==43 && data.get("enabled").as_bool());
	assert(data.get("fixed").get("value").as_int64()==23);
	ValueModel::Colliding<int,41,2,true> collision_copy=json::converter<ValueModel::Colliding<int,41,2,true>>::read(data);
	assert(collision_copy.payload==31 && collision_copy.fixed.value==23);
	schema=json::compile_schema(json::converter<ValueModel::Colliding<int,41,2,true>>::schema());
	assert(schema.is_valid(data));

	ValueModel::DefaultDerived default_base=json::converter<ValueModel::DefaultDerived>::read(json::parse("{}"));
	assert(default_base.count==7);
	data=json::converter<ValueModel::DefaultDerived>::write(ref default_base);
	schema=json::compile_schema(json::converter<ValueModel::DefaultDerived>::schema());
	assert(schema.is_valid(data));
	ValueModel::Shifted<18,string> shifted=ValueModel::Shifted<18,string>();
	shifted.inherited="shift";
	data=json::converter<ValueModel::Shifted<18,string>>::write(ref shifted);
	assert(data.get("count").as_int64()==9);
	schema=json::compile_schema(json::converter<ValueModel::Shifted<18,string>>::schema());
	assert(schema.is_valid(data));
	ValueModel::Quoted<3,string> quoted=ValueModel::Quoted<3,string>();
	quoted.inherited="quoted";
	data=json::converter<ValueModel::Quoted<3,string>>::write(ref quoted);
	assert(data.get("count").as_int64()==62 && data.get("marker").as_int64()==3);
	schema=json::compile_schema(json::converter<ValueModel::Quoted<3,string>>::schema());
	assert(schema.is_valid(data));

	ValueModel::PropertyDerived<3> property=json::converter<ValueModel::PropertyDerived<3>>::read(json::parse("{\"child\":{\"count\":25}}"));
	assert(property.get().count==25);
	data=json::converter<ValueModel::PropertyDerived<3>>::write(ref property);
	schema=json::compile_schema(json::converter<ValueModel::PropertyDerived<3>>::schema());
	assert(schema.is_valid(data));
	ValueModel::ChosenProperty<-1,4,7> chosen=json::converter<ValueModel::ChosenProperty<-1,4,7>>::read(json::parse("{\"child\":{\"count\":33}}"));
	assert(chosen.get().count==33);

	ValueModel::TraitRecord<int> trait=json::converter<ValueModel::TraitRecord<int>>::read(json::parse("{\"payload\":37}"));
	assert(trait.payload==37);
	data=json::converter<ValueModel::TraitRecord<int>>::write(ref trait);
	schema=json::compile_schema(json::converter<ValueModel::TraitRecord<int>>::schema());
	assert(schema.is_valid(data));

	json::mapping_context context=json::mapping_context_create();
	json::value first=json::converter<ValueModel::Record<int,3,true>>::schema_nested(context);
	json::value second=json::converter<ValueModel::Record<int,9,true>>::schema_nested(context);
	json::value third=json::converter<ValueModel::Record<int,3,false>>::schema_nested(context);
	json::value repeated=json::converter<ValueModel::Record<int,3,true>>::schema_nested(context);
	assert(first.get("$ref").as_string()!=second.get("$ref").as_string());
	assert(first.get("$ref").as_string()!=third.get("$ref").as_string());
	assert(first.equals(repeated));
	return 0;
}
