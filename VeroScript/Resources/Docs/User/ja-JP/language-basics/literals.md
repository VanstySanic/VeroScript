# リテラル

リテラルは、ソース コードに直接記述された値です。VeroScript は、整数、浮動小数点、文字、文字列、ワイド文字列、ブール値、および `nullptr` リテラルをサポートします。

## 整数リテラル

接頭辞のない整数は 10 進数です。`0x`/`0X` は 16 進数、`0b`/`0B` は 2 進数を表します。

```VeroScript
int decimal = 42;
int hexadecimal = 0x2A;
int binary = 0b101010;
```

サフィックスを使用すると、範囲または符号の有無を明示できます。

```VeroScript
unsigned int unsigned_value = 100u;
long long_value = 100L;
int64 wide_value = 9223372036854775807i64;
uint64 unsigned_wide_value = 18446744073709551615u64;
```

サポートされる整数サフィックスには、大文字と小文字を区別しない形式の `u`、`l`、`ul`、`lu`、`i64`、および `u64` が含まれます。 `l`/`L` は言語の 64 ビット `long` を選択します。 `i64`/`u64` は、符号付き/符号なし 64 ビット リテラルを直接選択します。接尾辞のないリテラルと `u` リテラルは 32 ビット パスを使用します。リテラルは、そのターゲットの型で表現できる必要があります。

アンダースコアは現在の基数で有効な 2 桁の間にだけ置けます。浮動小数点の小数部と指数部でも使用でき、`1_000`、`0xFF_FF`、`0b1010_0101`、`1_2.5_0e1` は有効です。基数接頭辞、小数点、指数記号や符号、型サフィックス、別のアンダースコアには隣接できません。先頭のゼロは 8 進数を意味せず、`010` は 10 進数の十です。

## 浮動小数点リテラル

小数点または指数を含む数値は、浮動小数点リテラルとして解析されます。

```VeroScript
double pi_approximation = 3.14159;
double scientific = 1.25e2;
double half = .5;
float small = .5f;
double fifty = .5e2;
```

小数点の直後に 10 進数字が続く場合は、整数部を省略できます。そのため、`.5`、`.5f`、`.5e2` は有効な浮動小数点リテラルで、それぞれ `double` の `0.5`、`float` の `0.5`、`double` の `50.0` を表します。単独の `.` は引き続きメンバー アクセスの記号であり、`i64` などの整数サフィックスはこの形式には適用できません。

指数には `e` または `E` が使用され、`+` または `-` が含まれる場合があります。 `f` または `F` サフィックスは、`float` リテラルを識別します。接尾辞のないリテラルは `double` です。

10 進浮動小数点リテラルの小数点はホストのロケールによらず `.` です。`f`/`F` 付きは直接 binary32、接尾辞なしは binary64 に丸めます。オーバーフローと受理する binary64 範囲外はコンパイルエラーです。`double` では表現できても `float` には小さすぎる値は、`1e-100f` のようにゼロになる場合があります。このリテラル規則は、実行時の文字列解析 API がゼロへのアンダーフローを受理する意味ではありません。

## 文字リテラル

文字リテラルの静的型は、言語の符号付き 8 ビット整数型 `char` です。

```VeroScript
char letter = 'A';
char newline = '\n';
char byte_ff = '\xFF';
```

直接記述する文字は 1 ASCII バイトでなければなりません。`\xNN` は 8 ビットのビットパターンを正確に書き込むため、`\x80`～`\xFF` は負の `char` 値になります。空、複数文字、非 ASCII、Unicode エスケープ、および `L'A'` のようなワイド文字形式は拒否されます。

## 文字列とワイド文字列

通常の文字列には二重引用符が使用されます。ワイド文字列では、開始引用符の前に `L` が追加されます。

```VeroScript
string text = "VeroScript";
wstring wide_text = L"text";
```

文字列リテラルは、改行や引用符を含むエスケープ文字をサポートします。

```VeroScript
string message = "first line\nsecond line";
string quoted = "say \"hello\"";
```

| エスケープ | `string` | `wstring` | `char` |
| --- | --- | --- | --- |
| `\\`、`\"`、`\'` | バックスラッシュまたは引用符 | 同左 | 同左 |
| `\0`、`\a`、`\b`、`\f`、`\n`、`\r`、`\t`、`\v` | 1 バイト | 1 UTF-16 コード単位 | 1 バイト |
| `\xNN` | 生の 1 バイト | `U+00NN` | 8 ビットパターン |
| `\uNNNN`、`\UNNNNNNNN` | Unicode スカラーを UTF-8 化 | Unicode スカラーを UTF-16 化 | 非対応 |

未知または不完全なエスケープはコンパイル エラーです。Unicode エスケープではサロゲート値と `U+10FFFF` を超える値を拒否します。`string` は長さ付きバイト列です。ソース テキストとテキスト API は UTF-8 を使いますが、`\0` と `\xNN` は埋め込み NUL や任意のバイトを生成できるため、実行時の `string` が常に正しい UTF-8 とは限りません。`wstring` は長さ付き UTF-16 コード単位列です。明示的な UTF-8/UTF-16 変換は不正入力を黙って置換せず、エラーを報告します。

文字列リテラルはソース行にまたがることはできません。 `string` と `wstring` は異なるタイプであるため、直接混合することはできません。必要に応じて明示的な変換を使用します。

配列、マップ、セット、および明示的な型の波括弧初期化子では、`{1, 2,}` や `{'A': 1,}` のように末尾のコンマを 1 つ記述できます。空の初期化子は `{}` のままです。先頭のコンマ、連続するコンマ、関数引数の末尾コンマは使用できません。

## ブール値と null

ブール値リテラルは `true` および `false` です。

```VeroScript
bool enabled = true;
bool finished = false;
```

`nullptr` は、null オブジェクト ハンドルまたは null ランタイム値を表します。これは、クラス オブジェクトおよび null 許容ランタイム値とともによく使用されます。

```VeroScript
class Node
{
public:
	int value;
};

int main()
{
	Node node = nullptr;
	if (node == nullptr)
	{
		print("empty");
	}
}
```

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/literals.md) · [fr-FR](../../fr-FR/language-basics/literals.md) · [ja-JP](literals.md) · [ko-KR](../../ko-KR/language-basics/literals.md) · [zh-CN](../../zh-CN/language-basics/literals.md) · [zh-TW](../../zh-TW/language-basics/literals.md)
