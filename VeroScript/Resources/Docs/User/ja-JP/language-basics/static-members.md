# 静的メンバー

`static` で宣言されたメンバーは、1 つのオブジェクト インスタンスではなく型自体に属します。静的フィールドには型レベルの格納場所が 1 つあり、静的メソッドにはオブジェクト レシーバーは必要ありません。

## 静的ユーティリティ型

`static class` と `static struct` は、インスタンス化できないユーティリティ コンテナーを宣言します。内部で宣言したすべてのフィールドとメソッドは静的として扱われ、型名と `::` を使用してアクセスします。

```VeroScript
public static class Math
{
	public double Square(double value)
	{
		return value * value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Square(4.0));
	print(Limits::MaxRetries);
}
```

静的クラスまたは静的構造体は、構築することも基底型として使用することもできません。ジェネリックにすること、コンストラクターやデストラクター、`using` メンバー、デフォルト初期化ブロックを宣言することもできません。フィールドに `mutable` を指定することはできず、メソッドでは `virtual`、`override`、`final`、純粋仮想、`const` などのインスタンス修飾子を使用できません。アクセス セクションは引き続き適用されます。静的クラスはクラスのデフォルト アクセス、静的構造体は構造体のデフォルト アクセスに従います。インスタンス、継承、インスタンス状態が必要な場合は、通常の `class` または `struct` を使用してください。

## 静的フィールド

型名と `::` を使用して静的フィールドにアクセスします。以下の `Total` フィールドは、すべての `Counter` インスタンスで共有されます。

```VeroScript
class Counter
{
public:
	static int Total = 0;
	int id;

	Counter()
	{
		Counter::Total = Counter::Total + 1;
		id = Counter::Total;
	}
};

Counter first = Counter();
Counter second = Counter();
print("{} {}", first.id, Counter::Total);
```

静的フィールドは引き続きアクセス制御の対象となります。型外のコードがアクセスする必要がある場合は、`public` セクションでフィールドを宣言します。

## 静的メソッド

呼び出し用のオブジェクトを作成せずに、型名を使用して静的メソッドを呼び出します。

```VeroScript
class Counter
{
public:
	static int Total = 0;

	static int Peek()
	{
		return Counter::Total;
	}
};

int current = Counter::Peek();
```

静的メソッドには暗黙的なインスタンス オブジェクトがないため、非静的フィールドに直接アクセスしたり、インスタンス依存のメンバーを呼び出したりすることはできません。インスタンス メンバーが必要な場合は、オブジェクトを明示的に渡します。

```VeroScript
class Formatter
{
public:
	string prefix;

	static string with_prefix(Formatter formatter, string value)
	{
		return formatter.prefix + value;
	}
};
```

## ジェネリック型の静的メンバー

ジェネリック型の各具象型インスタンスには、独立した静的ストレージがあります。たとえば、`Box<int>::Count` と `Box<string>::Count` は別の値です。

```VeroScript
class Box<T>
{
public:
	static int Count = 0;
	T value;
};

Box<int>::Count = Box<int>::Count + 1;
Box<string>::Count = Box<string>::Count + 1;
```

静的メンバーを使用する場合は、完全な型名で修飾することをお勧めします。これにより、メンバーがオブジェクト インスタンスではなく型に属していることが明確になります。

## メンバーの既定値と初期化

インスタンスフィールドの既定値は、そのインスタンスの通常の構築時に評価します。ジェネリック型を含め、所有する型で後に宣言した静的メソッドを呼べます。静的な値はコンパイル時のスナップショットではなく、その時点の値を読みます。別のインスタンスを作っても型の静的フィールドは初期化し直しません。アクセス制御、const、コンストラクター初期化の規則は維持します。

```VeroScript
class Sample<T>
{
public:
	static int Seed = 7;
	T snapshot = Sample<T>::Read();

	static T Read()
	{
		return cast<T>(Sample<T>::Seed);
	}
};

int main()
{
	Sample<int> first = Sample<int>();
	Sample<int>::Seed = 11;
	Sample<int> second = Sample<int>();
	print(first.snapshot);
	print(second.snapshot);
	print(first.snapshot);
	return 0;
}
```

出力は行ごとに `7`、`11`、`7` です。

ジェネリック型や入れ子の型の内部では、型自身の字句名は現在の特殊化を参照し、外側の型引数と値引数を保持します。`Outer<int>::Inner` のような明示引数は明示したまま扱います。同じ具体型の別名は静的ストレージを共有します。同名のローカル値は式中で値のままで、先頭の `::` はルートスコープを選びます。[スコープ](scope.md)を参照してください。

型の静的フィールドは各実行状態のモジュール起動時に初期化し、初回到達時に初期化する関数ローカル static とは異なります。特定できるジェネリック静的依存は、それを使う初期化子の前に準備します。Lambda を保存しただけでは本体を呼ばず、null を保持できるフィールドや空コンテナーは要素を構築しません。任意の遅延初期化や無関係な型の全順序とは扱わないでください。依存は単純にし、順序が必要な副作用は `main` で明示します。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/static-members.md) · [fr-FR](../../fr-FR/language-basics/static-members.md) · [ja-JP](static-members.md) · [ko-KR](../../ko-KR/language-basics/static-members.md) · [zh-CN](../../zh-CN/language-basics/static-members.md) · [zh-TW](../../zh-TW/language-basics/static-members.md)
