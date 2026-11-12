# データ型

型によって、値の表現方法、値に適用できる操作、および許可される変換が決まります。 VeroScript タイプは、組み込みタイプ、コンテナタイプ、およびユーザー定義タイプにグループ化されます。

## 組み込み型

|カテゴリー |種類 |
| --- | --- |
|値なし | `void` |
|ブール値 | `bool` |
|符号付き整数 | `char`、`short`、`int`、`long` |
|符号なし整数 | `unsigned char`、`unsigned short`、`unsigned int`、`unsigned long` |
|固定幅の整数 | `int8`、`uint8`、`int16`、`uint16`、`int32`、`uint32`、`int64`、`uint64` |
|浮動小数点 | `float` (バイナリ 32)、`double` (バイナリ 64) |
|テキスト | `string`、`wstring` |

`void` は関数の戻り値の型としてのみ使用できます。変数には使用できません。固定幅整数と `float`/`double` は、ドキュメント専用のエイリアスではなく、別個の言語タイプです。これらは、明示的な幅または浮動小数点精度を必要とするインターフェイスやデータ構造に役立ちます。

固定幅の整数の範囲は、`int8` の場合は `-128` ～ `127`、`uint8` の場合は `0` ～ `255`、`int16` の場合は `-32768` ～ `32767`、および `0` ～ `uint16` の場合は `65535`。 `int32`、`uint32`、`int64`、および `uint64` は、対応する符号付きおよび符号なしの 32 ビットおよび 64 ビット範囲を使用します。ターゲット範囲外のリテラルはエラーになります。切り捨てに依存しないでください。

```VeroScript
int8 signed_byte = -128;
uint8 byte = 255u;
float single = 2.05f;
double precise = 2.05;
```

`f`/`F` サフィックスを持つ浮動小数点リテラルは、`float` として保存されます。接尾辞のない浮動小数点リテラルは、`double` として保存されます。したがって、それらの丸め動作は異なる場合があります。

## コンテナの種類

コンテナ型は、型引数を使用して要素またはキー/値の型を記述します。

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
set<string> names = set<string>();
queue<int> pending = queue<int>();
```

VeroScript は次のコンテナ カテゴリを提供します。

- シーケンス コンテナー: `array`、`deque`。
- 連想コンテナ: `map`、`multimap`、`ordered_map`、`ordered_multimap`。
- コンテナーを設定します: `set`、`multiset`、`ordered_multiset`。
- アダプター: `queue`、`priority_queue`、`stack`。
- 同時コンテナ: `channel`。

コンテナ操作、要素のセマンティクス、および同時実行ルールは、コンテナおよび同時実行の参照に属します。

## ネイティブ配列

固定サイズのネイティブ配列は、要素タイプの後に次元を置きます。

```VeroScript
int numbers[3] = {10, 20, 30};
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
```

`T[N]` は型にその長さを含みますが、`array<T>` は動的配列コンテナーです。どちらもインデックスを作成できますが、タイプが異なります。

## ユーザー定義型

ユーザーは、`struct`、`class`、`interface`、および `enum` を使用して新しいタイプを定義できます。

```VeroScript
struct Point
{
	int x;
	int y;
};

enum class Color
{
	Red,
	Green,
	Blue
};
```

`struct` には値セマンティクスがあり、`class` には共有 ID があります。フィールド、メソッド、およびオブジェクトのセマンティクスについては、[構造体](structs.md) および [クラス](classes.md) を参照してください。これらの型形式については [Interfaces](interfaces.md) および [Enums](enums.md) を参照してください。ジェネリックについては個別に説明します。

## `auto`

`auto` は、変数の型をその初期化子から推測します。

```VeroScript
auto count = 3;
auto values = array<int>{1, 2, 3};
```

`auto` 宣言には初期化子が必要です。推論は宣言時に行われます。変数はその後も明確な静的型を持ちます。

## 型変換

互換性のある数値型は、言語規則に従って変換できます。 `string` と `wstring` を暗黙的に混合することはできません。テキスト タイプ間を移動する場合は、適切な明示的な変換関数を使用してください。

変換が許可されるかどうかは、ソースの種類、ターゲットの種類、および式のコンテキストによって異なります。ホスト バインディングとジェネリック インターフェイスでは、可能な場合は暗黙的な変換に依存するのではなく、明示的な型を使用する必要があります。

## 読み取り専用ラッパー

`readonly<T>` は `T` を値として保持する標準ラッパーです。ラッパー自体はコピーやコンテナーへの格納が可能で、`const ref T get() const` を通じて読み取り専用に公開されます。

```VeroScript
readonly<int> count = readonly<int>(41);
const ref int view = count.get();
print(view);
```

暗黙のデフォルト コンストラクターはありません。struct では値をコピーして保持し、class ではクラス ハンドルを保持して共有 ID を維持します。意図的な `const_cast<T>` のエスケープについては、[型変換](../language-reference/type-conversions.md)を参照してください。

組み込み型名 `optional`、`readonly`、`weak`、`shared`、`type`、`field_info`、`method_info`、`module_info`、`function_info`、`global_info`、`enum_member_info` は予約されており、スクリプトで再宣言できません。

## コンパイラ提供のユーティリティ型

VeroScript には `optional<T>`、`weak<T>`、`pair<T, U>`、`shared<T>` / `handle<T>` も用意されています。コンストラクター、状態、所有権の動作、およびマップ エントリのセマンティクスについては、[値ユーティリティ型](value-utility-types.md)にまとめています。

`pair<T, U>` と `handle<T>` はソフトなジェネリック組み込みです。同じ名前とジェネリック引数数のユーザー宣言がある場合は、コンパイラ提供の型よりもユーザー宣言が優先されます。これは、上記の厳密に予約された名前とは異なる点です。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/data-types.md) · [fr-FR](../../fr-FR/language-basics/data-types.md) · [ja-JP](data-types.md) · [ko-KR](../../ko-KR/language-basics/data-types.md) · [zh-CN](../../zh-CN/language-basics/data-types.md) · [zh-TW](../../zh-TW/language-basics/data-types.md)
