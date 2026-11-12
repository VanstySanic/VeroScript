# 文字列モジュール

モジュールファイル: `string.vs`

次のようにインポートします。

```VeroScript
import "string.vs";
```

## クエリとスライス

|機能 |説明 |
| --- | --- |
| `std::strings::substring(string s, int64 start, int64 count)` | バイトオフセット `start` から最大 `count` 個の UTF-8 バイトを返します。 |
| `std::strings::find(string haystack, string needle)` | 最初に一致した UTF-8 バイトオフセットを `int64` で返し、見つからない場合は `-1` です。 |
| `std::strings::char_code(string s, int64 index)` | `index` にあるバイトを `int`（`0`–`255`）として返します。 |
| `std::strings::contains(string s, string sub)` |部分文字列が存在するかどうかをテストします。 |
| `std::strings::starts_with(string s, string prefix)` |文字列がプレフィックスで始まるかどうかをテストします。 |
| `std::strings::ends_with(string s, string suffix)` |文字列が接尾辞で終わるかどうかをテストします。 |

文字列インデックスは Unicode コードポイントではなく、0 始まりの UTF-8 バイト オフセットです。
`std::strings::find`、`std::strings::last_find`、`std::strings::substring`、`std::strings::char_code` は同じ規則を使用します。範囲外のインデックスを
使用すると、実行時エラーが発生します。

## 分割、結合、変換

|機能 |説明 |
| --- | --- |
| `std::strings::split(string s, string separator)` |文字列を `array<string>` に分割します。 |
| `std::strings::join(array<string> parts, string separator)` |区切り文字を使用して文字列配列を結合します。 |
| `std::strings::trim_left(string s)` |左側の ASCII 空白 (タブ、改行、垂直タブ、改ページ、復帰、スペース) を削除します。 |
| `std::strings::trim_right(string s)` |右側の空白を削除します。 |
| `std::strings::trim(string s)` |両側の空白を削除します。 |
| `std::strings::to_lower(string s)` |テキストを小文字に変換します。 |
| `std::strings::to_upper(string s)` |テキストを大文字に変換します。 |
| `std::strings::replace(string s, string from, string to)` |一致する部分文字列を置換します。 `from` が空の場合は、元の文字列を返します。 |
| `std::strings::last_find(string haystack, string needle)` |最後に一致した UTF-8 バイト オフセットを返します。ない場合は `-1`、空の needle は `len(haystack)` です。 |
| `std::strings::repeat(string value, int64 count)` |文字列を `count` 回繰り返します。負の count は実行時エラーです。 |
| `std::strings::remove_prefix(string value, string prefix)` |`prefix` が存在すれば削除し、なければ元の文字列を返します。 |
| `std::strings::remove_suffix(string value, string suffix)` |`suffix` が存在すれば削除し、なければ元の文字列を返します。 |

## フォーマット

このモジュールは 2 つの書式設定関数を提供します。`format` は中括弧フィールドを使用し、`formatf` は printf スタイルの変換を使用します。 2 つの文法は独立しているため、混合しないでください。

公開署名は次のとおりです。

```text
string format(string fmt, array<string> args);
string formatf(string fmt, array<string> args);
```

スクリプト コードは、`string`、ブール値、浮動小数点値、符号付き整数または符号なし整数を直接渡すことができます。コンパイラはこれらのスカラーを文字列に変換し、`array<string>` に収集します。既存の `array<string>` を 2 番目の引数として直接渡すこともできます。

```VeroScript
import "string.vs";

string text = format("value={}, name={}", 42, "VeroScript");
string hex = format("{:8X}", 255);
string legacy = formatf("value=%d", 42);
```

### `format` フィールド

|フォーム |意味 |
| --- | --- |
| `{}` |次の自動引数を使用します。 |
| `{N}` |明示的なゼロベースのインデックスを使用します。 |
| `{:spec}` / `{N:spec}` | `[alignment][width][.precision][type]` 形式の仕様を使用します。 |
| `{{` / `}}` |リテラルの中括弧を発行します。 |

配置は `<`、`>`、または `^` で、デフォルトは右配置です。幅と精度は `0` ～ `1024` に制限され、スペース パディングを使用します。タイプ コードは、小文字と大文字の 16 進数の `x` と `X` のみです。 Precision は有限の数値を解析し、固定小数点数を出力します。自動インデックスと明示的インデックスを混合しないでください。引数の欠落、中括弧の不一致、および無効な仕様により、実行時エラーが発生します。

<a id="integer-formatting"></a>

format x/X は 64 ビットに対応し、正値は UINT64_MAX、負値は INT64_MIN まで扱い、負値は 64 ビット補数です。-1 は f が16個です。printf 整数は double を経由しません。幅と精度は引き続き1024以下です。

### `formatf` コンバージョン

変換形状は `%[flags][width][.precision][length]conversion` です。サポートされているフラグは、`-`、`+`、スペース、`#`、および `0` です。幅と精度は `0` ～ `1024` に制限されており、動的な `*` はサポートされていません。長さ修飾子 `h`、`hh`、`l`、`ll`、`j`、`z`、および `t` はソース互換性のために受け入れられますが、VeroScript 値の幅や ABI は変更されません。

サポートされている変換は次のとおりです。

|変換 |意味 |
| --- | --- |
| `%d`、`%i` |符号付き整数。 |
| `%u`、`%o`、`%x`、`%X` |負でない 10 進数、8 進数、または小文字/大文字の 16 進整数。 |
| `%f`、`%F`、`%e`、`%E`、`%g`、`%G` |浮動小数点;デフォルトの精度は `6` です。 |
| `%c` | `0` ～ `255` の整数または半角文字列。精度が無効です。 |
| `%s` |弦;精度により、発行されるバイト数が制限されます。 |
| `%%` |引数を使用しないリテラル `%`。 |

`printf` は、`formatf` と同じ変換文法を使用し、標準出力に直接書き込みます。 `print` の複数引数形式では、`format` の中括弧文法が使用されます。変換によって消費されない追加の引数は無視されます。完全なルールとエラー境界については、[文字列言語の動作](../language-basics/strings.md#%E3%83%95%E3%82%A9%E3%83%BC%E3%83%9E%E3%83%83%E3%83%88) を参照してください。
`std::strings::to_lower` と `std::strings::to_upper` は ASCII の英字だけを変換し、その他の UTF-8 バイトは保持します。Unicode の大文字・小文字変換は行いません。
`std::strings::trim_left`、`std::strings::trim_right`、`std::strings::trim` は端の ASCII `0x09`～`0x0D` と空白 (`0x20`) を取り除きます。内部および非 ASCII のバイトは保持します。

`len(s)`、`std::strings::find`、`std::strings::last_find` は `int64` を返し、検索結果がない場合は `-1` です。`std::strings::substring` のバイトオフセットと個数は `int64` です。`start` は `len(s)` と等しくてもよく、`count` は非負でなければならず、残りのバイト数を超える場合は取得可能な末尾までに切り詰めます。`std::strings::char_code` は `int64` のバイトインデックスを受け取り、バイト値を `int`（`0`–`255`）として返します。`std::strings::repeat` の個数は `int64` で、結果の長さが `int64` の範囲を超えると拒否されます。確保上限も引き続き適用されます。

- [標準ライブラリ基盤](foundation.md)

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/string.md) · [fr-FR](../../fr-FR/standard-library/string.md) · [ja-JP](string.md) · [ko-KR](../../ko-KR/standard-library/string.md) · [zh-CN](../../zh-CN/standard-library/string.md) · [zh-TW](../../zh-TW/standard-library/string.md)
