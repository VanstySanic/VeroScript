# 注解与源码元数据

在声明前写 @Name 或 @Name(arguments)。注解默认是描述性元数据，只有已加载的扩展才会赋予它业务行为。它与保存值的 `std::attribute<T>` 是不同功能。

参数保留标志、标量类型、命名值和嵌套分组，按源码顺序保留重复名称，不是任意可执行表达式。需要保留重复项时按索引枚举；find_annotations 按注册的注解名称策略匹配，find_arguments 按保存的参数完整名称匹配，注解名称包含 @。

```VeroScript
/// Example metadata.
@Tag(data=(size=4, enabled=true), label="Example")
class Value
{
};

int main()
{
	type<Value> info = type<Value>();
	assert(info.has_annotation("@Tag"));
	annotation_info tag = info.annotation(0);
	assert(tag.name() == "@Tag");
	assert(tag.argument(0).argument(1).bool_value());
	print(tag.argument(0).argument(0).int64_value());
	return 0;
}
```

## 结构化反射

参阅[反射 — 结构化反射](reflection.md#reflection-metadata-contract)。

[反射](reflection.md) · [注释](comments.md) · [std::compiler](../standard-library/compiler.md)

## 注解名称匹配

名称默认区分大小写。扩展可以在注册前设置 `annotation_definition.case_sensitive = false`，让 `@GuiWidget`、`@guiWidget`、`@guiwidget` 和 `@GUIWIDGET` 解析为同一个定义。处理器订阅、`has_annotation`、`find_annotations` 和 `annotation_info.matches_name(name)` 共用定义中的规则。`name()` 和 `raw_text()` 保留源码拼写；`definition().name()` 返回注册名称，`definition().case_sensitive()` 返回匹配策略。参数名和元数据键仍区分大小写。`find_annotations` 和 `matches_name` 保留 `@` 前缀要求；`has_annotation` 仍允许省略 `@`。未注册的注解继续区分大小写。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md) · [en-US](../../en-US/language-basics/annotations.md) · [fr-FR](../../fr-FR/language-basics/annotations.md) · [ja-JP](../../ja-JP/language-basics/annotations.md) · [ko-KR](../../ko-KR/language-basics/annotations.md) · [zh-CN](annotations.md) · [zh-TW](../../zh-TW/language-basics/annotations.md)
