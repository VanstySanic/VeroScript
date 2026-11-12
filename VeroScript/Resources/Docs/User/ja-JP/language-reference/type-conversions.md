# 型変換

型変換は、式を別の型の値として使用できるかどうかを決定します。変換が有効かどうかは、ソースの種類、ターゲットの種類、および式のコンテキストによって異なります。

## 数値型

互換性のある整数型と浮動小数点型は、言語の数値変換規則に従って、代入、引数の受け渡し、演算に参加できます。固定幅の整数は、明示的なデータ幅境界で役立ちます。より狭い型または符号なし型に変換する場合は、値の範囲が切り捨てられたり、予期せず変更されたりしないことを確認してください。

```VeroScript
int count = 10;
double total = count;
int rounded = to_int(total);
```

意図が重要な場合は、明示的なターゲット タイプの構築または変換フォームを使用します。数値変換の代わりに文字列の連結や暗黙的な変換を使用しないでください。

## 変換コンテキスト

VeroScript では、自動的に許可される暗黙変換と、明示的に要求する変換を区別します。

| コンテキスト | 目的 | 主な規則 |
| --- | --- | --- |
| 代入、引数、戻り値 | 暗黙変換 | 互換性のある数値変換と基底型・インターフェイスへの変換を考慮します。`explicit` でない単一値コンストラクターも参加できます。 |
| `cast<T>(value)` | 明示的な変換または構築 | 数値、スコープ付き enum、参照、インターフェイス、明示的コンストラクターを扱い、ユーザー定義変換演算子も要求できます。検査付き参照キャストに失敗すると実行時エラーになります。 |
| `try_cast<T>(value)` | 失敗可能な変換 | `optional<T>` を返します。互換性のない参照変換は空の optional になり、`explicit` コンストラクターは呼び出しません。 |
| `const_cast<T>(value)` | const アクセス能力の除去 | ソースは const で、`T` と同じ正規化型である必要があります。値の変換や実行時型検査は行いません。 |

ユーザー定義変換は連鎖しません。`A` から `B`、さらに `B` から `C` の変換が存在しても、`A` から `C` へ暗黙に二段階変換されることはありません。

## テキストの種類

`string` と `wstring` は別個の型であり、代入、比較、または連結で直接混合することはできません。明示的な標準ライブラリ変換関数を使用します。

```VeroScript
string text = "hello";
wstring wide = to_wstring(text);
string restored = to_string(wide);
```

数値またはブール値をテキストに変換する場合は、混合型の暗黙的な連結に依存するのではなく、`to_string` を使用します。

## 変換コンストラクターと変換演算子

値パラメーターを 1 つ受け取るコンストラクターは、`explicit` が付いていなければ暗黙変換に参加できます。

```VeroScript
struct Meter
{
	int value;

	Meter(int input)
	{
		value = input;
	}
};

Meter assigned = 7;
```

直接構築または明示的な `cast<T>` でのみ呼び出したい場合は `explicit` を付けます。

```VeroScript
struct UserId
{
	int value;

	explicit UserId(int input)
	{
		value = input;
	}
};

UserId direct(7);
UserId converted = cast<UserId>(8);
// UserId invalid = 9; // Compile error.
```

`operator bool()` のようなユーザー定義変換演算子も、言語が許可する変換コンテキストに参加できます。暗黙変換が予測しやすくなるよう、意外な変換演算子は避けてください。
`cast<T>` は一致するユーザー定義変換演算子を明示的に要求できます。`try_cast<T>` は、ドキュメントに記載されたプリミティブ型、スコープ付き enum、および参照/インターフェイスのケースに限定され、ユーザー定義コンストラクターや変換演算子は呼び出しません。

## `cast<T>`

`cast<T>(value)` は明示的な変換を要求します。互換性のある数値、スコープ付き enum と基底整数型、検査付きクラス・インターフェイス参照、対象 struct/class の明示的な構築に使用できます。

```VeroScript
double precise = cast<double>(7);
int narrowed = cast<int>(2.9);

enum class Mode
{
	Idle = 3
};

Mode mode = cast<Mode>(3);
int raw_mode = cast<int>(mode);
```

downcast や cross-cast が必要なクラス・インターフェイス参照では実行時関係を検査します。失敗すると実行時エラーになります。struct から interface への変換は値表現を作成し、class から interface への変換は共有されるクラス ID を保持します。

`cast<T>` は const アクセスを除去しません。const のソースから得られた結果も const として扱われます。意図的な const 解除には `const_cast<T>` を使用します。

構造体値へのキャストが成功すると、値セマンティクスに従います。

## 括弧付きキャスト構文

