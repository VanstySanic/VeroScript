// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

class TextAdapter { public: static json::value create(json::value input,json::mapping_context context){return json::value();}
 static bool encode(const json::value& input,json::value& output,json::mapping_context context) { return context.write_string(input.write(),ref output); }
 static bool decode(json::value input,json::value& output,json::mapping_context context) { string text="";if(!context.read_string(input,ref text))return false;json::error error=json::error();if(!json::try_parse(text,ref output,ref error))return context.adopt(error);return true; }
 static json::value schema(json::mapping_context context) { return json::parse("{\"type\":\"string\"}"); }
};
enum class Alias {First=1,Second=1,Last=2};
int constructed=0;
@JsonFactory(type="StructFactory") struct Required {public: int n;Required(int value){constructed++;n=value;} };
class StructFactory {public: static Required create(json::value input,json::mapping_context context) {return Required(7);} };
struct GenericBase<T> { public: T payload; };
struct Child: GenericBase<int> { public: int own=3; };
int main() {
 json::value source=json::parse("{\"n\":2}");
 json::value encoded=json::custom_converter<json::value,TextAdapter>::write(ref source);
 assert(encoded.as_string()=="{\"n\":2}");
 json::value copy=json::custom_converter<json::value,TextAdapter>::read(encoded);assert(copy.equals(source));
 Required required=json::converter<Required>::read(source);assert(required.n==2 && constructed==1);
 array<Required> records=json::converter<array<Required>>::read(json::parse("[{\"n\":3},{\"n\":4}]"));assert(len(records)==2 && records[1].n==4 && constructed==3);
 Child child=Child();child.payload=11;
 encoded=json::converter<Child>::write(ref child);assert(encoded.get("payload").as_int64()==11);
 Child restored=json::converter<Child>::read(encoded);assert(restored.payload==11 && restored.own==3);
 json::schema schema=json::compile_schema(json::converter<Child>::schema());assert(schema.is_valid(encoded));

 json::mapping_options numeric=json::mapping_options();numeric.set_numeric_enums(true);
 json::schema enum_schema=json::compile_schema(json::converter<Alias>::schema_with_options(numeric,true));assert(enum_schema.is_valid(json::from_int(1)));
 json::schema alias_input=json::compile_schema(json::converter<Alias>::schema());assert(alias_input.is_valid(json::from_string("Second")));
 json::schema alias_output=json::compile_schema(json::converter<Alias>::schema_with_options(json::mapping_options(),false));assert(!alias_output.is_valid(json::from_string("Second")));
 return 0;
}
