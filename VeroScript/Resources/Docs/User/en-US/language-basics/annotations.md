# Annotations and source metadata

Place @Name or @Name(arguments) before a declaration. An annotation is descriptive metadata until a loaded extension assigns it behavior. It is separate from the value container `std::attribute<T>`.

Arguments retain flags, scalar kinds, named values and nested groups in source order, including repeated names. They are not arbitrary executable expressions. Use indexed access to preserve duplicates; find_annotations follows the registered annotation name policy; find_arguments uses the exact stored argument name. Annotation names include @.

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

## Structured reflection

See [Reflection — Structured reflection](reflection.md#reflection-metadata-contract).

[Reflection](reflection.md) · [Comments](comments.md) · [std::compiler](../standard-library/compiler.md)

## Annotation name matching

Names are case-sensitive by default. An extension can set `annotation_definition.case_sensitive = false` before registration to make `@GuiWidget`, `@guiWidget`, `@guiwidget` and `@GUIWIDGET` resolve to one definition. Processor subscriptions, `has_annotation`, `find_annotations` and `annotation_info.matches_name(name)` use that definition policy. `name()` and `raw_text()` retain source spelling; `definition().name()` returns the registered name, and `definition().case_sensitive()` reports the policy. Argument names and metadata keys remain case-sensitive. `find_annotations` and `matches_name` retain the `@` prefix requirement; `has_annotation` continues to accept an optional `@`. Unregistered annotations remain case-sensitive.

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md) · [en-US](annotations.md) · [fr-FR](../../fr-FR/language-basics/annotations.md) · [ja-JP](../../ja-JP/language-basics/annotations.md) · [ko-KR](../../ko-KR/language-basics/annotations.md) · [zh-CN](../../zh-CN/language-basics/annotations.md) · [zh-TW](../../zh-TW/language-basics/annotations.md)
