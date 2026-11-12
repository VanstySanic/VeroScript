// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

#include <jsoncons/json.hpp>
#include <jsoncons_ext/jsonschema/jsonschema.hpp>
#include <iostream>

int main()
{
	using J = jsoncons::ojson;
	auto Options = jsoncons::json_options{}.lossless_number(true);
	auto Instance = J::parse("9007199254740993.0", Options);
	auto Schema = jsoncons::jsonschema::make_json_schema(
		J::parse(R"({"$schema":"https://json-schema.org/draft/2020-12/schema","maximum":9007199254740992})", Options));
	bool bRejected = false;
	Schema.validate(Instance,
		[&](const jsoncons::jsonschema::validation_message&)
		{
			bRejected = true;
			return jsoncons::jsonschema::walk_state::advance;
		});
	bool bEqual = J::parse("1", Options) == J::parse("1.0", Options);
	std::cout << "Exact maximum: " << bRejected << "; mathematical equality: " << bEqual << '\n';
	// This target is a qualification gate, not part of the passing module suite.
	return bRejected && bEqual ? 0 : 1;
}
