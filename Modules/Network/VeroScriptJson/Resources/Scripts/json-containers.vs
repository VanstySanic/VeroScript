// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_encode(const set<T>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		array<T> items = input.values();
		return veroscript_json_mapping::mapping_encode<int>(ref items, ref output, context);
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_decode(json::value input, set<T>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		if (input.kind() != 4) return context.fail("Expected a JSON array");
		if (!context.enter(0u64)) return false;
		set<T> result = set<T>();
		for (int64 index = 0; index < input.size(); index++)
		{
			if (!context.push(to_string(index)))
			{
				context.leave();
				return false;
			}
			if (!veroscript_json_mapping::mapping_validate<int>(type<T>(), input.at(index), context))
			{
				context.pop();
				context.leave();
				return false;
			}
			T holder = veroscript_json_mapping::mapping_initial<int>(type<T>(), input.at(index), context);
			context.use_fresh(type<T>().qualified_name);
			if (!veroscript_json_mapping::mapping_decode<int>(input.at(index), ref holder, context))
			{
				context.pop();
				context.leave();
				return false;
			}
			if (result.has(holder))
			{
				context.fail("Duplicate JSON set element");
				context.pop();
				context.leave();
				return false;
			}
			result.add(holder);
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	json::value mapping_schema(type<set<T>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		json::value result = json::object();
		result.set("type", json::from_string("array"));
		result.set("items", veroscript_json_mapping::mapping_schema<int>(type<T>(), context));
		result.set("uniqueItems", json::from_bool(true));
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_encode(const multiset<T>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		array<T> items = input.values();
		return veroscript_json_mapping::mapping_encode<int>(ref items, ref output, context);
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_decode(json::value input, multiset<T>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		if (input.kind() != 4) return context.fail("Expected a JSON array");
		if (!context.enter(0u64)) return false;
		multiset<T> result = multiset<T>();
		for (int64 index = 0; index < input.size(); index++)
		{
			if (!context.push(to_string(index)))
			{
				context.leave();
				return false;
			}
			if (!veroscript_json_mapping::mapping_validate<int>(type<T>(), input.at(index), context))
			{
				context.pop();
				context.leave();
				return false;
			}
			T holder = veroscript_json_mapping::mapping_initial<int>(type<T>(), input.at(index), context);
			context.use_fresh(type<T>().qualified_name);
			if (!veroscript_json_mapping::mapping_decode<int>(input.at(index), ref holder, context))
			{
				context.pop();
				context.leave();
				return false;
			}
			result.add(holder);
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	json::value mapping_schema(type<multiset<T>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		json::value result = json::object();
		result.set("type", json::from_string("array"));
		result.set("items", veroscript_json_mapping::mapping_schema<int>(type<T>(), context));
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_encode(const ordered_multiset<T>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		array<T> items = input.values();
		return veroscript_json_mapping::mapping_encode<int>(ref items, ref output, context);
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_decode(json::value input, ordered_multiset<T>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		if (input.kind() != 4) return context.fail("Expected a JSON array");
		if (!context.enter(0u64)) return false;
		ordered_multiset<T> result = ordered_multiset<T>();
		for (int64 index = 0; index < input.size(); index++)
		{
			if (!context.push(to_string(index)))
			{
				context.leave();
				return false;
			}
			if (!veroscript_json_mapping::mapping_validate<int>(type<T>(), input.at(index), context))
			{
				context.pop();
				context.leave();
				return false;
			}
			T holder = veroscript_json_mapping::mapping_initial<int>(type<T>(), input.at(index), context);
			context.use_fresh(type<T>().qualified_name);
			if (!veroscript_json_mapping::mapping_decode<int>(input.at(index), ref holder, context))
			{
				context.pop();
				context.leave();
				return false;
			}
			result.add(holder);
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	json::value mapping_schema(type<ordered_multiset<T>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		json::value result = json::object();
		result.set("type", json::from_string("array"));
		result.set("items", veroscript_json_mapping::mapping_schema<int>(type<T>(), context));
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_encode(const queue<T>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		array<T> items = input.values();
		return veroscript_json_mapping::mapping_encode<int>(ref items, ref output, context);
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_decode(json::value input, queue<T>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		if (input.kind() != 4) return context.fail("Expected a JSON array");
		if (!context.enter(0u64)) return false;
		queue<T> result = queue<T>();
		for (int64 index = 0; index < input.size(); index++)
		{
			if (!context.push(to_string(index)))
			{
				context.leave();
				return false;
			}
			if (!veroscript_json_mapping::mapping_validate<int>(type<T>(), input.at(index), context))
			{
				context.pop();
				context.leave();
				return false;
			}
			T holder = veroscript_json_mapping::mapping_initial<int>(type<T>(), input.at(index), context);
			context.use_fresh(type<T>().qualified_name);
			if (!veroscript_json_mapping::mapping_decode<int>(input.at(index), ref holder, context))
			{
				context.pop();
				context.leave();
				return false;
			}
			result.push(holder);
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	json::value mapping_schema(type<queue<T>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		json::value result = json::object();
		result.set("type", json::from_string("array"));
		result.set("items", veroscript_json_mapping::mapping_schema<int>(type<T>(), context));
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_encode(const stack<T>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		array<T> items = input.values();
		return veroscript_json_mapping::mapping_encode<int>(ref items, ref output, context);
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_decode(json::value input, stack<T>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		if (input.kind() != 4) return context.fail("Expected a JSON array");
		if (!context.enter(0u64)) return false;
		stack<T> result = stack<T>();
		for (int64 index = 0; index < input.size(); index++)
		{
			if (!context.push(to_string(index)))
			{
				context.leave();
				return false;
			}
			if (!veroscript_json_mapping::mapping_validate<int>(type<T>(), input.at(index), context))
			{
				context.pop();
				context.leave();
				return false;
			}
			T holder = veroscript_json_mapping::mapping_initial<int>(type<T>(), input.at(index), context);
			context.use_fresh(type<T>().qualified_name);
			if (!veroscript_json_mapping::mapping_decode<int>(input.at(index), ref holder, context))
			{
				context.pop();
				context.leave();
				return false;
			}
			result.push(holder);
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	json::value mapping_schema(type<stack<T>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		json::value result = json::object();
		result.set("type", json::from_string("array"));
		result.set("items", veroscript_json_mapping::mapping_schema<int>(type<T>(), context));
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_encode(const priority_queue<T>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		array<T> items = input.values();
		return veroscript_json_mapping::mapping_encode<int>(ref items, ref output, context);
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_decode(json::value input, priority_queue<T>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		if (input.kind() != 4) return context.fail("Expected a JSON array");
		if (!context.enter(0u64)) return false;
		priority_queue<T> result = priority_queue<T>();
		for (int64 index = 0; index < input.size(); index++)
		{
			if (!context.push(to_string(index)))
			{
				context.leave();
				return false;
			}
			if (!veroscript_json_mapping::mapping_validate<int>(type<T>(), input.at(index), context))
			{
				context.pop();
				context.leave();
				return false;
			}
			T holder = veroscript_json_mapping::mapping_initial<int>(type<T>(), input.at(index), context);
			context.use_fresh(type<T>().qualified_name);
			if (!veroscript_json_mapping::mapping_decode<int>(input.at(index), ref holder, context))
			{
				context.pop();
				context.leave();
				return false;
			}
			result.push(holder);
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	json::value mapping_schema(type<priority_queue<T>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		json::value result = json::object();
		result.set("type", json::from_string("array"));
		result.set("items", veroscript_json_mapping::mapping_schema<int>(type<T>(), context));
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_encode(const deque<T>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		array<T> items = input.values();
		return veroscript_json_mapping::mapping_encode<int>(ref items, ref output, context);
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_decode(json::value input, deque<T>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		if (input.kind() != 4) return context.fail("Expected a JSON array");
		if (!context.enter(0u64)) return false;
		deque<T> result = deque<T>();
		for (int64 index = 0; index < input.size(); index++)
		{
			if (!context.push(to_string(index)))
			{
				context.leave();
				return false;
			}
			if (!veroscript_json_mapping::mapping_validate<int>(type<T>(), input.at(index), context))
			{
				context.pop();
				context.leave();
				return false;
			}
			T holder = veroscript_json_mapping::mapping_initial<int>(type<T>(), input.at(index), context);
			context.use_fresh(type<T>().qualified_name);
			if (!veroscript_json_mapping::mapping_decode<int>(input.at(index), ref holder, context))
			{
				context.pop();
				context.leave();
				return false;
			}
			result.push_back(holder);
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	json::value mapping_schema(type<deque<T>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		json::value result = json::object();
		result.set("type", json::from_string("array"));
		result.set("items", veroscript_json_mapping::mapping_schema<int>(type<T>(), context));
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_encode(const map<K, V>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || false)) return context.fail("This JSON map requires key_value_pairs");
		if (!context.enter(0u64)) return false;
		json::builder result = json::object_builder();
		if (pairs)
		{
			result = json::array_builder();
		}
		int64 index = 0;
		for (K key : input.keys())
		{
			V item = input[key];
			if (!context.push(pairs ? to_string(index) : to_string(key)))
			{
				context.leave();
				return false;
			}
			json::value encoded_key = json::value();
			json::value encoded_item = json::value();
			if (pairs && !context.push("key"))
			{
				context.pop();
				context.leave();
				return false;
			}
			bool success = veroscript_json_mapping::mapping_encode<int>(ref key, ref encoded_key, context);
			if (pairs)
			{
				context.pop();
			}
			if (success && pairs && !context.push("value"))
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success)
			{
				success = veroscript_json_mapping::mapping_encode<int>(ref item, ref encoded_item, context);
				if (pairs)
				{
					context.pop();
				}
			}
			context.pop();
			if (!success)
			{
				context.leave();
				return false;
			}
			if (pairs)
			{
				json::value entry = json::object();
				entry.set("key", encoded_key);
				entry.set("value", encoded_item);
				result.push(entry);
			}
			else
			{
				result.set(encoded_key.as_string(), encoded_item);
			}
			index++;
		}
		context.leave();
		output = result.finish();
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_decode(json::value input, map<K, V>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || false)) return context.fail("This JSON map requires key_value_pairs");
		if ((pairs && input.kind() != 4) || (!pairs && input.kind() != 5)) return context.fail("Unexpected JSON map representation");
		if (!context.enter(0u64)) return false;
		map<K, V> result = map<K, V>();
		json::value keys = json::value();
		if (!pairs)
		{
			keys = input.keys();
		}
		for (int64 index = 0; index < input.size(); index++)
		{
			string location = to_string(index);
			if (!pairs)
			{
				location = keys.at(index).as_string();
			}
			if (!context.push(location))
			{
				context.leave();
				return false;
			}
			json::value key_input = json::from_string(location);
			json::value item_input = json::value();
			if (pairs)
			{
				json::value entry = input.at(index);
				if (entry.kind() != 5 || !entry.contains("key") || !entry.contains("value") || entry.size() != 2)
				{
					context.fail("Expected a JSON key/value entry");
					context.pop();
					context.leave();
					return false;
				}
				key_input = entry.get("key");
				item_input = entry.get("value");
			}
			else
			{
				item_input = input.get(location);
			}

			if (pairs && !context.push("key"))
			{
				context.pop();
				context.leave();
				return false;
			}
			bool success = veroscript_json_mapping::mapping_validate<int>(type<K>(), key_input, context);
			if (!success)
			{
				if (pairs)
				{
					context.pop();
				}
				context.pop();
				context.leave();
				return false;
			}
			K key = veroscript_json_mapping::mapping_initial<int>(type<K>(), key_input, context);
			context.use_fresh(type<K>().qualified_name);
			success = veroscript_json_mapping::mapping_decode<int>(key_input, ref key, context);
			if (pairs)
			{
				context.pop();
			}
			if (!success)
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success && pairs && !context.push("value"))
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success)
			{
				success = veroscript_json_mapping::mapping_validate<int>(type<V>(), item_input, context);
			}
			if (!success)
			{
				if (pairs)
				{
					context.pop();
				}
				context.pop();
				context.leave();
				return false;
			}
			V item = veroscript_json_mapping::mapping_initial<int>(type<V>(), item_input, context);
			context.use_fresh(type<V>().qualified_name);
			success = veroscript_json_mapping::mapping_decode<int>(item_input, ref item, context);
			if (pairs)
			{
				context.pop();
			}
			if (!success)
			{
				context.pop();
				context.leave();
				return false;
			}
			if (result.has(key))
			{
				context.fail("Duplicate JSON map key");
				context.pop();
				context.leave();
				return false;
			}
			result[key] = item;
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	json::value mapping_schema(type<map<K, V>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || false))
		{
			context.require(context.fail("This JSON map requires key_value_pairs"));
		}
		json::value result = json::object();
		json::value item = veroscript_json_mapping::mapping_schema<int>(type<V>(), context);
		if (!pairs)
		{
			result.set("type", json::from_string("object"));
			result.set("additionalProperties", item);
			return result;
		}
		result.set("type", json::from_string("array"));
		json::value entry = json::parse("{\"type\":\"object\",\"required\":[\"key\",\"value\"],\"additionalProperties\":false}");
		json::value properties = json::object();
		properties.set("key", veroscript_json_mapping::mapping_schema<int>(type<K>(), context));
		properties.set("value", item);
		entry.set("properties", properties);
		result.set("items", entry);
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_encode(const ordered_map<K, V>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || false)) return context.fail("This JSON map requires key_value_pairs");
		if (!context.enter(0u64)) return false;
		json::builder result = json::object_builder();
		if (pairs)
		{
			result = json::array_builder();
		}
		int64 index = 0;
		for (K key : input.keys())
		{
			V item = input[key];
			if (!context.push(pairs ? to_string(index) : to_string(key)))
			{
				context.leave();
				return false;
			}
			json::value encoded_key = json::value();
			json::value encoded_item = json::value();
			if (pairs && !context.push("key"))
			{
				context.pop();
				context.leave();
				return false;
			}
			bool success = veroscript_json_mapping::mapping_encode<int>(ref key, ref encoded_key, context);
			if (pairs)
			{
				context.pop();
			}
			if (success && pairs && !context.push("value"))
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success)
			{
				success = veroscript_json_mapping::mapping_encode<int>(ref item, ref encoded_item, context);
				if (pairs)
				{
					context.pop();
				}
			}
			context.pop();
			if (!success)
			{
				context.leave();
				return false;
			}
			if (pairs)
			{
				json::value entry = json::object();
				entry.set("key", encoded_key);
				entry.set("value", encoded_item);
				result.push(entry);
			}
			else
			{
				result.set(encoded_key.as_string(), encoded_item);
			}
			index++;
		}
		context.leave();
		output = result.finish();
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_decode(json::value input, ordered_map<K, V>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || false)) return context.fail("This JSON map requires key_value_pairs");
		if ((pairs && input.kind() != 4) || (!pairs && input.kind() != 5)) return context.fail("Unexpected JSON map representation");
		if (!context.enter(0u64)) return false;
		ordered_map<K, V> result = ordered_map<K, V>();
		json::value keys = json::value();
		if (!pairs)
		{
			keys = input.keys();
		}
		for (int64 index = 0; index < input.size(); index++)
		{
			string location = to_string(index);
			if (!pairs)
			{
				location = keys.at(index).as_string();
			}
			if (!context.push(location))
			{
				context.leave();
				return false;
			}
			json::value key_input = json::from_string(location);
			json::value item_input = json::value();
			if (pairs)
			{
				json::value entry = input.at(index);
				if (entry.kind() != 5 || !entry.contains("key") || !entry.contains("value") || entry.size() != 2)
				{
					context.fail("Expected a JSON key/value entry");
					context.pop();
					context.leave();
					return false;
				}
				key_input = entry.get("key");
				item_input = entry.get("value");
			}
			else
			{
				item_input = input.get(location);
			}

			if (pairs && !context.push("key"))
			{
				context.pop();
				context.leave();
				return false;
			}
			bool success = veroscript_json_mapping::mapping_validate<int>(type<K>(), key_input, context);
			if (!success)
			{
				if (pairs)
				{
					context.pop();
				}
				context.pop();
				context.leave();
				return false;
			}
			K key = veroscript_json_mapping::mapping_initial<int>(type<K>(), key_input, context);
			context.use_fresh(type<K>().qualified_name);
			success = veroscript_json_mapping::mapping_decode<int>(key_input, ref key, context);
			if (pairs)
			{
				context.pop();
			}
			if (!success)
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success && pairs && !context.push("value"))
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success)
			{
				success = veroscript_json_mapping::mapping_validate<int>(type<V>(), item_input, context);
			}
			if (!success)
			{
				if (pairs)
				{
					context.pop();
				}
				context.pop();
				context.leave();
				return false;
			}
			V item = veroscript_json_mapping::mapping_initial<int>(type<V>(), item_input, context);
			context.use_fresh(type<V>().qualified_name);
			success = veroscript_json_mapping::mapping_decode<int>(item_input, ref item, context);
			if (pairs)
			{
				context.pop();
			}
			if (!success)
			{
				context.pop();
				context.leave();
				return false;
			}
			if (result.has(key))
			{
				context.fail("Duplicate JSON map key");
				context.pop();
				context.leave();
				return false;
			}
			result[key] = item;
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	json::value mapping_schema(type<ordered_map<K, V>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || false))
		{
			context.require(context.fail("This JSON map requires key_value_pairs"));
		}
		json::value result = json::object();
		json::value item = veroscript_json_mapping::mapping_schema<int>(type<V>(), context);
		if (!pairs)
		{
			result.set("type", json::from_string("object"));
			result.set("additionalProperties", item);
			return result;
		}
		result.set("type", json::from_string("array"));
		json::value entry = json::parse("{\"type\":\"object\",\"required\":[\"key\",\"value\"],\"additionalProperties\":false}");
		json::value properties = json::object();
		properties.set("key", veroscript_json_mapping::mapping_schema<int>(type<K>(), context));
		properties.set("value", item);
		entry.set("properties", properties);
		result.set("items", entry);
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_encode(const multimap<K, V>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || true)) return context.fail("This JSON map requires key_value_pairs");
		if (!context.enter(0u64)) return false;
		json::builder result = json::object_builder();
		if (pairs)
		{
			result = json::array_builder();
		}
		int64 index = 0;
		for (K key : input.keys())
		{
			for (V item : input[key])
			{
				if (!context.push(pairs ? to_string(index) : to_string(key)))
				{
					context.leave();
					return false;
				}
				json::value encoded_key = json::value();
				json::value encoded_item = json::value();
				if (pairs && !context.push("key"))
				{
					context.pop();
					context.leave();
					return false;
				}
				bool success = veroscript_json_mapping::mapping_encode<int>(ref key, ref encoded_key, context);
				if (pairs)
				{
					context.pop();
				}
				if (success && pairs && !context.push("value"))
				{
					context.pop();
					context.leave();
					return false;
				}
				if (success)
				{
					success = veroscript_json_mapping::mapping_encode<int>(ref item, ref encoded_item, context);
					if (pairs)
					{
						context.pop();
					}
				}
				context.pop();
				if (!success)
				{
					context.leave();
					return false;
				}
				if (pairs)
				{
					json::value entry = json::object();
					entry.set("key", encoded_key);
					entry.set("value", encoded_item);
					result.push(entry);
				}
				else
				{
					result.set(encoded_key.as_string(), encoded_item);
				}
				index++;
			}
		}
		context.leave();
		output = result.finish();
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_decode(json::value input, multimap<K, V>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || true)) return context.fail("This JSON map requires key_value_pairs");
		if ((pairs && input.kind() != 4) || (!pairs && input.kind() != 5)) return context.fail("Unexpected JSON map representation");
		if (!context.enter(0u64)) return false;
		multimap<K, V> result = multimap<K, V>();
		json::value keys = json::value();
		if (!pairs)
		{
			keys = input.keys();
		}
		for (int64 index = 0; index < input.size(); index++)
		{
			string location = to_string(index);
			if (!pairs)
			{
				location = keys.at(index).as_string();
			}
			if (!context.push(location))
			{
				context.leave();
				return false;
			}
			json::value key_input = json::from_string(location);
			json::value item_input = json::value();
			if (pairs)
			{
				json::value entry = input.at(index);
				if (entry.kind() != 5 || !entry.contains("key") || !entry.contains("value") || entry.size() != 2)
				{
					context.fail("Expected a JSON key/value entry");
					context.pop();
					context.leave();
					return false;
				}
				key_input = entry.get("key");
				item_input = entry.get("value");
			}
			else
			{
				item_input = input.get(location);
			}

			if (pairs && !context.push("key"))
			{
				context.pop();
				context.leave();
				return false;
			}
			bool success = veroscript_json_mapping::mapping_validate<int>(type<K>(), key_input, context);
			if (!success)
			{
				if (pairs)
				{
					context.pop();
				}
				context.pop();
				context.leave();
				return false;
			}
			K key = veroscript_json_mapping::mapping_initial<int>(type<K>(), key_input, context);
			context.use_fresh(type<K>().qualified_name);
			success = veroscript_json_mapping::mapping_decode<int>(key_input, ref key, context);
			if (pairs)
			{
				context.pop();
			}
			if (!success)
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success && pairs && !context.push("value"))
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success)
			{
				success = veroscript_json_mapping::mapping_validate<int>(type<V>(), item_input, context);
			}
			if (!success)
			{
				if (pairs)
				{
					context.pop();
				}
				context.pop();
				context.leave();
				return false;
			}
			V item = veroscript_json_mapping::mapping_initial<int>(type<V>(), item_input, context);
			context.use_fresh(type<V>().qualified_name);
			success = veroscript_json_mapping::mapping_decode<int>(item_input, ref item, context);
			if (pairs)
			{
				context.pop();
			}
			if (!success)
			{
				context.pop();
				context.leave();
				return false;
			}
			result.add(key, item);
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	json::value mapping_schema(type<multimap<K, V>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || true))
		{
			context.require(context.fail("This JSON map requires key_value_pairs"));
		}
		json::value result = json::object();
		json::value item = veroscript_json_mapping::mapping_schema<int>(type<V>(), context);
		if (!pairs)
		{
			result.set("type", json::from_string("object"));
			result.set("additionalProperties", item);
			return result;
		}
		result.set("type", json::from_string("array"));
		json::value entry = json::parse("{\"type\":\"object\",\"required\":[\"key\",\"value\"],\"additionalProperties\":false}");
		json::value properties = json::object();
		properties.set("key", veroscript_json_mapping::mapping_schema<int>(type<K>(), context));
		properties.set("value", item);
		entry.set("properties", properties);
		result.set("items", entry);
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_encode(const ordered_multimap<K, V>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || true)) return context.fail("This JSON map requires key_value_pairs");
		if (!context.enter(0u64)) return false;
		json::builder result = json::object_builder();
		if (pairs)
		{
			result = json::array_builder();
		}
		int64 index = 0;
		for (K key : input.keys())
		{
			for (V item : input[key])
			{
				if (!context.push(pairs ? to_string(index) : to_string(key)))
				{
					context.leave();
					return false;
				}
				json::value encoded_key = json::value();
				json::value encoded_item = json::value();
				if (pairs && !context.push("key"))
				{
					context.pop();
					context.leave();
					return false;
				}
				bool success = veroscript_json_mapping::mapping_encode<int>(ref key, ref encoded_key, context);
				if (pairs)
				{
					context.pop();
				}
				if (success && pairs && !context.push("value"))
				{
					context.pop();
					context.leave();
					return false;
				}
				if (success)
				{
					success = veroscript_json_mapping::mapping_encode<int>(ref item, ref encoded_item, context);
					if (pairs)
					{
						context.pop();
					}
				}
				context.pop();
				if (!success)
				{
					context.leave();
					return false;
				}
				if (pairs)
				{
					json::value entry = json::object();
					entry.set("key", encoded_key);
					entry.set("value", encoded_item);
					result.push(entry);
				}
				else
				{
					result.set(encoded_key.as_string(), encoded_item);
				}
				index++;
			}
		}
		context.leave();
		output = result.finish();
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_decode(json::value input, ordered_multimap<K, V>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || true)) return context.fail("This JSON map requires key_value_pairs");
		if ((pairs && input.kind() != 4) || (!pairs && input.kind() != 5)) return context.fail("Unexpected JSON map representation");
		if (!context.enter(0u64)) return false;
		ordered_multimap<K, V> result = ordered_multimap<K, V>();
		json::value keys = json::value();
		if (!pairs)
		{
			keys = input.keys();
		}
		for (int64 index = 0; index < input.size(); index++)
		{
			string location = to_string(index);
			if (!pairs)
			{
				location = keys.at(index).as_string();
			}
			if (!context.push(location))
			{
				context.leave();
				return false;
			}
			json::value key_input = json::from_string(location);
			json::value item_input = json::value();
			if (pairs)
			{
				json::value entry = input.at(index);
				if (entry.kind() != 5 || !entry.contains("key") || !entry.contains("value") || entry.size() != 2)
				{
					context.fail("Expected a JSON key/value entry");
					context.pop();
					context.leave();
					return false;
				}
				key_input = entry.get("key");
				item_input = entry.get("value");
			}
			else
			{
				item_input = input.get(location);
			}

			if (pairs && !context.push("key"))
			{
				context.pop();
				context.leave();
				return false;
			}
			bool success = veroscript_json_mapping::mapping_validate<int>(type<K>(), key_input, context);
			if (!success)
			{
				if (pairs)
				{
					context.pop();
				}
				context.pop();
				context.leave();
				return false;
			}
			K key = veroscript_json_mapping::mapping_initial<int>(type<K>(), key_input, context);
			context.use_fresh(type<K>().qualified_name);
			success = veroscript_json_mapping::mapping_decode<int>(key_input, ref key, context);
			if (pairs)
			{
				context.pop();
			}
			if (!success)
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success && pairs && !context.push("value"))
			{
				context.pop();
				context.leave();
				return false;
			}
			if (success)
			{
				success = veroscript_json_mapping::mapping_validate<int>(type<V>(), item_input, context);
			}
			if (!success)
			{
				if (pairs)
				{
					context.pop();
				}
				context.pop();
				context.leave();
				return false;
			}
			V item = veroscript_json_mapping::mapping_initial<int>(type<V>(), item_input, context);
			context.use_fresh(type<V>().qualified_name);
			success = veroscript_json_mapping::mapping_decode<int>(item_input, ref item, context);
			if (pairs)
			{
				context.pop();
			}
			if (!success)
			{
				context.pop();
				context.leave();
				return false;
			}
			result.add(key, item);
			context.pop();
		}
		context.leave();
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	json::value mapping_schema(type<ordered_multimap<K, V>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		bool pairs = context.key_value_pairs();
		if (!pairs && (type<K>().qualified_name != "string" || true))
		{
			context.require(context.fail("This JSON map requires key_value_pairs"));
		}
		json::value result = json::object();
		json::value item = veroscript_json_mapping::mapping_schema<int>(type<V>(), context);
		if (!pairs)
		{
			result.set("type", json::from_string("object"));
			result.set("additionalProperties", item);
			return result;
		}
		result.set("type", json::from_string("array"));
		json::value entry = json::parse("{\"type\":\"object\",\"required\":[\"key\",\"value\"],\"additionalProperties\":false}");
		json::value properties = json::object();
		properties.set("key", veroscript_json_mapping::mapping_schema<int>(type<K>(), context));
		properties.set("value", item);
		entry.set("properties", properties);
		result.set("items", entry);
		return result;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	array<T> mapping_initial(type<array<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return array<T>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_validate(type<array<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	set<T> mapping_initial(type<set<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return set<T>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_validate(type<set<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	multiset<T> mapping_initial(type<multiset<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return multiset<T>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_validate(type<multiset<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	ordered_multiset<T> mapping_initial(type<ordered_multiset<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return ordered_multiset<T>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_validate(type<ordered_multiset<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	queue<T> mapping_initial(type<queue<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return queue<T>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_validate(type<queue<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	stack<T> mapping_initial(type<stack<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return stack<T>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_validate(type<stack<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	priority_queue<T> mapping_initial(type<priority_queue<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return priority_queue<T>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_validate(type<priority_queue<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	deque<T> mapping_initial(type<deque<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return deque<T>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_validate(type<deque<T>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	map<K, V> mapping_initial(type<map<K, V>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return map<K, V>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_validate(type<map<K, V>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	ordered_map<K, V> mapping_initial(type<ordered_map<K, V>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return ordered_map<K, V>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_validate(type<ordered_map<K, V>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	multimap<K, V> mapping_initial(type<multimap<K, V>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return multimap<K, V>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_validate(type<multimap<K, V>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	ordered_multimap<K, V> mapping_initial(type<ordered_multimap<K, V>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return ordered_multimap<K, V>();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, K = int, V = int>
	bool mapping_validate(type<ordered_multimap<K, V>> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int>
	json::value mapping_initial(type<json::value> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return json::value();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int>
	bool mapping_validate(type<json::value> tag, json::value input, json::mapping_context context, JsonGuard guard = 0)
	{
		return true;
	}
}
