// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

struct Containers {
 public:
 map<string,int> fields=map<string,int>(); ordered_map<string,int> ordered=ordered_map<string,int>();
 set<int> unique=set<int>(); multiset<int> bag=multiset<int>(); ordered_multiset<int> ordered_bag=ordered_multiset<int>();
 queue<int> queue_values=queue<int>(); stack<int> stack_values=stack<int>();priority_queue<int> priority=priority_queue<int>();deque<int> deque_values=deque<int>();
};
int main() {
 Containers c=Containers(); c.fields["a"]=1;c.ordered["b"]=2;c.unique.add(3);c.bag.add(4);c.bag.add(4);c.ordered_bag.add(5);c.queue_values.push(6);c.queue_values.push(7);c.stack_values.push(8);c.stack_values.push(9);c.priority.push(2);c.priority.push(11);c.deque_values.push_back(12);c.deque_values.push_front(10);
 json::value data=json::converter<Containers>::write(ref c);
 Containers d=json::converter<Containers>::read(data);
 assert(d.fields["a"]==1 && d.ordered["b"]==2 && d.unique.has(3) && d.bag.count(4)==2 && d.ordered_bag.count(5)==1);
 assert(d.queue_values.pop()==6 && d.queue_values.pop()==7 && d.stack_values.pop()==9 && d.stack_values.pop()==8 && d.priority.pop()==11 && d.deque_values.pop_front()==10 && d.deque_values.pop_back()==12);
 assert(len(c.queue_values)==2 && len(c.stack_values)==2 && len(c.priority)==2);
 json::schema schema=json::compile_schema(json::converter<Containers>::schema());assert(schema.is_valid(data));
 json::mapping_options options=json::mapping_options();options.set_key_value_pairs(true);
 ordered_map<int,string> mapping=ordered_map<int,string>();mapping[7]="seven";
 data=json::converter<ordered_map<int,string>>::write_with_options(ref mapping,options);
 assert(data.at(0).get("key").as_int64()==7);
 ordered_map<int,string> result=json::converter<ordered_map<int,string>>::read_with_options(data,options);assert(result[7]=="seven");
 json::error error=json::error();data=json::parse("[{\"key\":7,\"value\":\"first\"},{\"key\":7,\"value\":\"second\"}]");
 assert(!json::converter<ordered_map<int,string>>::try_read_with_options(data,ref result,ref error,options) && result[7]=="seven" && error.instance_pointer()=="/1");
 ordered_multimap<int,string> multiple=json::converter<ordered_multimap<int,string>>::read_with_options(data,options);
 assert(len(multiple[7])==2 && multiple[7][1]=="second");
 data=json::converter<ordered_multimap<int,string>>::write_with_options(ref multiple,options);assert(data.size()==2);
 return 0;
}
