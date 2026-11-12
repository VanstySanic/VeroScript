# Schema annotation reports

`FSchema::TryValidateWithOptions` / `schema.validate_with_options` return errors and annotations; TryIsValid/is_valid is the fast mode without annotations. Only evaluated successful branches contribute. Failed/unselected branches and unused $defs do not; anyOf collects all successful branches and contains all matching elements. An invalid overall Schema result discards its annotations but retains errors.

Fields map as `InstancePointer → instance_pointer`, `SchemaLocation → schema_location`, `EvaluationPath → evaluation_path`, `Keyword → keyword`, `Value → value`. They contain the instance JSON Pointer, absolute keyword URI, evaluation path including $ref, keyword and immutable exact JSON value. Metadata includes title/description/default/examples/deprecated/readOnly/writeOnly/format, applicable content metadata and ordinary unknown keywords. contentSchema is collected only alongside contentMediaType for a string, with no implicit decode/validation. $comment is excluded and default never changes the instance.

Evaluation annotations include property-name arrays for properties/patternProperties/additionalProperties/unevaluatedProperties; prefixItems’ last index or true; contains’ matched indices or true; and true for applied items/unevaluatedItems. Nested marks do not merge into parent instances. Order is repeatable for identical inputs/options, but read by path/keyword rather than fixed index.

FSchemaReportOptions collects by default, MaxIssues=MaxAnnotations=256. Issues range 1–16384, annotations 0–16384. Annotation truncation preserves validity and sets bAnnotationsTruncated/bTruncated; failed branches discard their truncation flags. MaxAnnotations=0 still detects omitted annotations; disable collection with bCollectAnnotations=false. Limits/FWork cover work, annotation bytes, nodes/depth and cancellation. An incomplete execution returns false, preserves the report and distinguishes Cancelled/Limit/Allocation. Report values outlive schemas, inputs and resolver callbacks. Concurrent validations require separate reports.

TryValidate/schema.validate use default annotation options; existing MaxIssues/Limits and validate_with_limits remain effective. This is the module’s stable report, not a claimed standard verbose tree. Script schema_report exposes annotation_count/annotation/annotations_truncated; schema_report_options exposes limits/max_issues/max_annotations/collect_annotations and setters. Checked entry: try_validate_schema_with_options(schema,value,options,ref report,ref error). Ordinary failure preserves the report; cancellation and wrapper-delivery failures still fail native execution.

```VeroScript
json::schema schema = json::compile_schema(json::parse("{\"title\":\"Count\",\"type\":\"integer\"}"));
json::schema_report_options options = json::schema_report_options();
options.set_max_annotations(16);
json::schema_report report = schema.validate_with_options(json::from_int(3), options);
print(report.annotation(0).get("value").as_string());
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](schema-annotations.md) · [fr-FR](../../fr-FR/Reference/schema-annotations.md) · [ja-JP](../../ja-JP/Reference/schema-annotations.md) · [ko-KR](../../ko-KR/Reference/schema-annotations.md) · [zh-CN](../../zh-CN/Reference/schema-annotations.md) · [zh-TW](../../zh-TW/Reference/schema-annotations.md)
