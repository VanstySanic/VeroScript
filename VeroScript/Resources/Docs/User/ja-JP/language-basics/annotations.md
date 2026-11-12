# 注釈とソースメタデータ

宣言の前に @Name または @Name(arguments) を置きます。読み込んだ拡張が動作を定義するまでは、注釈は説明用の情報です。値を保持する `std::attribute<T>` とは別の機能です。

引数はフラグ、スカラーの種類、名前付き値、入れ子のグループを、重複した名前を含めて記述順に保持します。任意の実行式ではありません。重複を保持するには添字で列挙します。find_annotations は登録された照合規則に従い、find_arguments は引数名との完全一致で検索し、注釈名には @ が含まれます。

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

## 構造化されたリフレクション

[リフレクション — 構造化されたリフレクション](reflection.md#reflection-metadata-contract) を参照してください。

[リフレクション](reflection.md) · [コメント](comments.md) · [std::compiler](../standard-library/compiler.md)

## 注釈名の照合

名前は既定で大文字と小文字を区別します。`annotation_definition.case_sensitive = false` を設定すると `@GuiWidget`、`@guiWidget`、`@GUIWIDGET` が同じ定義を参照します。処理器の購読、`has_annotation`、`find_annotations`、`annotation_info.matches_name(name)` はこの規則に従います。`name()` と `raw_text()` は元の表記、`definition().name()` は登録名、`definition().case_sensitive()` は照合規則を返します。引数名とメタデータのキーは区別を維持します。`find_annotations` と `matches_name` には `@` が必要ですが、`has_annotation` では省略できます。未登録の注釈は大文字と小文字を区別します。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md) · [en-US](../../en-US/language-basics/annotations.md) · [fr-FR](../../fr-FR/language-basics/annotations.md) · [ja-JP](annotations.md) · [ko-KR](../../ko-KR/language-basics/annotations.md) · [zh-CN](../../zh-CN/language-basics/annotations.md) · [zh-TW](../../zh-TW/language-basics/annotations.md)