`(T)式` は `cast<T>(式)` の別表記です。数値、スコープ付き列挙型、参照/インターフェイス、
明示的な構築、および対応するユーザー定義変換演算子を含め、同じ明示的変換規則に従います。

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);

int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print("a={}, calls={}", a, calls);
	return 0;
}
```

この形式は単項式の位置で認識されます。単純なユーザー型名は値式と曖昧になる場合があるため、
対象型が明らかでないときは `cast<T>` を使用してください。

`(void)式;` は文の位置でだけ使用できる、結果を明示的に破棄するための構文です。式は正確に
1 回評価され、副作用と実行時エラーを保持したまま、結果だけが破棄されます。`for` の初期化
文でも使用できます。これは値式ではないため、引数、代入値、算術オペランド、入れ子の式には
使用できません。通常の `式;` と実行時の動作は同じであり、`(void)` は結果を破棄する意図を
明示します。

## `try_cast<T>`

`try_cast<T>(value)` は、プリミティブ、スコープ付き enum、参照・インターフェイス変換の失敗を値で扱う形式です。

```VeroScript
class Base
{
};

class Derived : public Base
{
};

class Other
{
};

Base base = Derived();
optional<Derived> found = try_cast<Derived>(base);
optional<Other> missing = try_cast<Other>(base);

if (found.has_value())
{
	print("derived");
}

if (!missing.has_value())
{
	print("not compatible");
}
```

`try_cast` は、変換に成功すると engaged optional を返します。検査付き参照変換に失敗すると空の optional になります。null 参照の変換は nullable なターゲットへの成功した変換であり、`nullptr` を含む engaged optional になる場合があります。数値変換では変換後の値が生成され、たとえば `try_cast<int>(2.9)` には `2` が入ります。

`try_cast<T>` は `explicit` コンストラクターを選択しません。明示的な構築を行う場合は `cast<T>` を使用してください。

## `const_cast<T>`

`const_cast<T>(value)` は限定的な const エスケープです。ソース式は const であり、エイリアス解決後の正規化型が `T` と完全に一致する必要があります。

```VeroScript
class Counter
{
public:
	int value = 41;
};

int main()
{
	readonly<Counter> stored = Counter();
	const_cast<Counter>(stored.get()).value = 42;
	print(stored.get().value);
	return 0;
}
```

この例では、共有されるクラスオブジェクトのフィールドを変更しています。`const_cast<int>(view)` は代入可能な参照ではなくスカラー値を生成するため、`const_cast<int>(view) = 42` は無効です。スカラー値のコピーが元のストレージへの別名になるわけではありません。

この操作はコンパイル時の const アクセス能力だけを除去します。値を変換したり、実行時型を変更したり、検査付きキャストを行ったりしません。制御された `readonly<T>` ラッパーなど、意図的に書き換え可能なストレージに限って使用してください。真に不変なストレージに適用すると、`const` が提供する保護を無効にします。

次の形式は拒否されます。

```VeroScript
const int number = 1;
// const_cast<double>(number); // The target type must be exactly int.
// const_cast<int>(mutable_value); // The source must be const.
```

## 構造体、クラス、インターフェイス、コンテナー

構造体の変換は値のセマンティクスに従い、構造体の値のコピーを作成します。それ自体が共有ハンドルである構造体フィールドは、そのフィールドのハンドル セマンティクスを保持します。クラス変換では、共有オブジェクトのアイデンティティが保持されます。

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Point : IReadable
{
	int value;
	int read() const override
	{
		return value;
	}
};

Point point = Point();
IReadable readable = point;
```

構造体をインターフェイスに変換すると、インターフェイスに必要な値表現が作成されます。クラスをインターフェイスに変換すると、クラス オブジェクトの共有アイデンティティが保持されます。具象型は、インターフェイスに必要なすべての純粋仮想メソッドを実装する必要があります。

コンテナ要素、キー、および値の型は、その宣言の規則を満たす必要があります。ジェネリック関数またはジェネリック型は、引数が宣言された制約を満たす場合にのみインスタンス化できます。それ以外の場合、`where` 制約は変換ルールを緩和しません。

## 依存しない変換

`nullptr` は、NULL 許容ハンドル型にのみ適用されます。テキスト型は通常の代入では交換できません。また、フィールドが似ているという理由だけで構造体の値をクラス ハンドルと交換することもできません。ホスト バインディングおよびインターフェイス境界での明示的なターゲット タイプを優先します。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-reference/type-conversions.md) · [fr-FR](../../fr-FR/language-reference/type-conversions.md) · [ja-JP](type-conversions.md) · [ko-KR](../../ko-KR/language-reference/type-conversions.md) · [zh-CN](../../zh-CN/language-reference/type-conversions.md) · [zh-TW](../../zh-TW/language-reference/type-conversions.md)
