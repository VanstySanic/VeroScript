// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

struct Record { public: int id=4; array<string> tags=array<string>{"one","two"}; };
struct Box<T> { public: T item; };
class Link { public: int id=8; Link next; };
struct Pair { public: Link left; Link right; };
int main() {
 json::error error=json::error();
 int number=42;
 assert(json::converter<int>::write(ref number).as_int64()==42);
 assert(json::converter<int>::read(json::from_int(9))==9);
 Record value=Record();
 json::value data=json::converter<Record>::write(ref value);
 assert(data.get("tags").at(1).as_string()=="two");
 Record copy=json::converter<Record>::read(data);
 assert(copy.tags[0]=="one");
 assert(!json::converter<Record>::try_read(json::parse("{\"id\":\"oops\"}"),ref copy,ref error));
 assert(copy.id==4 && len(copy.tags)==2 && error.instance_pointer()=="/id");
 assert(!json::converter<Record>::try_read(json::parse("{\"extra\":0}"),ref copy,ref error));
 assert(error.instance_pointer()=="/extra");
 array<int> numbers=array<int>{1,2,3};
 assert(json::converter<array<int>>::write(ref numbers).at(2).as_int64()==3);
 array<int> nc=json::converter<array<int>>::read(json::parse("[4,5]"));
 assert(nc[1]==5);
 Box<string> box=Box<string>(); box.item="box";
 assert(json::converter<Box<string>>::write(ref box).get("item").as_string()=="box");
 Link link=Link(); link.next=link;
 json::value unchanged=json::from_int(77);
 assert(!json::converter<Link>::try_write(ref link,ref unchanged,ref error));
 assert(error.message_id()=="json.cycle" && error.instance_pointer()=="/next" && unchanged.as_int64()==77);
 link.next=nullptr;
 Pair pair=Pair();pair.left=link;pair.right=link;
 assert(json::converter<Pair>::write(ref pair).get("right").get("id").as_int64()==8);

 json::schema shape=json::compile_schema(json::converter<Record>::schema());
 assert(shape.is_valid(data));
 assert(!shape.is_valid(json::parse("{\"id\":2147483648}")));
 assert(!shape.is_valid(json::parse("{\"extra\":0}")));
 json::schema recursive=json::compile_schema(json::converter<Link>::schema());
 assert(recursive.is_valid(json::parse("{\"id\":1,\"next\":{\"next\":null}}")));
 return 0;
}

