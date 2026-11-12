// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

int main() {
 json::error error=json::error();json::builder builder=json::object_builder();json::value output=json::from_int(99);
 assert(!builder.try_push(json::from_int(1),ref error) && error.code()!=0);
 assert(!builder.try_reserve(-1,ref error) && error.code()!=0);
 assert(builder.try_set("key",json::from_bool(true),ref error) && error.code()==0);
 int64 count=77;assert(builder.try_size(ref count,ref error) && count==1);
 assert(builder.try_capacity(ref count,ref error) && count>=1);
 assert(builder.try_finish(ref output,ref error) && output.get("key").as_bool());
 json::value saved=output;assert(!builder.try_finish(ref output,ref error) && output.equals(saved));
 assert(builder.try_reset_array(ref error) && builder.try_push(json::from_int(3),ref error));
 assert(builder.try_finish(ref output,ref error) && output.at(0).as_int64()==3);
 bool contains=true;assert(saved.try_contains("absent",ref contains,ref error) && !contains && error.code()==0);
 assert(!output.try_contains("key",ref contains,ref error) && !contains && error.code()!=0);
 json::value keys=json::from_int(5);assert(saved.try_keys(ref keys,ref error) && keys.at(0).as_string()=="key");
 saved=keys;assert(!json::from_int(2).try_keys(ref keys,ref error) && keys.equals(saved));

 json::schema schema=json::schema();json::schema_options options=json::schema_options();
 json::value resources=json::object();resources.set("urn:record",json::parse("{\"type\":\"integer\"}"));
 assert(json::try_compile_schema_with_options(json::parse("{\"$ref\":\"urn:record\"}"),options,resources,ref schema,ref error));
 assert(schema.is_valid(json::from_int(7)));
 assert(!json::try_compile_schema_with_options(json::from_bool(true),options,json::array(),ref schema,ref error));
 assert(!schema.is_valid(json::from_string("bad")) && error.code()!=0);
 json::schema_report report=schema.validate(json::from_int(1));json::limits limits=json::limits();
 assert(json::try_validate_schema_with_limits(schema,json::from_string("bad"),2,limits,ref report,ref error) && !report.valid());
 assert(!json::try_validate_schema_with_limits(schema,json::from_int(1),0,limits,ref report,ref error) && !report.valid() && error.code()!=0);

 json::document document=json::document();document.set(json::parse("{\"x\":17}"));
 json::pointer pointer=json::pointer();assert(json::try_compile_pointer("/x",false,ref pointer,ref error));
 assert(!json::try_compile_pointer("/bad~",false,ref pointer,ref error) && error.code()!=0);
 assert(pointer.try_resolve(document,ref output,ref error) && output.as_int64()==17);
 json::limits shallow=json::limits();shallow.set_depth(0);
 assert(!json::try_compile_pointer_with_limits("/x",false,ref pointer,ref error,shallow));
 assert(pointer.try_resolve_with_limits(document,ref output,ref error,limits) && output.as_int64()==17);
 document=json::patch(document,json::parse("[{\"op\":\"remove\",\"path\":\"\"}]"));
 assert(!document.try_get(ref output,ref error) && output.as_int64()==17);
 uint64 unsigned_number=0u64;assert(json::try_as_uint64(json::from_number("18446744073709551615"),ref unsigned_number,ref error) && unsigned_number==18446744073709551615u64);
 assert(json::try_from_string("text",ref output,ref error) && output.as_string()=="text");
 assert(json::try_from_double(0.5,ref output,ref error) && output.as_double()==0.5);
 return 0;
}
