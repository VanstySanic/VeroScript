// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

import "Fixtures/polymorphic_model.vs";
namespace Shapes {
class Base<T> {public: T value;};
class Box<T> : public Base<T> {public: string label="box";};
using IntegerBox=Box<int>;
@JsonPolymorphic(base="Base<int>",discriminator="type/id",payload="body~",variants=(box=(type="IntegerBox",tag="方块")))
class Table {};
}
int main() {
 Shapes::Base<int> original=Shapes::Box<int>();original.value=31;
 json::value data=json::custom_converter<Shapes::Base<int>,Shapes::Table>::write(ref original);
 assert(data.get("type/id").as_string()=="方块" && data.get("body~").get("value").as_int64()==31);
 Shapes::Base<int> copy=json::custom_converter<Shapes::Base<int>,Shapes::Table>::read(data);
 assert(copy.value==31);
 json::schema schema=json::compile_schema(json::custom_converter<Shapes::Base<int>,Shapes::Table>::schema());
 assert(schema.is_valid(data));
 json::mapping_options options=json::mapping_options();options.set_ignore_unknown(true);
 data.set("extra",json::from_int(0));
 json::error error=json::error();
 assert(!json::custom_converter<Shapes::Base<int>,Shapes::Table>::try_read(data,ref copy,ref error) && error.instance_pointer()=="/extra");
 copy=json::custom_converter<Shapes::Base<int>,Shapes::Table>::read_with_options(data,options);
 assert(copy.value==31);
 json::schema permissive=json::compile_schema(json::custom_converter<Shapes::Base<int>,Shapes::Table>::schema_with_options(options,true));
 json::schema output=json::compile_schema(json::custom_converter<Shapes::Base<int>,Shapes::Table>::schema_with_options(options,false));
 assert(permissive.is_valid(data) && !output.is_valid(data));
 data.erase("extra");
 data.set("type/id",json::from_int(4));
 assert(!json::custom_converter<Shapes::Base<int>,Shapes::Table>::try_read(data,ref copy,ref error) && error.instance_pointer()=="/type~1id");
 ImportedBase imported=ImportedChild();
 json::value external=json::converter<ImportedBase>::write(ref imported);
 ImportedBase restored=json::converter<ImportedBase>::read(external);
 assert(restored.marker==12 && external.get("kind").as_string()=="child");
 json::schema imported_schema=json::compile_schema(json::converter<ImportedBase>::schema());
 assert(imported_schema.is_valid(external));
 return 0;
}
