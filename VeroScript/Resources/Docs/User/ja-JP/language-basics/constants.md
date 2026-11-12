# 定数

定数は、宣言された名前によって変更できない変数またはメンバーです。 VeroScript は定数に `const` を使用します。

`const` は読み取り専用を指定し、コンパイル時初期化は要求しません。対応するスカラー値をコンパイル時に確定するには [constexpr](../language-reference/constexpr.md) を使用します。

## 定数変数

`const` は、ローカル変数およびモジュールレベル変数に使用できます。定数は宣言時に初期化する必要があります。

```VeroScript
const int max_count = 100;

int main()
{
	const string product_name = "VeroScript";
	print(product_name);
}
```

初期化後は、その名前を使用して定数を割り当てることはできません。

```VeroScript
const int limit = 10;
// limit = 20; // Compile error.
```

定数には、その宣言に記述された型がそのまま残ります。 `const` は、値を変換したり、そのスコープを変更したりしません。

## 定数メンバー

構造体またはクラスは定数フィールドを宣言できます。

```VeroScript
class Settings
{
public:
	const int version = 1;
};
```

宣言初期化子を持たない非静的フィールドは、コンストラクター初期化リストで初期化できます。

```VeroScript
class Settings
{
public:
	const int version;

	Settings(int initial_version)
		: version(initial_version)
	{
	}
};
```

初期化リストで指定できるのは、現在の型が直接宣言したインスタンスフィールドだけです。コンストラクター初期化リスト内の直接の基本型項目が先に実行され、その後、このリスト内のフィールド項目がフィールドの宣言順に実行されてから、コンストラクター本体が実行されます。宣言初期化子を持つフィールドをこのリストでも初期化することや、同じフィールドを二度指定することはできません。コンストラクター本体での書き込みは通常の代入のままなので、`const` フィールドを変更できません。宣言初期化子およびリストで省略されたフィールドには、既存のデフォルト構築規則が引き続き適用されます。

## `const` メンバー関数

メンバー関数は、パラメーター リストの後に `const` を使用して、現在のオブジェクトの可変状態を変更できないことを示すことができます。

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
```

Const メンバー関数はクエリに適しています。メンバー関数を呼び出せるかどうかは、オブジェクトの定数と関数の `const` 修飾子によって決まります。

### `mutable` の例外

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

この例外は指定したフィールドだけに適用されます。const オブジェクト全体を書き換え可能にしたり、並行アクセスの安全性を保証したりするものではありません。`mutable` フィールドを `const`、`static`、参照フィールドと併用することはできません。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/constants.md) · [fr-FR](../../fr-FR/language-basics/constants.md) · [ja-JP](constants.md) · [ko-KR](../../ko-KR/language-basics/constants.md) · [zh-CN](../../zh-CN/language-basics/constants.md) · [zh-TW](../../zh-TW/language-basics/constants.md)
