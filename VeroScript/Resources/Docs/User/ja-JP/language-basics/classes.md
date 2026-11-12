# クラス

`class` は、共有 ID を持つユーザー タイプを定義します。クラス値はハンドルであり、null にすることもでき、フィールド、メソッド、コンストラクター、デストラクター、継承、および仮想メソッドをサポートします。構造体とは異なり、クラス変数を割り当てると、通常、オブジェクト自体ではなくオブジェクト ハンドルがコピーされます。

## 宣言とアクセス制御

クラスにはフィールドとメンバー関数を含めることができます。クラス メンバーはデフォルトで `private` であるため、呼び出し元に公開されるインターフェイスには明示的な `public` セクションを使用します。

```VeroScript
class Counter
{
	int value = 0;

public:
	void add(int amount)
	{
		value += amount;
	}

	int get() const
	{
		return value;
	}
};
```

メンバーは、`protected` または `private` にすることもできます。アクセス指定子は、クラス、派生クラス、および外部コードにどのメンバーが表示されるかを制御します。 `friend` は、指定された関数またはクラスへの追加アクセスを許可できます。

## 構築と null オブジェクト

クラスは 1 つ以上のコンストラクターを宣言できます。構築式は新しいクラス オブジェクトを作成します。

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}
};

Counter counter = Counter(10);
```

コンストラクター初期化リストでは、コンストラクター本体より前に直接の基本型コンストラクターを呼び出し、直接のインスタンスフィールドを初期化できます。このリストのフィールド項目は、フィールドの宣言順に初期化されます。宣言初期化子を持つフィールドをこのリストでも初期化することはできません。`const` フィールドの規則については、[定数](constants.md)を参照してください。

構築中または破棄中にそのオブジェクトの仮想メソッドを呼ぶと、現在実行中のコンストラクターまたはデストラクターの型の実装が使われます。ヘルパーやバインドされたメソッド経由の呼び出しも同じです。基本型のコンストラクターから、まだ初期化されていない派生部分のオーバーライドは呼ばれません。その段階で実装のない純粋仮想メソッドを呼ぶと、実行時エラーになります。構築完了後の通常の動的ディスパッチは、実際の派生型を使用します。

構築に失敗した型のデストラクターは実行されません。初期化済みのメンバーと構築済みの基本型は、構築の逆順で解放されます。

クラス ハンドルには `nullptr` を含めることができます。

```VeroScript
Counter empty = nullptr;
if (empty == nullptr)
{
	print("empty");
}
```

null ハンドルを介してインスタンス メンバーを呼び出すと、実行時エラーが発生します。ハンドルを使用する前に、ハンドルが有効なオブジェクトを参照していることを確認してください。

## 共有アイデンティティ

あるクラス変数を別のクラス変数に代入すると、両方の変数が同じオブジェクトを参照するようになります。

```VeroScript
class Box
{
public:
	int value = 0;

	void set(int next)
	{
		value = next;
	}
};

Box first = Box();
Box second = first;
second.set(10);

print(first.value);  // 10
```

独立した ID が必要な場合は、別のオブジェクトを構築します。ハンドルをコピーしないでください。クラス フィールド、コンテナー内のクラス値、およびクラス パラメーターは、同じ共有 ID セマンティクスに従います。

## Const オブジェクトとクエリ メソッド

メンバー関数は `const` で修飾できます。これは、現在のオブジェクトを通じてメンバーを変更できないことを意味します。 const オブジェクトは、const オブジェクトに対して有効なメソッドのみを呼び出すことができます。

```VeroScript
class Counter
{
public:
	int value = 0;

	int get() const
	{
		return value;
	}
};

const Counter counter = Counter();
print(counter.get());
```

オブジェクトの状態のみを検査する操作には、`const` メンバー関数を優先します。可変メンバー関数は、`const` オブジェクトを通じて呼び出すことはできません。

### `mutable` 実装フィールド

`mutable` は `struct` または `class` の非 static インスタンス フィールドに対する文脈依存のフィールド指定子です。`const` メンバー関数からそのフィールドだけを更新できます。

```VeroScript
class Label
{
	string source;
	mutable string cached;

public:
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

`mutable` はオブジェクト全体を書き換え可能にせず、スレッド セーフティを提供せず、`const`、`static`、参照フィールドとは併用できません。グローバルに予約された語ではなく、フィールド宣言の外では文法が許す限り通常の識別子として使用できます。

## 継承と仮想メソッド

クラスは 1 つ以上の基本クラスから継承し、インターフェイスを実装できます。動的にディスパッチされるメソッドには `virtual` を使用し、基本メソッドのオーバーライドを明示的にするには `override` を使用します。

```VeroScript
class Base
{
public:
	virtual int value() const
	{
		return 1;
	}
};

class Derived : public Base
{
public:
	int value() const override
	{
		return 2;
	}
};

int main()
{
	Base object = Derived();
	print(object.value());
}
```

抽象クラスは、`virtual int value() const = 0;` などの純粋仮想メソッドを宣言できます。実装されていない純粋な仮想スロットを持つクラスはインスタンス化できません。仮想デストラクターは、派生オブジェクトが基本クラス ID を通じて破棄されるときに、正しい破棄パスを保証します。

インターフェイス、継承アクセス、および多重継承の完全なルールは、[インターフェイス](interfaces.md) ドキュメントで説明されています。

## デストラクター

クラスは、オブジェクトの有効期間が終了したときに実行されるデストラクターを `~TypeName()` として宣言できます。デストラクターは仮想または純粋仮想にすることもできます。継承が関係する場合は、オブジェクトがどのように破棄されるかに応じて仮想デストラクターの設計を選択します。


## クラス外の定義

クラス内でメンバーを宣言し、同じソースモジュール内の後方で `Type::member` を使って定義できます。パラメーター型、戻り値型、参照修飾、`const` は宣言と一致する必要があります。コンストラクターには戻り値型がありません。これはソース内の定義であり、宣言と実装を別々にリンクするスクリプトモジュールに分割しないでください。

```VeroScript
class Counter
{
public:
	Counter(int seed);
	int Add(int delta) const;
	int value;
};

Counter::Counter(int seed)
{
	this.value = seed;
}

int Counter::Add(int delta) const
{
	return this.value + delta;
}

void main()
{
	Counter counter = Counter(6);
	print(counter.Add(1));
}
```

入れ子の型、ローカル宣言、アクセス規則、各階層のジェネリック引数については、[型のスコープ](scope.md)を参照してください。

## 自己参照フィールド

ジェネリッククラスは、自身の特殊化を参照するハンドルをフィールドに保持できます。フィールドは `nullptr` で初期化でき、別のオブジェクトを代入してもその同一性が保たれます。

```VeroScript
class Node<T>
{
public:
	T value;
	Node<T> next = nullptr;
};
```

構造体フィールドの直接または相互の値による再帰は引き続き禁止されます。循環継承や無限に展開するテンプレートも拒否され、コンパイル深度の制限も適用されます。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/classes.md) · [fr-FR](../../fr-FR/language-basics/classes.md) · [ja-JP](classes.md) · [ko-KR](../../ko-KR/language-basics/classes.md) · [zh-CN](../../zh-CN/language-basics/classes.md) · [zh-TW](../../zh-TW/language-basics/classes.md)
