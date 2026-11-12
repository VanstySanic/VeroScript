# 05: 構造体、クラス、インターフェイス

このチュートリアルでは、小さな読み取り可能なオブジェクトの例を使用して、値の構造体、共有 ID のクラス、置換可能な動作のインターフェイスという 3 つのユーザー定義型を比較します。

## ステップ 1: 構造体で値を表現する

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	int distance_score() const
	{
		return x * x + y * y;
	}
};

int main()
{
	Point point = Point();
	point.x = 3;
	point.y = 4;
	print(point.distance_score());
	return 0;
}
```

構造体は通常、値のグループを表します。構造体を別の変数に割り当てることは、通常、値のコピーとして理解され、座標、設定、および小さなレコードに適しています。

## ステップ 2: クラスとの共有 ID を表現する

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}

	void add(int amount)
	{
		value = value + amount;
	}
};

int main()
{
	Counter first = Counter(10);
	Counter second = first;
	second.add(5);
	print(first.value);
	return 0;
}
```

`first` と `second` は同じクラス オブジェクトを参照しているため、これにより `15` が出力されます。クラス割り当ては、独立したオブジェクトではなく、オブジェクト ID をコピーします。別の状態が必要な場合は、別のオブジェクトを構築します。

クラスのメンバーはデフォルトではプライベートであるため、この例ではコンストラクターとメンバーに `public:` を使用します。大規模なプログラムでは、呼び出し元が代わりに制御された操作を使用する必要がある場合は、フィールドを非公開にしておきます。

### 変換コンストラクターと `explicit`

値パラメーターを 1 つ持つ非 `explicit` コンストラクターは、暗黙の変換に参加できます。直接構築または `cast<T>` でのみ構築させる場合は `explicit` を付けます。

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
```

`UserId invalid = 9;` は拒否されます。これによりオーバーロード解決が予測可能になり、ラッパー型の意図しない構築を防げます。

`(T)式` は `cast<T>(式)` の構文糖であり、まったく同じ変換規則に従います。

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);
```

単純なユーザー型名が同名の値式と曖昧になる場合は `cast<T>` を使用してください。`(void)式;` は文の位置でだけ使用できる、結果を明示的に破棄する構文です。式を 1 回評価し、副作用と実行時エラーを保持したまま結果だけを破棄します。通常の `式;` も実行時の動作は同じです。`for` の初期化文でも使用できますが、`(void)式` を入れ子の値や関数の引数として使用することはできません。

```VeroScript
int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print(calls);
	return 0;
}
```

### `mutable` キャッシュ フィールド

`const` メソッドがキャッシュなどの実装状態を更新する必要がある場合に限り、非 static の `struct` または `class` インスタンス フィールドへコンテキスト依存の `mutable` を使用します。これはオブジェクト全体を書き込み可能にするものでも、スレッド セーフにするものでもなく、`const`、`static`、参照フィールドとは併用できません。

```VeroScript
struct Label
{
	string source;
	mutable string cached;

	string text() const
	{
		if (cached == "")
		{
			cached = source;
		}
		return cached;
	}
};
```

### 移植可能な読み取り専用ペイロード

`readonly<T>` は `T` を通常の値として保持するため、ラッパー自体をコピーしたりコンテナーに格納したりできますが、`get()` が公開するのは `const T&` だけです。`readonly<Struct>` は構造体の値を取り込み、`readonly<Class>` はクラス ハンドルを保持します。

`const_cast<T>(value)` は、元の式が const で、正規化された実行時型が `T` と同じ場合にのみ使用してください。変換や実行時型チェックは行わず、コンパイル時の const アクセス能力だけを取り除きます。通常の `cast<T>` は意図的に const アクセスを維持します。

前の `Label` 宣言を残し、この手順では以下の `main` を使います。読取専用 wrapper の前に `label` を作成し、最初に `source` を表示します。

