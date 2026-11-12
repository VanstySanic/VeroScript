// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

struct Directional
{
	@JsonRequired int amount=7;
	@JsonSerializeOnly int generated=11;
	@JsonDeserializeOnly int supplied=13;
};

int main()
{
	json::mapping_context context=json::mapping_context_create();
	context.set_schema_input(true);
	json::value input=json::converter<Directional>::schema_nested(context);
	json::value input_again=json::converter<Directional>::schema_nested(context);
	assert(input.equals(input_again));

	context.set_schema_input(false);
	json::value output=json::converter<Directional>::schema_nested(context);
	json::value output_again=json::converter<Directional>::schema_nested(context);
	assert(output.equals(output_again));
	assert(input.get("$ref").as_string()!=output.get("$ref").as_string());

	context.set_schema_input(true);
	assert(input.equals(json::converter<Directional>::schema_nested(context)));
	json::value root=json::parse("{\"type\":\"object\",\"properties\":{},\"required\":[\"input\",\"output\"],\"additionalProperties\":false}");
	json::value properties=json::object();
	properties.set("input",input);
	properties.set("output",output);
	root.set("properties",properties);
	json::schema schema=json::compile_schema(context.schema_finish(root));

	assert(schema.is_valid(json::parse("{\"input\":{\"amount\":2,\"supplied\":4},\"output\":{\"amount\":2,\"generated\":11}}")));
	assert(!schema.is_valid(json::parse("{\"input\":{\"amount\":2,\"generated\":11},\"output\":{\"amount\":2,\"generated\":11}}")));
	assert(!schema.is_valid(json::parse("{\"input\":{\"amount\":2},\"output\":{\"amount\":2}}")));
	assert(!schema.is_valid(json::parse("{\"input\":{\"amount\":2},\"output\":{\"amount\":2,\"generated\":11,\"supplied\":4}}")));
	return 0;
}
