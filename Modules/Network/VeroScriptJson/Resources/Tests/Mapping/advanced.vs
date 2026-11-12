// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

enum class Role : uint8 { @JsonName(value="reader") Read=1u, Write=255u };
struct Base { public: int id=8; };
struct Profile : Base { public: Role role=Role::Read; @JsonDeserializeOnly int password=0; @JsonSerializeOnly int output=7; };
class Property { private: int stored=3; public: @JsonProperty(name="count",setter="set") int get() const { return stored; } void set(int n) { stored=n; } };
@JsonConverter(type="TokenAdapter") class Token { public: string text; Token(string n) { text=n; } };
class TokenAdapter { public:
 static bool encode(const Token& input,json::value& output,json::mapping_context context) { return context.write_string(input.text,ref output); }
 static bool decode(json::value input,Token& output,json::mapping_context context) { string text="";if(!context.read_string(input,ref text))return false;output=Token(text);return true; }
 static json::value schema(json::mapping_context context) { return json::parse("{\"type\":\"string\"}"); }
};
@JsonFactory(type="Factory") class Needs { public: int n; Needs(int value){n=value;} };
class Factory { public: static Needs create(json::value input,json::mapping_context context) { return Needs(2); } };
int main() {
 Profile p=Profile(); json::value encoded=json::converter<Profile>::write(ref p);
 assert(encoded.get("id").as_int64()==8 && encoded.get("role").as_string()=="reader" && !encoded.contains("password"));
 json::error error=json::error();
 assert(!json::converter<Profile>::try_read(encoded,ref p,ref error));assert(error.instance_pointer()=="/output");
 encoded.erase("output");
 Profile copy=json::converter<Profile>::read(encoded);assert(copy.role==Role::Read && copy.id==8);
 json::mapping_options options=json::mapping_options(); options.set_numeric_enums(true);
 encoded=json::converter<Profile>::write_with_options(ref p,options);assert(encoded.get("role").as_int64()==1);
 json::schema input_schema=json::compile_schema(json::converter<Profile>::schema());
 assert(input_schema.is_valid(json::parse("{\"role\":\"reader\",\"password\":9}")));
 assert(!input_schema.is_valid(json::parse("{\"output\":1}")));
 json::schema output_schema=json::compile_schema(json::converter<Profile>::schema_with_options(json::mapping_options(),false));
 Profile q=Profile();assert(output_schema.is_valid(json::converter<Profile>::write(ref q)));
 Property property=json::converter<Property>::read(json::parse("{\"count\":11}"));assert(property.get()==11);
 assert(json::converter<Property>::write(ref property).get("count").as_int64()==11);
 Token token=json::converter<Token>::read(json::from_string("token"));assert(token.text=="token");
 assert(json::converter<Token>::write(ref token).as_string()=="token");
 Needs needs=json::converter<Needs>::read(json::parse("{\"n\":5}"));assert(needs.n==5);
 bool found=true; json::value value=json::from_int(77);encoded=json::object();
 assert(encoded.try_get("missing",ref found,ref value,ref error) && !found && value.as_int64()==77);
 assert(!encoded.try_at(0,ref value,ref error) && value.as_int64()==77 && error.code()!=0);
 assert(encoded.try_set("key",json::from_bool(true),ref error) && error.code()==0);
 assert(encoded.try_get("key",ref found,ref value,ref error) && found && value.as_bool());
 return 0;
}
