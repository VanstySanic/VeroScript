# Schema 注解报告

`FSchema::TryValidateWithOptions` 和脚本 `schema.validate_with_options` 提供错误与注解报告。
`TryIsValid` / `schema.is_valid` 保留快速有效性模式，不收集注解。

## 报告内容

注解只来自实际求值并成功的 Schema 分支；失败分支、未选择的 `then/else` 和仅位于
`$defs` 中而未被引用的定义不产生报告。`anyOf` 收集所有成功分支，`contains` 收集所有
匹配元素。整个 Schema 无效时，其注解被丢弃；错误仍保留。

每项注解包含：

| C++ 字段 | 脚本 JSON 字段 | 含义 |
| --- | --- | --- |
| `InstancePointer` | `instance_pointer` | 实例的 JSON Pointer |
| `SchemaLocation` | `schema_location` | 关键字的绝对 Schema URI |
| `EvaluationPath` | `evaluation_path` | 本次求值经过的关键字路径，包括 `$ref` 路径 |
| `Keyword` | `keyword` | 关键字名称 |
| `Value` | `value` | 不可变 JSON 值，精确数字不经过 double |

支持 `title/description/default/examples/deprecated/readOnly/writeOnly/format`、适用的 content
元数据及普通未知关键字。`contentSchema` 仅在同一 Schema 有 `contentMediaType` 且实例是字符串时收集，
不隐式解码或验证字符串中的内容。`$comment` 按 [Core 8.3](https://json-schema.org/draft/2020-12/json-schema-core#section-8.3)
排除，不能作为用户注解输出。`default` 不修改实例。

求值注解包括 `properties/patternProperties/additionalProperties/unevaluatedProperties`
的属性名数组、`prefixItems` 的最后匹配索引或 true、`contains` 的匹配索引数组或 true，
以及实际应用了 `items/unevaluatedItems` 时的 true。嵌套实例的求值标记不会合并进父实例。
注解顺序在同一 Schema、输入和选项下可重复；调用方按路径和关键字读取，不依赖固定索引。

## 选项与生命周期

`FSchemaReportOptions` 默认收集注解，`MaxIssues/MaxAnnotations` 均为 256。
错误上限范围为 1–16384，注解上限为 0–16384。达到注解上限时保留有效性结果，设置
`bAnnotationsTruncated` 和 `bTruncated`；失败分支产生的截断状态也会被丢弃。
`MaxAnnotations=0` 仍判断是否存在被截断的注解；完全关闭收集使用 `bCollectAnnotations=false`。

`Limits` 和 `FWork` 继续限制执行量、注解字节数、节点和深度并检查取消。执行不能完成时返回 false，
保留原报告，通过 `FError` 区分取消、额度和分配失败。报告中的值可在编译 Schema、资源输入及
解析器回调销毁后继续读取。编译结果可并发验证，各次调用使用独立报告。

旧的 `TryValidate` / `schema.validate` 使用默认注解选项；`TryValidate` 的 `MaxIssues/Limits`
参数及脚本 `validate_with_limits` 继续生效。此报告是模块自己的稳定结构，不声称是标准 verbose 输出树。

## 脚本

```vs
json::schema schema = json::compile_schema(json::parse("{\"title\":\"Count\",\"type\":\"integer\"}"));
json::schema_report_options options = json::schema_report_options();
options.set_max_annotations(16);
json::schema_report report = schema.validate_with_options(json::from_int(3), options);
print(report.annotation(0).get("value").as_string());
```

`schema_report` 新增 `annotation_count/annotation/annotations_truncated`。
`schema_report_options` 提供 `limits/max_issues/max_annotations/collect_annotations` 及各自 `set_*`。
非抛错入口为 `json::try_validate_schema_with_options(schema, value, options, ref report, ref error)`。
普通失败保留旧报告；执行取消和无法交付包装对象的失败使 native 调用失败。

正式示例：[schema_annotations.vs](../../../../Examples/schema_annotations.vs)。

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/schema-annotations.md) · [fr-FR](../../fr-FR/Reference/schema-annotations.md) · [ja-JP](../../ja-JP/Reference/schema-annotations.md) · [ko-KR](../../ko-KR/Reference/schema-annotations.md) · [zh-CN](schema-annotations.md) · [zh-TW](../../zh-TW/Reference/schema-annotations.md)
