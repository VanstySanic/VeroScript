# Schema 註解報告

FSchema::TryValidateWithOptions 與 schema.validate_with_options 提供錯誤和註解；TryIsValid/is_valid 保留不收集的快速模式。只有實際求值且成功分支產生註解，失敗/未選分支與未參考 $defs 不產生。anyOf 收集全部成功分支，contains 收集全部匹配元素；整體無效時捨棄註解，錯誤保留。

InstancePointer → instance_pointer、SchemaLocation → schema_location、EvaluationPath → evaluation_path、Keyword → keyword、Value → value 分別是實例 JSON Pointer、關鍵字絕對 URI、含 $ref 的求值路徑、名稱與不變精確 JSON 值。支援 title/description/default/examples/deprecated/readOnly/writeOnly/format、適用 content 中繼資料和普通未知關鍵字。contentSchema 僅在同一 Schema 有 contentMediaType 且實例為字串時收集，不暗中解碼/驗證。$comment 排除，default 不修改實例。

properties/patternProperties/additionalProperties/unevaluatedProperties 記錄屬性名陣列，prefixItems 記錄最後索引或 true，contains 記錄匹配索引或 true，實際應用 items/unevaluatedItems 記錄 true。巢狀標記不合併至父實例。同條件順序可重現，但依路徑/關鍵字讀取，不依固定索引。

FSchemaReportOptions 預設收集，MaxIssues=MaxAnnotations=256。錯誤範圍 1–16384，註解 0–16384。註解達上限仍保留有效性並設 bAnnotationsTruncated/bTruncated，失敗分支的截斷狀態也捨棄。MaxAnnotations=0 仍檢查是否有省略註解，完全關閉用 bCollectAnnotations=false。Limits/FWork 限制工作、註解位元組、節點/深度及取消。未完成回傳 false、保留原報告，區分 Cancelled/Limit/Allocation。報告值可在 Schema/輸入/resolver 銷毀後使用；並行驗證用獨立報告。

TryValidate/schema.validate 使用預設註解選項，原 MaxIssues/Limits 和 validate_with_limits 仍有效。這是模組自身穩定結構，不宣稱標準 verbose 樹。schema_report 有 annotation_count/annotation/annotations_truncated；schema_report_options 有 limits/max_issues/max_annotations/collect_annotations 及 setter。try_validate_schema_with_options(schema,value,options,ref report,ref error) 一般失敗保留原報告，取消或包裝交付失敗仍終止 native 執行。

```VeroScript
json::schema schema = json::compile_schema(json::parse("{\"title\":\"Count\",\"type\":\"integer\"}"));
json::schema_report_options options = json::schema_report_options();
options.set_max_annotations(16);
json::schema_report report = schema.validate_with_options(json::from_int(3), options);
print(report.annotation(0).get("value").as_string());
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/schema-annotations.md) · [fr-FR](../../fr-FR/Reference/schema-annotations.md) · [ja-JP](../../ja-JP/Reference/schema-annotations.md) · [ko-KR](../../ko-KR/Reference/schema-annotations.md) · [zh-CN](../../zh-CN/Reference/schema-annotations.md) · [zh-TW](schema-annotations.md)
