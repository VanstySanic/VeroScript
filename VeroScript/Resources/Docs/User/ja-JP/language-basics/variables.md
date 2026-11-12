# 変数

変数は、実行時に値を格納する型を持つ名前付き宣言です。変数は、モジュール スコープ、関数またはブロック内、構造体またはクラス メンバーとして宣言できます。

## 宣言

変数宣言では、型、名前、初期化子を指定します。

```VeroScript
int count = 0;
int limit = 10;
string name = "VeroScript";
```

ローカル変数とモジュール変数には初期化が必要です。`int count;` は無効です。値、コンストラクター呼び出し、または波括弧による初期化を指定してください。構造体やクラスのフィールドは初期化子を省略でき、その場合は型のフィールド既定値が使われます。

## 初期化

VeroScript はコンストラクター呼び出しと中括弧の初期化をサポートしています。

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
```

ユーザー定義型は、コンストラクターを呼び出すことで初期化できます。

```VeroScript
struct Point
{
	int x;
	int y;
};

int main()
{
	Point point = Point();
	point.x = 10;
	point.y = 20;
}
```

## 割り当て

宣言後、変数は代入式を使用して変更できます。

```VeroScript
int value = 1;
value = 2;
value += 3;
```

代入の両側が言語の変換規則を満たしている必要があります。代入によって変数の静的型は変更されません。

## ローカル変数とグローバル変数

関数またはブロック内で宣言された変数はローカルです。モジュール スコープで宣言された変数はグローバルであり、同じモジュール内の関数からアクセスできます。

```VeroScript
int global_count = 0;

void increment()
{
	global_count += 1;
}
```

モジュール間のグローバルへのアクセスは、インポートおよびエクスポートのルールの対象となります。スコープは、名前が有効な場所を示します。可視性はモジュール境界とアクセス指定子にも影響されます。

## `auto` 変数

`auto` はイニシャライザから型を推測します。

```VeroScript
auto answer = 42;
auto text = "hello";
```

`auto` 変数を初期化する必要があります。推論されると、静的に型指定された変数のままになり、後で別の型を割り当てることはできません。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/variables.md) · [fr-FR](../../fr-FR/language-basics/variables.md) · [ja-JP](variables.md) · [ko-KR](../../ko-KR/language-basics/variables.md) · [zh-CN](../../zh-CN/language-basics/variables.md) · [zh-TW](../../zh-TW/language-basics/variables.md)
