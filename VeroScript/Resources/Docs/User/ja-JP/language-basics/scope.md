# スコープ

スコープは、ソース コード内で名前が有効な場所を決定します。一般的な VeroScript スコープには、モジュール、名前空間、関数、ブロック、メンバー スコープが含まれます。

## ブロックスコープ

関数または制御フロー ステートメント内の中括弧はブロックを作成します。ローカル変数は、宣言からそれを囲んでいるブロックの終わりまで有効です。

```VeroScript
int main()
{
	int outer = 1;

	if (outer > 0)
	{
		int inner = 2;
		print(outer);
		print(inner);
	}

	// inner is out of scope here.
	print(outer);
}
```

内側のブロックは、外側のブロックですでに宣言されている名前にアクセスできます。外側のブロックは、内側のブロックで宣言されたローカル変数にアクセスできません。

## 関数のスコープ

関数パラメータとローカル変数は関数のスコープに属します。異なる関数が同じローカル名を使用できます。各宣言は別々です。

```VeroScript
void first()
{
	int value = 1;
	print(value);
}

void second()
{
	int value = 2;
	print(value);
}
```

## モジュールのスコープ

最上位で宣言された変数、関数、型は現在のモジュールに属します。エクスポートされない宣言は通常、モジュール内でのみ使用できます。エクスポートされた宣言は、インポート ルールに従って他のモジュールで使用できます。

## 名前空間のスコープ

名前空間は、宣言を別の名前範囲に配置します。

```VeroScript
namespace math
{
	int add(int left, int right)
	{
		return left + right;
	}
}

int main()
{
	print(math::add(1, 2));
}
```

名前空間メンバーには、`.` または `::` を使用してアクセスできます。ネームスペースはモジュールを置き換えるものではありません。モジュールはファイルとパッケージのインポートを整理し、ネームスペースは宣言名を整理します。

### グローバル スコープ修飾

先頭の `::` を使うと、現在の名前空間、ローカル エイリアス、`using namespace` ではなくグローバル スコープから検索します。ルートの宣言を意図する場合に `::Name` を使用してください。

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

また、ルート レベルの型や名前空間を修飾でき、`using namespace ::Name` でも使用できます。

## メンバーのスコープ

構造体、クラス、またはインターフェイス内のフィールドとメソッドは、型のメンバー スコープに属します。アクセスは、`public`、`protected`、および `private` によって制限されます。

```VeroScript
class Counter
{
private:
	int value = 0;

public:
	int get()
	{
		return value;
	}
};
```

メンバー関数は、`this` を使用して現在のオブジェクトにアクセスできます。静的メンバーは特定のオブジェクト インスタンスに依存せず、異なるアクセス ルールを持ちます。

## 名前の非表示

内部スコープは外部スコープと同じ名前を宣言できます。内側の宣言は外側の宣言を隠します。名前の検索が読みにくくなるため、不必要なシャドウイングは避けてください。


## 入れ子型とローカル型

型内に class、struct、interface、enum、型エイリアスを宣言できます。`Outer::Inner` で参照し、ジェネリック引数は各区間に指定します（`Outer<int>::Inner<string>`）。公開された入れ子型はエクスポートされた外側の型を通して使用できます。private／protected の経路と公開エイリアスには通常のアクセス検査が適用されます。

実行ブロック内のローカル型は宣言位置からブロック末尾まで有効です。内側のブロックでは同名を宣言できます。可視の型、エイリアス、列挙定数、型パラメーターを使用できますが、外側の変数や `this` はキャプチャしません。インスタンスメンバーを使用する場合はオブジェクトを明示的に渡します。static 領域は具体型と実行インスタンスごとに保持されます。

```VeroScript
class Outer<T>
{
public:
	struct Inner<U>
	{
		T key;
		U value;
	};
};

void main()
{
	Outer<int>::Inner<string> item = Outer<int>::Inner<string>();
	item.key = 7;
	item.value = "saved";
	struct Result
	{
		int count = 1;
	};
	Result result = Result();
	print("{} {} {}", item.key, item.value, result.count);
}
```

ローカルではない入れ子メソッドは `T Outer<T>::Inner<U>::read() { ... }` と定義でき、宣言をインポートした別ファイルにも記述できます。ローカル型のメソッドは型内またはプロセッサーの生成本体に定義します。不完全型の独立した前方宣言、型本体の再オープン、ローカルメソッドのファイル間定義は対象外です。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/scope.md) · [fr-FR](../../fr-FR/language-basics/scope.md) · [ja-JP](scope.md) · [ko-KR](../../ko-KR/language-basics/scope.md) · [zh-CN](../../zh-CN/language-basics/scope.md) · [zh-TW](../../zh-TW/language-basics/scope.md)
