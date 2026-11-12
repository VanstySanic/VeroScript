# 06: 参照、参照の戻り、ラムダ キャプチャ

このチュートリアルでは、いくつかの小さな実行可能なプログラムを使用して、既存の値へのエイリアスと外部状態を共有するクロージャの違いを説明します。参照は、既存のオブジェクトまたは要素をその場で変更する場合に役立ちます。ラムダ キャプチャは、クロージャが外側のバインディングを格納する方法を決定します。

## ステップ 1: ローカル参照エイリアスを作成する

```VeroScript
int main()
{
	int score = 40;
	int& alias = score;
	alias += 2;

	print("score={}, alias={}", score, alias);
	return 0;
}
```

`alias` は 2 番目の整数ではありません。これは `score` の参照エイリアスであるため、`alias` を介して書き込むと `score` が変更されます。初期化後に参照を別の整数に再バインドすることはできません。独立した値が必要な場合は、通常の変数を使用してください。

## ステップ 2: 関数への参照を渡す

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	int value = 40;
	add_one(&value);
	print(value);
	return 0;
}
```

`int&` は、書き込み可能な参照パラメータを宣言します。この呼び出しでは、`&value` を使用してエイリアシング操作を明示します。読み取り専用アクセスの場合は、`const int&` を使用します。関数がその参照を通じてオブジェクトを変更するのを防ぎながら、コピーを回避します。

## ステップ 3: 書き込み可能な参照を返す

参照は関数の戻り値にすることもできますが、返されたオブジェクトは呼び出しが終了した後も存在している必要があります。

```VeroScript
int counter = 1;

int& counter_ref()
{
	return counter;
}

int main()
{
	counter_ref() += 4;
	int& alias = counter_ref();
	alias += 1;
	print(counter);
	return 0;
}
```

これにより、グローバル `counter` が返されるため、呼び出し元は結果を通じて引き続き変更を行うことができます。通常のローカル変数への参照を返さないでください。関数が戻ったとき、そのローカル変数はスコープを離れています。コンテナ要素とオブジェクト メンバーは、所有者が有効なまま参照によって返すこともできます。

## ステップ 4: 参照の反復を通じてコンテナーを変更する

```VeroScript
int main()
{
	array<int> values = array<int>();
	values.push(1);
	values.push(2);

	for (int& value : values)
	{
		value += 10;
	}

	print(values[0]);
	print(values[1]);
	return 0;
}
```

`for (int& value : values)` は、`value` を各配列要素のエイリアスにするため、ループ内の代入は配列に書き戻されます。 `&` がない場合、ループ変数は値を受け取り、その変更はライトバックされません。インデックスによる要素の受け渡しには、同じ明示的な形式 (`add_one(&values[0])` など) を使用します。

## ステップ 5: ラムダに外部状態をキャプチャさせます

```VeroScript
function() -> int make_counter()
{
	int running = 40;
	return [&]() -> int
	{
		running++;
		return running;
	};
}

int main()
{
	function() -> int next = make_counter();
	print(next());
	print(next());
	return 0;
}
```

`[&]` キャプチャは、参照によってローカルを囲んで使用されます。 VeroScript は、キャプチャされたローカル バインディングを安全な共有ストレージにプロモートするため、`make_counter` が戻った後も、`next` は引き続き `running` に安全にアクセスして変更できます。

共有ミューテーションが必要な変数が 1 つだけの場合は、`[&running]` と書き込みます。混合ポリシーの場合は、`[=, &running]` を使用して、他の変数が値によってキャプチャされ、`running` のみが参照によってキャプチャされるようにします。

## ステップ 6: メンバー関数のレシーバーをキャプチャする

レシーバーのキャプチャは、その型自身のセマンティクスに従います。

```VeroScript
class Counter
{
public:
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

struct Snapshot
{
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

int main()
{
	Counter counter = Counter();
	counter.value = 1;
	function() -> int live = counter.getter();
	counter.value = 2;

	Snapshot snapshot = Snapshot();
	snapshot.value = 3;
	function() -> int copied = snapshot.getter();
	snapshot.value = 4;

	print(live());
	print(copied());
	return 0;
}
```

クラスのクロージャはオブジェクトを保持するため `2` を出力します。構造体の
クロージャは値のスナップショットを保持するため `3` を出力します。キャプチャ
なしの `[]` ラムダは `this` にアクセスできません。

## どちらかを選択する

- 独立したコピーが必要な場合は、通常の値または値キャプチャを使用します。
- 関数が呼び出し元が所有する値を変更する必要がある場合は、書き込み可能な参照パラメーターを使用します。
- ループでコンテナ要素を適切に更新する必要がある場合は、参照範囲変数を使用します。
- 返されたクロージャが変更可能な外部状態を共有し続ける必要がある場合は、参照キャプチャを使用します。
- 大きなオブジェクトをコピーせずに読み取り専用アクセスを行う場合は、`const T&` を検討してください。ただし、参照されるオブジェクトが十分な期間存続するようにしてください。

## 演習

1. `swap_values(int& left, int& right)` を書き込み、`swap_values(&a, &b)` で呼び出します。
2. `array<int>` に対する参照範囲ループを使用して、すべての要素に `2` を乗算します。
3. 値をキャプチャした `step` を `make_counter` に追加し、`[=, &running]` ポリシーを実装します。
4. 通常のローカルへの参照を返し、`--check` 診断を読み取り、グローバルまたはまだ有効なメンバーを返すように変更してみます。
5. クロージャを作成した後で `Counter` のフィールドを変更し、`Snapshot` の結果と比較します。

完全なルールについては、[参照と参照セマンティクス](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/references.md) および [ラムダと関数値](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/lambdas.md) を参照してください。実行可能な例は、[`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs)、[`examples/reference_returns.vs`](../../../../../VeroScript/Resources/Scripts/examples/reference_returns.vs)、および [`examples/lambda.vs`](../../../../../VeroScript/Resources/Scripts/examples/lambda.vs) です。

次: [モジュール、インポート、およびプリプロセッサ](07-modules-and-preprocessor.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/language/06-references-and-captures.md) · [fr-FR](../../fr-FR/language/06-references-and-captures.md) · [ja-JP](06-references-and-captures.md) · [ko-KR](../../ko-KR/language/06-references-and-captures.md) · [zh-CN](../../zh-CN/language/06-references-and-captures.md) · [zh-TW](../../zh-TW/language/06-references-and-captures.md)
