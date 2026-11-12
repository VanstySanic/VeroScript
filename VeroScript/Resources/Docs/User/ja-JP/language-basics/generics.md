# ジェネリック

ジェネリックスを使用すると、型または関数がその宣言で型パラメーターを使用し、使用場所で具象型を置き換えることができます。 VeroScript は、`where` 型制約とともに、汎用の構造体、クラス、インターフェイス、および関数をサポートします。

## ジェネリック型

型名の後に山かっこで型パラメータを宣言します。

```VeroScript
class Duo<T, U = int>
{
public:
	T first;
	U second;

	Duo(T first_value, U second_value)
	{
		first = first_value;
		second = second_value;
	}
};

Duo<string> text_pair = Duo<string>("score", 10);
Duo<int, string> number_pair = Duo<int, string>(10, "points");
```

後続の型パラメータにはデフォルトの型を指定でき、デフォルトは以前の型パラメータを参照できます。明示的な型引数はプレフィックスを形成します。後の引数はコンテキストまたは呼び出し引数から推測できます。

## 汎用関数

関数名の後に関数型パラメータを宣言します。

```VeroScript
T identity<T>(T value)
{
	return value;
}

string text = identity<string>("VeroScript");
int number = identity(42);
```

引数で十分な情報が提供される場合は、型引数を省略できます。特定のタイプを選択する必要がある場合は、それらを明示的に指定します。

`template<T>` も、汎用宣言の読みやすいスペルです。これは、型または関数名の後に `<T>` が続く場合と同じ一般的な規則に従います。

```VeroScript
template<T>
T identity_again(T value)
{
	return value;
}
```

## 型制約

`where` を使用して、型パラメータからのインターフェイス コントラクトを要求します。

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

T read_once<T>(T value) where T : IReadable
{
	return value;
}
```

型パラメータには複数のインターフェイス制約を含めることができます。

```VeroScript
T describe<T>(T value) where T : IReadable, IName
{
	return value;
}
```

制約はコンパイル時にチェックされます。ジェネリック関数の呼び出しまたはジェネリック型のインスタンス化に使用される具象型は、宣言されたすべての制約を満たす必要があります。

インターフェースの型エイリアスやエイリアスの連鎖も制約に使用できます。`using Readable = IReadable;` を宣言した後、`where T : Readable` と記述できます。`IBox<int>` などの具象ジェネリックインターフェースへのエイリアスにも対応しています。エイリアスは宣言元のモジュールで解決され、具象型は解決後のインターフェースを実装している必要があります。

## 汎用メンバーとインターフェース

ジェネリック型はインターフェイスまたはジェネリック基本型を継承でき、インターフェイス自体が型パラメーターを持つことができます。

```VeroScript
interface IBox<T>
{
	virtual T get() const = 0;
};

class Box<T> : public IBox<T>
{
public:
	T value;

	T get() const override
	{
		return value;
	}
};
```

ジェネリック宣言のメンバーは、アクセス制御、仮想メソッド、オーバーロード、静的メンバーの通常のルールに従います。汎用静的メンバーには、具体的な型ごとに独立したストレージがあります。 [静的メンバー](static-members.md) を参照してください。

## 型パラメータの境界

型パラメーターは、コンパイラーがその宣言から確立できる操作でのみ使用できます。インターフェイス メソッドを呼び出す前、または制約付きメンバーにアクセスする前に、`where` 制約を追加します。宣言されていない暗黙的なメンバーに依存しないでください。

`T::member` のような修飾静的アクセスは具体型への置換後に検査し、その型に対応するアクセス可能な静的フィールドまたはメソッドが必要です。アクセス、オーバーロード、const／参照の規則も適用します。自己型、別名、フィールド既定値の例は[静的メンバー](static-members.md)を参照してください。

入れ子の型、ローカル宣言、アクセス規則、各階層のジェネリック引数については、[型のスコープ](scope.md)を参照してください。

## メソッドテンプレートと値パラメーター

`T`、`typename T`、`class T` は同じ型パラメーター宣言です。インスタンスメソッド、static メソッド、本体を持つ非 virtual インターフェース補助メソッドは独自のパラメーターを持てます。内側のパラメーターによる外側の名前の隠蔽は禁止です。メソッドテンプレートに virtual、override、final は指定できません。

値パラメーターは bool、固定幅整数、スクリプトまたは登録済みホストの列挙型に対応します。`template<typename T, T Value>` のような依存型も使えます。例: `template<typename T, int Count = 4> struct Buffer { T values[Count]; };`。値は変更できないコンパイル時定数です。純粋なスカラー式、検査付きキャスト、列挙子、評価可能な const 初期化式を使用し、条件を満たす [constexpr 関数](../language-reference/constexpr.md)を評価できます。その他の関数やホストコールバックは実行しません。オーバーフロー、ゼロ除算、範囲外、列挙型の不一致はエラーです。固定配列の長さは具体化後に検査します。

既定値を指定した後の独自パラメーターにはすべて既定値が必要です。既定値は宣言の文脈で名前を解決し、外側または前のパラメーターを参照できます。呼び出しは明示的な先頭引数、引数の型構造による推論、既定値の順に処理します。実行時の数値から値パラメーターは推論しません。

`box.echo<string>("x")` はテンプレートを明示的に選び、`box.echo("x")` は推論を許可します。`f<>()` はテンプレートだけを対象にします。変換品質を先に比較し、同等なら通常候補を優先します。その後は証明可能な構造の特殊性のみで比較し、決定できなければ曖昧性エラーです。型は独自パラメーター総数の完全一致を優先し、その後に既定値を補完します。

`identity<int>`、`box.echo<string>`、static メソッド参照から callable を作れます。明示的な fn 型も推論に使えます。動的な文字列名は開いたメソッドテンプレートを自動で具体化しません。クラス外定義は owner とメソッドごとの template ヘッダーで位置対応を指定し、既定値を繰り返せません。

`method_templates.vs`、`value_template_parameters.vs`、`template_methods_main.vs` を参照してください。Core VM/Mixed/JIT または Core AOT を使用します。独立したソース出力は未対応機能を出力前に診断します。コンパイル済みライブラリは新しい引数のために開いた宣言を保持し、元ソースを必要としません。リフレクションの照会だけでは実行本体を生成しません。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/generics.md) · [fr-FR](../../fr-FR/language-basics/generics.md) · [ja-JP](generics.md) · [ko-KR](../../ko-KR/language-basics/generics.md) · [zh-CN](../../zh-CN/language-basics/generics.md) · [zh-TW](../../zh-TW/language-basics/generics.md)
