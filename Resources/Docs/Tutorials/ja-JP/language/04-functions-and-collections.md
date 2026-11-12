# 04: 関数とコレクション

`main` が増大し始めると、コメントをさらに追加するよりも、明確な責任を持つコードを抽出する方が通常は便利です。このチュートリアルでは、sum 関数を作成し、可変数の値を `array<int>` に保存します。

## ステップ 1: sum 関数を作成する

```VeroScript
int sum(array<int> values)
{
	int result = 0;
	for (int value : values)
	{
		result = result + value;
	}
	return result;
}

int main()
{
	array<int> values = array<int>();
	values.push(10);
	values.push(20);
	values.push(30);

	print("sum={}", sum(values));
	return 0;
}
```

関数宣言では、戻り値の型、名前、パラメーターを指定します。 `sum` は `array<int>` を受信し、`int` を返します。

## ステップ 2: コレクションを増やす

`array<int>()` は空の動的配列を作成し、`push` は最後に要素を追加します。中括弧の初期化を使用することもできます。

```VeroScript
int main()
{
	array<int> values = array<int>{10, 20, 30};

	values.push(40);
	for (int& value : values)
	{
		value = value + 1;
	}

	print("size={}", len(values));
	print("first={}", values[0]);
	return 0;
}
```

通常の範囲ループ `for (int value : values)` は要素のコピーを受け取ります。 `&` を追加すると、要素にエイリアスが作成されるため、変更はコンテナーに書き戻されます。

## ステップ 3: デフォルトのパラメータを使用する

```VeroScript
void print_values(array<int> values, string label = "values")
{
	print(label);
	for (int value : values)
	{
		print(value);
	}
}

int main()
{
	array<int> values = array<int>{1, 2, 3};
	print_values(values);
	print_values(values, "numbers");
	return 0;
}
```

デフォルトのパラメータを使用すると、呼び出し元は共通の設定を省略できます。デフォルトはパラメータのタイプと互換性がある必要があります。

## コンテナの選択

順序付けされた拡張可能なデータの場合は、`array<T>` から始めます。キー検索には `map<K, V>` を、一意性には `set<T>` を考慮してください。アクセス パターンで必要になるまでは、より複雑なコンテナを追加しないでください。

## 演習

1. 空の配列に対して `0.0` を返す `average` 関数を作成します。
2. エイリアス範囲ループを使用して、すべての負の値を `0` に変更する関数を作成します。
3. スコアを `map<string, int>` に保存し、1 人の名前のスコアを印刷します。

[`examples/dynamic_array.vs`](../../../../../VeroScript/Resources/Scripts/examples/dynamic_array.vs) および [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs) を参照してください。 [データ型](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/data-types.md) ページには、使用可能なコンテナーがまとめられています。

次: [構造体、クラス、インターフェイス](05-structs-classes-and-interfaces.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/language/04-functions-and-collections.md) · [fr-FR](../../fr-FR/language/04-functions-and-collections.md) · [ja-JP](04-functions-and-collections.md) · [ko-KR](../../ko-KR/language/04-functions-and-collections.md) · [zh-CN](../../zh-CN/language/04-functions-and-collections.md) · [zh-TW](../../zh-TW/language/04-functions-and-collections.md)
