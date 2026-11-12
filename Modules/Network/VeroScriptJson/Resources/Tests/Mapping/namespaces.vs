// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace ModelSpace {
using Count=int64;
struct Child { public: Count count=9; };
@JsonFactory(type="LocalFactory") class Record { public: Child child; Record(int unused){} };
class LocalFactory { public: static Record create(json::value input,json::mapping_context context) { return Record(0); } };
}
int main() {
 ModelSpace::Record record=json::converter<ModelSpace::Record>::read(json::parse("{\"child\":{\"count\":27}}"));
 assert(record.child.count==27);
 json::value data=json::converter<ModelSpace::Record>::write(ref record);
 assert(data.get("child").get("count").as_int64()==27);
 json::schema schema=json::compile_schema(json::converter<ModelSpace::Record>::schema());assert(schema.is_valid(data));
 return 0;
}
