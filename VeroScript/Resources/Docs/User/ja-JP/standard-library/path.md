# パスモジュール

モジュールファイル: `path.vs`

次のようにインポートします。

```VeroScript
import "path.vs";
```

## 関数

|機能 |説明 |
| --- | --- |
| `std::filesystem::basename(string path)` |最後のパス区切り文字の後の名前を返します。 |
| `std::filesystem::dirname(string path)` |最後の区切り文字の前のディレクトリを返します。区切り文字がない場合は、`"."` を返します。 |
| `std::filesystem::join_path(string left, string right)` | 2 つのパス コンポーネントをセパレータで結合します。 |
| `std::filesystem::extension(string path)` |ドットを含む拡張子を返します。拡張子がない場合は空の文字列を返します。 |
| `std::filesystem::change_extension(string path, string new_ext)` |拡張子を置換または追加します。 |

パス モジュールは、`/` と `\` の両方を区切り文字として認識します。 `std::filesystem::join_path` は、左側のコンポーネントの末尾にある既存のセパレーターと、両側の空のコンポーネントを処理します。

`std::filesystem::change_extension` は、先頭のドットの有無にかかわらず、新しい拡張子を受け入れます。 `"json"` と `".json"` は同じ結果を生成します。ファイル名の先頭のドットは、それ自体では拡張子として扱われません。
`std::filesystem::dirname` は Windows のドライブルートの区切り文字を保持します。たとえば `std::filesystem::dirname("C:\\foo.txt")` は `C:\` を返します。
`std::filesystem::join_path` は字句的な結合です。片側が空ならもう片側をそのまま返します。右側が `/` または `\\` で始まるか、`C:` のような ASCII ドライブ接頭辞を持つ場合は右側を返します。それ以外は必要に応じて `/` を挿入しますが、左側がちょうど `C:` のときは `C:file` になります。`.`、`..` の正規化やファイルシステムの参照はしません。


- [標準ライブラリ基盤](foundation.md)

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/path.md) · [fr-FR](../../fr-FR/standard-library/path.md) · [ja-JP](path.md) · [ko-KR](../../ko-KR/standard-library/path.md) · [zh-CN](../../zh-CN/standard-library/path.md) · [zh-TW](../../zh-TW/standard-library/path.md)