```VeroScript
int main()
{
	Label label = Label();
	label.source = "source";
	readonly<Label> published = readonly<Label>(label);
	print(published.get().text());

	// Deliberate escape: mutate the payload stored by the wrapper.
	const_cast<Label>(published.get()).cached = "recomputed";
	return 0;
}
```

### `cast` と `try_cast` の使い分け

変換が必要で、検査付き参照変換の失敗をエラーにしたい場合は `cast<T>` を使います。関係が成立しない可能性があり、呼び出し側で `optional<T>` を確認したい場合は `try_cast<T>` を使います。

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

int main()
{
	Base value = Derived();
	optional<Derived> found = try_cast<Derived>(value);
	optional<Other> missing = try_cast<Other>(value);

	if (found.has_value())
	{
		print("derived");
	}
	if (!missing.has_value())
	{
		print("not compatible");
	}
	return 0;
}
```

`try_cast<T>` はプリミティブ、スコープ付き enum、参照/インターフェイス変換を扱いますが、`explicit` コンストラクターは呼び出しません。完全な規則は[型変換](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-reference/type-conversions.md)を参照してください。


## ステップ 3: インターフェースを使用して動作を記述する

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Sensor : public IReadable
{
public:
	int value = 42;

	int read() const override
	{
		return value;
	}
};

int read_twice(IReadable value)
{
	return value.read() + value.read();
}

int main()
{
	Sensor sensor = Sensor();
	print(read_twice(sensor));
	return 0;
}
```

インターフェイスはオブジェクトを作成しません。実装が提供しなければならないものを示します。 `read_twice` は、1 つの具体的なセンサー タイプではなく、`IReadable` コントラクトに依存します。

## ステップ 4: 静的ユーティリティ型を定義する

型を処理や定数の名前付きコンテナーとしてだけ使う場合は、`static class` または `static struct` を使用します。宣言されたすべてのメンバーが静的として扱われるため、オブジェクトを作成せずに型名と `::` で呼び出します。

```VeroScript
public static class Math
{
	public int Base = 40;
	public int Add(int value)
	{
		return Math::Base + value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Add(2));
	print(Limits::MaxRetries);
}
```

静的ユーティリティ型は、構築、継承、ジェネリック型としての宣言ができません。コンストラクター、デストラクター、`using` メンバー、デフォルト初期化ブロック、`mutable` フィールド、`virtual` や `const` などのインスタンス修飾子も使用できません。オブジェクトごとの状態や継承が必要な場合は、通常の `class` または `struct` を使用してください。

## 現実的な選択

- 独立した値のコピーが役立つ場合は、`struct` を使用します。
- プログラムのいくつかの部分が変更可能な ID を共有する必要がある場合は、`class` を使用します。
- 呼び出し元が具体的な実装ではなく動作に依存する必要がある場合は、`interface` を使用します。
- オブジェクト ID やインスタンス状態が不要な場合は、静的ユーティリティ型を使用します。

## 演習

1. `move` メソッドを `Point` に追加します。
2. `reset` を `Counter` に追加し、`value` をプライベートのままにします。
3. `FixedSensor` という名前の 2 番目の `IReadable` 実装を作成します。

[構造体](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/structs.md)、[クラス](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/classes.md)、および [インターフェイス](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/interfaces.md) を参照してください。組み合わせた例は [`examples/object_basics.vs`](../../../../../VeroScript/Resources/Scripts/examples/object_basics.vs) です。

次: [参照、参照の戻り値、およびラムダ キャプチャ](06-references-and-captures.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/language/05-structs-classes-and-interfaces.md) · [fr-FR](../../fr-FR/language/05-structs-classes-and-interfaces.md) · [ja-JP](05-structs-classes-and-interfaces.md) · [ko-KR](../../ko-KR/language/05-structs-classes-and-interfaces.md) · [zh-CN](../../zh-CN/language/05-structs-classes-and-interfaces.md) · [zh-TW](../../zh-TW/language/05-structs-classes-and-interfaces.md)
