// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Embedded declarations retain their normal compiler phase.
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int>
	bool mapping_encode(const json::value & input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		output = input;
		return context.visit();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int>
	bool mapping_decode(json::value input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		output = input;
		return context.visit();
	}
}
// @json-declaration:types
namespace json
{
	class converter<T>
	{
	public:
		static bool encode(const T& input, json::value & output, json::mapping_context context)
		{
			json::value candidate = json::value();
			bool success = context.callback_result(veroscript_json_mapping::mapping_encode<int>(ref input, ref candidate, context));
			if (success)
			{
				output = candidate;
			}
			return success;
		}
		static bool decode(json::value input, T & output, json::mapping_context context)
		{
			return context.callback_result(veroscript_json_mapping::mapping_decode<int>(input, ref output, context));
		}
		static json::value schema_nested(json::mapping_context context)
		{
			return veroscript_json_mapping::mapping_schema<int>(type<T>(), context);
		}
		static bool try_write_with_options(const T& input, json::value & output, json::error & error, json::mapping_options options)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			json::value candidate = json::value();
			bool success = context.callback_result(veroscript_json_mapping::mapping_encode<int>(ref input, ref candidate, context));
			error = context.error();
			if (success)
			{
				output = candidate;
			}
			return success;
		}
		static bool try_write(const T& input, json::value & output, json::error & error)
		{
			return json::converter<T>::try_write_with_options(ref input, ref output, ref error, json::mapping_options());
		}
		static json::value write_with_options(const T& input, json::mapping_options options)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			json::value output = json::value();
			context.require(veroscript_json_mapping::mapping_encode<int>(ref input, ref output, context));
			return output;
		}
		static json::value write(const T& input)
		{
			return json::converter<T>::write_with_options(ref input, json::mapping_options());
		}
		static bool try_read_with_options(json::value input, T & output, json::error & error, json::mapping_options options)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			bool success = context.callback_result(veroscript_json_mapping::mapping_decode<int>(input, ref output, context));
			error = context.error();
			return success;
		}
		static bool try_read(json::value input, T & output, json::error & error)
		{
			return json::converter<T>::try_read_with_options(input, ref output, ref error, json::mapping_options());
		}
		static T read_with_options(json::value input, json::mapping_options options)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			context.require(veroscript_json_mapping::mapping_validate<int>(type<T>(), input, context));
			T result = veroscript_json_mapping::mapping_initial<int>(type<T>(), input, context);
			context.use_fresh(type<T>().qualified_name);
			context.require(veroscript_json_mapping::mapping_decode<int>(input, ref result, context));
			return result;
		}
		static T read(json::value input)
		{
			return json::converter<T>::read_with_options(input, json::mapping_options());
		}
		static json::value schema_with_options(json::mapping_options options, bool input)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			context.set_schema_input(input);
			return context.schema_finish(veroscript_json_mapping::mapping_schema<int>(type<T>(), context));
		}
		static json::value schema()
		{
			return json::converter<T>::schema_with_options(json::mapping_options(), true);
		}
	};
}
// @json-declaration:types
namespace json
{
	class custom_converter<T, Adapter>
	{
	private:
		function(Adapter) -> void converter_signature;
	public:
		static bool encode(const T& input, json::value & output, json::mapping_context context)
		{
			json::value candidate = json::value();
			bool success = context.callback_result(Adapter::encode(ref input, ref candidate, context));
			if (success)
			{
				output = candidate;
			}
			return success;
		}
		static bool decode(json::value input, T & output, json::mapping_context context)
		{
			T candidate = Adapter::create(input, context);
			bool success = context.callback_result(Adapter::decode(input, ref candidate, context));
			if (success)
			{
				output = candidate;
			}
			return success;
		}
		static json::value schema_nested(json::mapping_context context)
		{
			return Adapter::schema(context);
		}
		static bool try_write_with_options(const T& input, json::value & output, json::error & error, json::mapping_options options)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			json::value candidate = json::value();
			bool success = context.callback_result(Adapter::encode(ref input, ref candidate, context));
			error = context.error();
			if (success)
			{
				output = candidate;
			}
			return success;
		}
		static bool try_write(const T& input, json::value & output, json::error & error)
		{
			return json::custom_converter<T, Adapter>::try_write_with_options(ref input, ref output, ref error, json::mapping_options());
		}
		static json::value write_with_options(const T& input, json::mapping_options options)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			json::value output = json::value();
			context.require(Adapter::encode(ref input, ref output, context));
			return output;
		}
		static json::value write(const T& input)
		{
			return json::custom_converter<T, Adapter>::write_with_options(ref input, json::mapping_options());
		}
		static bool try_read_with_options(json::value input, T & output, json::error & error, json::mapping_options options)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			T candidate = Adapter::create(input, context);
			bool success = context.callback_result(Adapter::decode(input, ref candidate, context));
			error = context.error();
			if (success)
			{
				output = candidate;
			}
			return success;
		}
		static bool try_read(json::value input, T & output, json::error & error)
		{
			return json::custom_converter<T, Adapter>::try_read_with_options(input, ref output, ref error, json::mapping_options());
		}
		static T read_with_options(json::value input, json::mapping_options options)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			T holder = Adapter::create(input, context);
			context.require(Adapter::decode(input, ref holder, context));
			return holder;
		}
		static T read(json::value input)
		{
			return json::custom_converter<T, Adapter>::read_with_options(input, json::mapping_options());
		}
		static json::value schema_with_options(json::mapping_options options, bool input)
		{
			json::mapping_context context = json::mapping_context_create_with_options(options);
			context.set_schema_input(input);
			return context.schema_finish(Adapter::schema(context));
		}
		static json::value schema()
		{
			return json::custom_converter<T, Adapter>::schema_with_options(json::mapping_options(), true);
		}
	};
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_encode(const array<T>& input, json::value & output, json::mapping_context context, JsonGuard guard = 0)
	{
		if (!context.visit()) return false;
		json::builder builder = json::array_builder();
		int64 index = 0;
		for (T item : input)
		{
			if (!context.push(to_string(index))) return false;
			json::value value = json::value();
			bool success = veroscript_json_mapping::mapping_encode<int>(ref item, ref value, context);
			context.pop();
			if (!success) return false;
			builder.push(value);
			index++;
		}
		output = builder.finish();
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	bool mapping_decode(json::value input, array<T>& output, json::mapping_context context, JsonGuard guard = 0)
	{
		if (input.kind() != 4) return context.fail("Expected a JSON array");
		if (!context.visit()) return false;
		array<T> result = array<T>();
		for (int64 index = 0; index < input.size(); index++)
		{
			if (!context.push(to_string(index))) return false;
			if (!veroscript_json_mapping::mapping_validate<int>(type<T>(), input.at(index), context))
			{
				context.pop();
				return false;
			}
			T holder = veroscript_json_mapping::mapping_initial<int>(type<T>(), input.at(index), context);
			context.use_fresh(type<T>().qualified_name);
			bool success = veroscript_json_mapping::mapping_decode<int>(input.at(index), ref holder, context);
			context.pop();
			if (!success) return false;
			result.push(holder);
		}
		output = result;
		return true;
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int>
	json::value mapping_schema(type<json::value> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		return json::object();
	}
}
// @json-declaration:members
namespace veroscript_json_mapping
{
	template<JsonGuard = int, T = int>
	json::value mapping_schema(type<array<T>> tag, json::mapping_context context, JsonGuard guard = 0)
	{
		json::value schema = json::object();
		schema.set("type", json::from_string("array"));
		schema.set("items", veroscript_json_mapping::mapping_schema<int>(type<T>(), context));
		return schema;
	}
}
