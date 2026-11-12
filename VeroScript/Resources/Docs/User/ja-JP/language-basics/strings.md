# 文字列

VeroScript は、`string` と `wstring` の 2 つのテキスト タイプを提供します。これらはどちらもテキスト値を表しますが、別個の型であるため、明示的な変換を行わずに代入、比較、または連結で混合することはできません。このページでは、`string.vs` の文字列の動作と一般的なヘルパーについて説明します。リテラル構文については、[リテラル](literals.md) で説明します。

## テキストの種類

通常の文字列リテラルの型は `string` ですが、ワイド文字列リテラルの型は `wstring` です。

```VeroScript
string name = "VeroScript";
wstring title = L"Language reference";
```

`string` と `wstring` は両方とも、`len`、等価比較、および同じテキスト タイプの値との連結をサポートします。連結のオペランドは同じテキスト タイプに属している必要があります。

```VeroScript
string message = "Hello, " + name;
wstring heading = L"Vero" + L"Script";
```

2 つのテキスト タイプ間の明示的な変換には、`to_wstring` または `to_string` を使用します。

```VeroScript
wstring wide_name = to_wstring(name);
string narrow_name = to_string(wide_name);
```

数値またはブール値をテキストに変換する場合も、`to_string` を使用します。これにより、暗黙的な混合型の連結に依存するのではなく、変換が明示的に保たれます。

```VeroScript
string summary = "count=" + to_string(3);
```

## 文字列標準ライブラリ

文字列ヘルパーは `string.vs` によって提供されます。使用する前にモジュールをインポートします。

```VeroScript
import "string.vs";
```

一般的な機能は次のとおりです。

|機能 |説明 |
| --- | --- |
| `std::strings::substring(s, start, count)` | `start` から始まる指定された数の文字を抽出します。 |
| `std::strings::find(s, needle)` |部分文字列を検索し、その位置を返します。見つからない場合は負の値を返します。 |
| `std::strings::char_code(s, index)` |インデックスの文字コードを返します。 |
| `std::strings::contains(s, sub)` |部分文字列が存在するかどうかをテストします。 |
| `std::strings::starts_with(s, prefix)` |文字列がプレフィックスで始まるかどうかをテストします。 |
| `std::strings::ends_with(s, suffix)` |文字列が接尾辞で終わるかどうかをテストします。 |
| `std::strings::trim_left(s)` / `std::strings::trim_right(s)` |左側または右側の空白を削除します。 |
| `std::strings::trim(s)` |両側の空白を削除します。 |
| `std::strings::split(s, separator)` |文字列を `array<string>` に分割します。 |
| `std::strings::join(parts, separator)` |区切り文字を使用して文字列配列を結合します。 |
| `std::strings::to_lower(s)` / `std::strings::to_upper(s)` |テキストを小文字または大文字に変換します。 |
| `std::strings::replace(s, from, to)` |一致する部分文字列を指定されたテキストに置き換えます。 |
| `std::strings::last_find(s, needle)` |最後に一致した UTF-8 バイト オフセットを返し、ない場合は `-1` を返します。 |
| `std::strings::repeat(s, count)` |`s` を繰り返します。負の count は実行時エラーです。 |
| `std::strings::remove_prefix(s, prefix)` / `std::strings::remove_suffix(s, suffix)` |一致する接頭辞または接尾辞を削除し、なければ `s` を返します。 |

これらの関数は新しい値を返しますが、入力文字列は変更されません。例えば：

```VeroScript
import "string.vs";

string raw = "  red,green,blue  ";
array<string> colors = std::strings::split(std::strings::trim(raw), ",");
string normalized = std::strings::join(colors, " | ");
print(normalized);
```

`trim_left`、`trim_right`、`trim` は ASCII 空白バイト `0x09` から `0x0D` とスペース（`0x20`）を除去します。文字列インデックスはゼロ始まりの UTF-8 バイトオフセットで、Unicode コードポイントの番号ではありません。`len`、`find`、`last_find` は `int64` を返し、一致しない検索は `-1` です。スライスの位置と個数、`char_code` のインデックス、`repeat` の個数には `int64` を使います。範囲と戻り値の型は[文字列モジュール](../standard-library/string.md)を参照してください。

## フォーマット

VeroScript は 2 つの独立した書式設定言語を提供します。 `format` は中括弧フィールドを使用します。 `formatf` および `printf` は printf スタイルの変換を使用します。 1 つの書式文字列に 2 つの方言を混在させないでください。

### 中括弧の書式設定

`string.vs` の公開署名は次のとおりです。

```text
string format(string fmt, array<string> args);
```

スクリプト コードは通常、スカラー引数を直接渡します。コンパイラは、`string`、ブール値、浮動小数点値、符号付きまたは符号なし整数を文字列に変換し、それらを引数配列に収集します。

```VeroScript
import "string.vs";

string message = format("name={}, score={}", "Ada", 97);
print(message);
```

`array<string>` がすでに存在する場合は、2 番目の引数として直接渡すことができます。

```VeroScript
import "string.vs";

array<string> args = array<string>();
args.push("Ada");
args.push("97");
string message = format("name={}, score={}", args);
```

フィールドの形式は次のとおりです。

|フォーム |意味 |
| --- | --- |
| `{}` |インデックス `0` から始まる次の自動引数を使用します。 |
| `{N}` |明示的なゼロベースの引数インデックス (`{0}` など) を使用します。 |
| `{:spec}` |次の自動引数をフォーマット指定で使用します。 |
| `{N:spec}` |形式指定のある明示的なインデックスを使用します。 |
| `{{` / `}}` |リテラルの `{` / `}` を発行します。 |

