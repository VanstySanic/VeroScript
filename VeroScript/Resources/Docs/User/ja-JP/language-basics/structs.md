# 構造体

`struct` は、値セマンティクスを使用してユーザー タイプを定義します。構造体には、フィールド、メソッド、コンストラクター、およびデストラクターを含めることができます。 1 つの基本型から継承したり、インターフェイスを実装したりすることもできます。構造体オブジェクト自体は値によってコピーされます。各フィールドは依然として独自のタイプのセマンティクスに従います。

## 宣言とメンバー

構造体の宣言は、名前とメンバーのリストで構成されます。構造体のメンバーはデフォルトで `public` です。

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	void translate(int dx, int dy)
	{
		this.x += dx;
		this.y += dy;
	}
};
```

メンバー関数内では、`this` は現在の構造体オブジェクトを参照します。裸の名前でアクセスできるメンバーには、`this.member` としてアクセスすることもできます。

構造体は、`public`、`protected`、および `private` のアクセス制御をサポートします。メンバーはデフォルトではパブリックですが、アクセスを制限する必要がある場合は、明示的なアクセス指定子を使用します。

```VeroScript
struct Account
{
public:
	int id = 0;

private:
	int balance = 0;
};
```

## 構築と初期化

フィールドにはデフォルトの初期化式を提供できます。構造体はコンストラクターを宣言することもできます。構築では引数リストからオーバーロードを選択します。

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point()
	{
	}

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}
};

Point origin = Point();
Point point = Point(3, 4);
Point literal = Point{7, 8};
```

コンストラクター初期化リストでは、コンストラクター本体より前に直接のインスタンスフィールドを初期化できます。このリストのフィールド項目は、フィールドの宣言順に初期化されます。宣言初期化子を持つフィールドをこのリストでも初期化することはできません。`const` フィールドの規則については、[定数](constants.md)を参照してください。

中括弧付き形式は型付き初期化であり、同じコンストラクターの一致ルールに従います。明示的なコンストラクターが宣言されていない場合でも、フィールドのデフォルトはデフォルトの構築に参加します。

## 値のセマンティクス

構造体を割り当てると、その値がコピーされます。コピーの直接フィールドを変更しても、元の構造体は変更されません。

```VeroScript
struct Counter
{
	int value = 0;
};

Counter first = Counter();
Counter second = first;
second.value = 10;

print(first.value);  // 0
print(second.value); // 10
```

値のセマンティクスは、構造体オブジェクト自体を記述します。フィールドが `array<T>`、`map<K, V>`、または別の共有ハンドル タイプの場合、そのフィールドは、構造体のコピー時に独自の共有ハンドル動作を保持します。独立したコンテナが必要な場合は、コンテナの内容を明示的にコピーします。

## メソッドとリファレンス

struct メソッドは、現在のオブジェクトを読み取りまたは変更できます。関数が呼び出し元の構造体を直接変更する必要がある場合は、参照パラメーターを使用します。

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}

	void translate(int dx, int dy)
	{
		x += dx;
		y += dy;
	}
};

void reset(ref Point point)
{
	point.x = 0;
	point.y = 0;
}

int main()
{
	Point point = Point{3, 4};
	point.translate(1, 2);
	reset(ref point);
	print("{}, {}", point.x, point.y);
	return 0;
}
```

通常のパラメーターと値による範囲の反復では、構造体の値のコピーが作成されます。参照パラメータと参照範囲の反復は、元のオブジェクトに書き込みます。詳細なルールについては、[参考資料](references.md)を参照してください。

## `mutable` 実装フィールド

非 static のインスタンス フィールドは、`const` クエリから更新できる実装状態を表す場合に `mutable` と指定できます。最も一般的な用途はキャッシュです。

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

## 継承とインターフェース

構造体は 1 つの構造体ベースを継承し、1 つ以上のインターフェイスを実装できます。

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Counter : IReadable
{
	int value = 0;

	int read() const
	{
		return value;
	}
};
```

構造体をインターフェイス型に変換すると、インターフェイスに必要な値表現が作成されます。そのインターフェイス値を変更しても、元の構造体には書き戻されません。インターフェイスの継承については、[インターフェイス](interfaces.md) ページで説明されています。一般的な制約については、[ジェネリック](generics.md) ページで説明されています。

## デフォルト特殊メンバー

```VeroScript
struct Record
{
	int value = 0;
	Record() = default;
	Record(const Record& other) = default;
	Record& operator=(const Record& other) = default;
	bool operator==(const Record& other) const = default;
	bool operator!=(const Record& other) const = default;
};
```

構造体では、メンバー単位のコピー構築、代入、等価比較を明示的に生成できます。コピー構築はフィールド初期化子を再実行しません。代入は既存のサブオブジェクトを更新し、元のオブジェクトへの参照を返します。const または参照フィールドがあるとデフォルト代入が使用できない場合があります。コピーと比較の可否は別々に判定されます。

クラスは共有ハンドルのコピーを維持したままデフォルト等価比較を生成できます。削除された操作やアクセスできない操作から、生のコピーへは切り替わりません。明示的コピーコンストラクターは直接構築で使用でき、暗黙の初期化では使用できません。新しい構築結果に追加のコピーは不要です。スクリプトのムーブ操作は追加しません。

これらの宣言はコアの実行機能を使用します。混合／JIT 方針での仮想マシンへのフォールバックとコア AOT も対象です。独立した C++、C#、Python ソースのエクスポートでは未対応のデフォルトメンバーを拒否します。例：`default_special_members.vs`。

入れ子の型、ローカル宣言、アクセス規則、各階層のジェネリック引数については、[型のスコープ](scope.md)を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/structs.md) · [fr-FR](../../fr-FR/language-basics/structs.md) · [ja-JP](structs.md) · [ko-KR](../../ko-KR/language-basics/structs.md) · [zh-CN](../../zh-CN/language-basics/structs.md) · [zh-TW](../../zh-TW/language-basics/structs.md)
