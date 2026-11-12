# Schema 注釈レポート

FSchema::TryValidateWithOptions／schema.validate_with_options は error と注釈を返し、TryIsValid/is_valid は収集しない高速モードです。実際に評価して成功した branch だけが寄与します。失敗／未選択 branch や未参照 $defs は除外し、anyOf は全成功 branch、contains は全一致要素を集めます。全体が無効なら注釈を捨て、error は残します。

InstancePointer → instance_pointer、SchemaLocation → schema_location、EvaluationPath → evaluation_path、Keyword → keyword、Value → value は、instance JSON Pointer、keyword の絶対 URI、$ref を含む評価 path、keyword 名、不変で正確な JSON 値です。title/description/default/examples/deprecated/readOnly/writeOnly/format、適用できる content metadata、通常の未知 keyword を収集します。contentSchema は同じ Schema に contentMediaType があり instance が string の場合だけで、暗黙 decode／validate はしません。$comment は除外、default は instance を変更しません。

properties/patternProperties/additionalProperties/unevaluatedProperties は property 名配列、prefixItems は最後の index または true、contains は一致 index 配列または true、適用済み items/unevaluatedItems は true を記録します。子 instance の mark を親へ混ぜません。同条件では順序再現できますが固定 index ではなく path／keyword で読みます。

FSchemaReportOptions は既定で収集し MaxIssues=MaxAnnotations=256。error は 1–16384、注釈は 0–16384 です。注釈上限で有効性は保持し bAnnotationsTruncated/bTruncated を立て、失敗 branch の切り捨ても捨てます。MaxAnnotations=0 でも未収集の有無を判定し、完全停止は bCollectAnnotations=false です。Limits/FWork が work、注釈 byte、node／depth、取消を制限します。未完了は false と元 report 保持、Cancelled/Limit/Allocation を区別します。report 値は schema／input／resolver の破棄後も有効、並行検証は独立 report が必要です。

TryValidate/schema.validate は既定注釈設定、既存 MaxIssues/Limits／validate_with_limits は有効です。標準 verbose tree を称する形式ではなくモジュール固有の安定構造です。schema_report は annotation_count/annotation/annotations_truncated、schema_report_options は limits/max_issues/max_annotations/collect_annotations と setter。try_validate_schema_with_options(schema,value,options,ref report,ref error) は通常失敗で report を保持し、取消や wrapper を返せない失敗は native 実行の失敗です。

```VeroScript
json::schema schema = json::compile_schema(json::parse("{\"title\":\"Count\",\"type\":\"integer\"}"));
json::schema_report_options options = json::schema_report_options();
options.set_max_annotations(16);
json::schema_report report = schema.validate_with_options(json::from_int(3), options);
print(report.annotation(0).get("value").as_string());
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/schema-annotations.md) · [fr-FR](../../fr-FR/Reference/schema-annotations.md) · [ja-JP](schema-annotations.md) · [ko-KR](../../ko-KR/Reference/schema-annotations.md) · [zh-CN](../../zh-CN/Reference/schema-annotations.md) · [zh-TW](../../zh-TW/Reference/schema-annotations.md)
