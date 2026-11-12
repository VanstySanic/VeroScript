// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load VeroScriptJson before compiling this example.
int main()
{
	json::value source = json::parse("{\"format\":\"email\"}");
	json::schema annotation_only = json::compile_schema(source);
	if (!annotation_only.is_valid(json::from_string("bad"))) return 1;
	json::schema_options options = json::schema_options();
	options.set_assert_formats(true);
	json::schema_options copy = options;
	copy.set_assert_formats(false);
	if (!options.assert_formats() || copy.assert_formats()) return 2;
	json::schema checked = json::compile_schema_with_options(source, options, json::object());
	if (checked.is_valid(json::from_string("bad"))) return 3;
	if (!checked.is_valid(json::from_string("x@example.test"))) return 4;
	json::schema_report report = checked.validate(json::from_string("bad"));
	if (report.valid() || report.issue(0).get("keyword").as_string() != "format") return 5;
	json::schema host = json::compile_schema_with_options(json::parse("{\"format\":\"idn-hostname\"}"), options, json::object());
	if (!host.is_valid(json::from_string("例え.テスト"))) return 6;
	json::error error = json::error();
	if (json::try_compile_schema_with_options(json::parse("{\"format\":\"unknown\"}"), options, json::object(), ref checked, ref error)) return 7;
	if (!checked.is_valid(json::from_string("x@example.test"))) return 8;
	return 0;
}
