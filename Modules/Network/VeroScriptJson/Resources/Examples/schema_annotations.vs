// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

// Load VeroScriptJson before compiling this example.
int main()
{
	json::schema schema = json::compile_schema(json::parse("{\"title\":\"Count\",\"type\":\"integer\"}"));
	json::schema_report_options options = json::schema_report_options();
	options.set_max_annotations(16);
	json::schema_report_options copy = options;
	copy.set_collect_annotations(false);
	if (!options.collect_annotations() || copy.collect_annotations()) return 1;
	json::schema_report report = schema.validate_with_options(json::from_int(3), options);
	if (!report.valid() || report.annotation_count() != 1 || report.annotations_truncated()) return 2;
	json::value annotation = report.annotation(0);
	if (annotation.get("keyword").as_string() != "title" || annotation.get("value").as_string() != "Count") return 3;
	if (annotation.get("evaluation_path").as_string() != "/title" || annotation.get("instance_pointer").as_string() != "") return 4;
	report = schema.validate_with_options(json::from_int(3), copy);
	if (report.annotation_count() != 0 || report.truncated()) return 5;
	options.set_max_annotations(0);
	json::error error = json::error();
	if (!json::try_validate_schema_with_options(schema, json::from_int(3), options, ref report, ref error)) return 6;
	if (!report.valid() || !report.annotations_truncated() || !report.truncated()) return 7;
	report = schema.validate(json::from_string("bad"));
	if (report.valid() || report.annotation_count() != 0 || report.size() != 1) return 8;
	if (annotation.get("value").as_string() != "Count") return 9;
	return 0;
}
