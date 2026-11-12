# インターフェース

`interface` は、構造体またはクラスが実装できるメンバー コントラクトのセットを宣言します。インターフェイスは直接インスタンス化できませんが、パラメーター、戻り値の型、または変数の型として使用して、さまざまな実装に均一にアクセスできます。

## インターフェース宣言

インターフェイス メソッドは通常、純粋な仮想メソッドとして宣言されます。純粋な仮想メソッドは、`virtual` を使用し、`= 0;` で終わる必要があります。

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};
```

純粋な仮想メソッドはパブリックである必要があります。そのパラメータ リスト、戻り値の型、および `const` 修飾が組み合わされて、実装コントラクトを形成します。

## インターフェースの実装

構造体またはクラスは、継承リスト内のインターフェイスに名前を付け、一致するメソッド実装を提供します。

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Counter : public IReadable
{
public:
	int value = 0;

	int read() const override
	{
		return value;
	}
};
```

`override` を使用して、インターフェイス メソッドの実装を明示的にします。未実装の純粋仮想メソッドがまだあるクラスまたは構造体は、具象オブジェクトとしてインスタンス化できません。

型は複数のインターフェイスを実装できます。

```VeroScript
interface IId
{
	virtual int id() const = 0;
};

interface IName
{
	virtual string name() const = 0;
};

class Person : public IId, public IName
{
public:
	int id() const override
	{
		return 42;
	}

	string name() const override
	{
		return "Ada";
	}
};
```

## デフォルトの実装

インターフェイス メソッドは、デフォルトの実装を形成する本体を提供することもできます。実装型はその実装を継承することも、`override` を使用して独自のバージョンを提供することもできます。

```VeroScript
interface IDescribable
{
	virtual string name() const = 0;

	string describe() const
	{
		return "name=" + name();
	}
};
```

デフォルトのメソッドはインターフェイス コントラクトの一部として残ります。具体的なインスタンス化を妨げるのは、実装されていない純粋な仮想メソッドのみです。

## インターフェースの継承

インターフェイスは 1 つ以上の親インターフェイスから継承できます。

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

interface IResource : IReadable
{
	virtual void close() = 0;
};
```

`IResource` を実装する型は、その型とその親インターフェイスによって宣言されたすべての純粋仮想メソッドを満たす必要があります。インターフェイス継承リスト内のすべての型は、それ自体がインターフェイスである必要があります。

## インターフェイスの値と変換

構造体をインターフェイス型に変換すると、インターフェイスに必要な構造体の値表現が作成されます。そのインターフェイス値にアクセスしても、元の構造体は変更されません。クラスをインターフェイスに変換すると、クラス オブジェクトの共有 ID が保持されます。

```VeroScript
interface ICounter
{
	virtual void add(int amount) = 0;
};

struct ValueCounter : ICounter
{
	int value = 0;
	void add(int amount)
	{
		value += amount;
	}
};

class SharedCounter : ICounter
{
public:
	int value = 0;
	void add(int amount) override
	{
		value += amount;
	}
};
```

したがって、インターフェイス値のコピーと呼び出しは、基礎となる実装に依存します。つまり、構造体インターフェイス値は値のセマンティクスに従い、クラス インターフェイス値はクラス ハンドルのセマンティクスに従います。インターフェイス変数を使用して新しい実装オブジェクトを構築することはできません。

入れ子の型、ローカル宣言、アクセス規則、各階層のジェネリック引数については、[型のスコープ](scope.md)を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/interfaces.md) · [fr-FR](../../fr-FR/language-basics/interfaces.md) · [ja-JP](interfaces.md) · [ko-KR](../../ko-KR/language-basics/interfaces.md) · [zh-CN](../../zh-CN/language-basics/interfaces.md) · [zh-TW](../../zh-TW/language-basics/interfaces.md)