形式仕様の形式は `[alignment][width][.precision][type]` です。

|パート |受け入れられる値 |意味 |
| --- | --- | --- |
|位置合わせ | `<`、`>`、`^` |左揃え、右揃え、または中央揃え。右揃えがデフォルトです。 |
|幅 | 10 進整数 `0` ～ `1024` |短い結果をスペースで埋めます。値が切り捨てられることはありません。 |
|精度 | `.decimal-integer`、`0` ～ `1024` |有限の数値を解析し、その桁数の固定小数点出力を生成します。 |
|タイプ | `x`、`X` | 10 進整数を小文字または大文字の 16 進数に変換します。 |

`format` の `x`/`X` 変換は、符号付き 32 ビット 10 進整数を解析し、その 32 ビット 16 進数表現を出力します。完全な符号なし 64 ビット範囲が必要な場合は、`%x` または `%X` を `formatf` とともに使用します。

例えば：

```VeroScript
import "string.vs";

string right = format("{:>6}", "ok");   // "    ok"
string left = format("{:<6}", "ok");    // "ok    "
string center = format("{:^6}", "ok");  // "  ok  "
string fixed = format("{:.2}", "1.5");  // "1.50"
string hex = format("{:X}", "255");     // "FF"
string repeated = format("{0} / {0}", "same");
string braces = format("{{value}}", array<string>()); // "{value}"
```

自動インデックスは、自動フィールドが使用されている場合にのみ進められます。予期せぬ結果を避けるには、1 つの書式文字列内で一貫して自動インデックス作成または明示的インデックス作成を使用してください。引数の欠落、中括弧の不一致、無効な仕様、および数値仕様として解析できない値により、実行時エラーが発生します。フィールドによって参照されない追加の引数は無視されます。

### printf の書式設定

`formatf` の公開署名は次のとおりです。

```text
string formatf(string fmt, array<string> args);
```

スクリプト コードは、スカラー引数を直接渡すこともできます。 `printf` は同じ文法を使用しますが、標準出力に直接書き込みます。変換は次のような形になります。

```text
%[flags][width][.precision][length]conversion
```

|パート |許容される値または制限 |
| --- | --- |
|フラグ | `-` は左揃え、`+` は正の符号を示し、スペースは正の数値の前にスペースを付け、`#` は代替プレフィックスを有効にし、`0` はゼロ パディングを有効にします。 |
|幅 | 10 進整数 `0` ～ `1024`;動的幅 `*` はサポートされていません。 |
|精度 | `.digits`、`0` ～ `1024`;動的精度 `.*` はサポートされていません。 |
|長さ | `h`、`hh`、`l`、`ll`、`j`、`z`、および `t` はソース互換性のために受け入れられますが、VeroScript 値の幅や ABI は変更されません。 |
|変換 | `d`、`i`、`u`、`o`、`x`、`X`、`f`、`F`、`e`、`E`、`g`、 `G`、`c`、および `s`。 |

変換には次の引数形式が必要です。

|変換 |引数と結果 |
| --- | --- |
| `%d`、`%i` |符号付き整数。 |
| `%u` |負ではない整数。 |
| `%o` | 8 進整数。 |
| `%x`、`%X` |小文字または大文字の 16 進整数。 |
| `%f`、`%F` |固定小数点浮動小数点。デフォルトの精度は `6` です。 |
| `%e`、`%E` |科学的表記法。大文字のバリアントでは大文字が使用されます。 |
| `%g`、`%G` |デフォルトの浮動小数点表現。大文字のバリアントでは大文字が使用されます。 |
| `%c` | `0` ～ `255` の整数、または半角文字列。精度が無効です。 |
| `%s` |文字列。精度により、発行されるバイト数が制限されます。 |
| `%%` |引数を使用せずにリテラルの `%` を発行します。 |

`%c` および `%s` では、`-` フラグと幅のみが許可されます。数値変換の場合、整数のゼロ埋め込みよりも精度が優先されます。末尾の `%`、引数の欠落、サポートされていない変換、または要求された変換で解析できない値により、ランタイム エラーが発生します。浮動小数点変換には有限の数値が必要です。変換によって消費されない追加の引数は無視されます。

```VeroScript
import "string.vs";

string text = formatf("d=%+06d x=%#08X f=%.2f s=%-5.3s c=%c", -42, 255, 3.14159, "abcdef", 65);
printf("%s%% done", text);
```

### 出力関数との関係

```VeroScript
import "string.vs";

print("name={}, score={}", "Ada", 97); // print's multi-argument form uses braces
printf("score=%03d", 7);                // printf always uses printf syntax
string value = format("score={}", 7);   // use format when a string is needed
```

値が 1 つの場合、`print` はそれを直接書き込みます。書式文字列と追加の引数を指定すると、中かっこの書式設定が使用されます。 `format` と `formatf` は文字列を返しますが、`printf` は標準出力に書き込みます。 `format`、`formatf`、および `printf` は、参照引数または `void` 式を書式設定引数として受け入れません。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/strings.md) · [fr-FR](../../fr-FR/language-basics/strings.md) · [ja-JP](strings.md) · [ko-KR](../../ko-KR/language-basics/strings.md) · [zh-CN](../../zh-CN/language-basics/strings.md) · [zh-TW](../../zh-TW/language-basics/strings.md)
