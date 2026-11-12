# 註記與原始碼中繼資料

在宣告前寫 @Name 或 @Name(arguments)。註記（annotation）預設是描述性中繼資料，只有已載入的擴充才會賦予業務行為。它與保存值的 `std::attribute<T>` 是不同功能。

參數保留旗標、純量型別、具名值及巢狀群組，依原始碼順序保留重複名稱，並非任意可執行運算式。需要保留重複項目時依索引列舉；find_annotations 依註冊的註記名稱規則比對，find_arguments 依儲存的參數完整名稱比對，註記名稱包含 @。

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

## 結構化反射

請參閱[反射 — 結構化反射](reflection.md#reflection-metadata-contract)。

[反射](reflection.md) · [註解](comments.md) · [std::compiler](../standard-library/compiler.md)

## 註記名稱比對

預設區分大小寫；設定 `annotation_definition.case_sensitive = false` 可讓 `@GuiWidget`、`@guiWidget` 和 `@GUIWIDGET` 使用同一定義。處理器、`has_annotation`、`find_annotations` 與 `annotation_info.matches_name(name)` 共用此規則。`name()` 和 `raw_text()` 保留原始拼寫，`definition().name()` 回傳註冊名，`definition().case_sensitive()` 回傳規則。參數和中繼資料鍵仍區分大小寫。`find_annotations` 和 `matches_name` 保留 `@` 前綴要求，`has_annotation` 可省略 `@`。未註冊的註記區分大小寫。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md) · [en-US](../../en-US/language-basics/annotations.md) · [fr-FR](../../fr-FR/language-basics/annotations.md) · [ja-JP](../../ja-JP/language-basics/annotations.md) · [ko-KR](../../ko-KR/language-basics/annotations.md) · [zh-CN](../../zh-CN/language-basics/annotations.md) · [zh-TW](annotations.md)
