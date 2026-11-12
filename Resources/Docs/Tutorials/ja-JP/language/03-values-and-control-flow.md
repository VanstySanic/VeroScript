# 03: 値、条件、ループ

このチュートリアルでは、変数、静的型、分岐、繰り返しを小さなスコア計算ツールに組み合わせます。目標は、問題を状態、決定、繰り返されるアクションに分割する方法を学ぶことです。

## ステップ 1: 状態を準備する

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	print("total={}", total);
	return 0;
}
```

`total` と `count` は両方とも `int` です。 `i` は `for` ループ内にのみ存在します。ローカル スコープは、一時的な状態が無関係なコードに漏洩するのを防ぎます。

## ステップ 2: 決定を追加する

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	if (total >= 15)
	{
		print("pass: {}", total);
	}
	else
	{
		print("retry: {}", total);
	}
	return 0;
}
```

`if` の条件はブール値の結果を生成する必要があります。比較すると `bool` が生成されます。条件として扱われる整数に依存するのではなく、明示的な比較を使用します。

## ステップ 3: データを配列に移動する

```VeroScript
int main()
{
	int scores[4] = {72, 88, 91, 65};
	int total = 0;

	for (int i = 0; i < len(scores); i = i + 1)
	{
		total = total + scores[i];
	}

	double average = to_double(total) / len(scores);
	print("average={}", average);
	return 0;
}
```

`scores` は、固定サイズのネイティブ配列です。そのインデックスは `0` から始まり、`len(scores)` はその長さを返します。実行時に要素の数を変更する必要がある場合は、`array<int>` を使用します。

## ステップ 4: 小数リテラルを使う

`.` の直後に 10 進数字が続く場合は、浮動小数点リテラルの整数部を省略できます。サフィックスは保存される型を引き続き決定します。`.5f` は `float` で、`.5` と `.5e2` は `double` 値です。

```VeroScript
int main()
{
	float ratio = .5f;
	double half = .5;
	double fifty = .5e2;

	print("ratio={}, half={}, fifty={}", ratio, half, fifty);
	return 0;
}
```

`0.5f` と `.5f` は同じ値と型です。ドットの後には数字を置いてください。単独の `.` は引き続きメンバー アクセスに使用されます。

## ステップ 5: スクリプト例外を処理する

スクリプトが説明して回復できる失敗には `throw` を使用します。例外値は関数呼び出しを
越えて伝播し、一致する `catch` で処理できます。操作が成功した場合でも例外で終了した
場合でも実行する必要がある後処理には `finally` を使用します。

```VeroScript
void validate_score(int score)
{
	if (score < 0)
	{
		throw "score must not be negative";
	}
}

int main()
{
	try
	{
		validate_score(-1);
	}
	catch (const string& message)
	{
		print("handled: {}", message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

このプログラムは処理したメッセージを出力し、その後 `cleanup` を出力します。例外の型が
重要な場合は型付きハンドラーを使用し、`catch (...)` は最後のフォールバックに限定します。
ハンドラー内の式なし `throw;` は元の値を再送出します。

完全な例外の例については、[`examples/exceptions.vs`](../../../../../VeroScript/Resources/Scripts/examples/exceptions.vs) を参照してください。

## ステップ 6: `assert` で不変条件を確認する

`assert(condition[, message])` はランタイムチェックです。条件は `bool`、省略可能な
メッセージは `string` でなければなりません。

```VeroScript
int main()
{
	int total = 15;
	assert(total >= 0, "total must be non-negative");
	print("ready");
	return 0;
}
```

条件が真なら出力せずに続行します。条件が偽ならランタイムエラーを報告して現在の実行を
停止します。これはスクリプト例外ではないため、`catch (...)` では処理できません。

## 使用した制御フローの考え方

- `if` は、条件に基づいてパスを選択します。
- `for` は、明示的な初期化、条件、および増分を使用して作業を繰り返します。
- `try`/`catch` は `throw` が送出したスクリプト値を処理し、`finally` は後処理を行います。
- `assert` はブール不変条件を確認し、失敗時にランタイムエラーを報告します。
- `return` は結果を返して関数を終了します。次のチュートリアルではこれを多用します。

## 演習

1. 最高スコアと最低スコアを計算します。
2. `60` 以上のスコアのみをカウントします。
3. 固定配列を `array<int>` に置き換え、範囲ベースの反復を使用します。
4. `0.5` のような小数リテラルを `.5` に置き換え、`float` 値が必要な箇所では `.5f` を使用します。
5. 負のスコアを拒否する関数に型付き `catch` と `finally` ブロックを追加します。
6. スコア計算の不変条件にメッセージ付きの `assert` を追加します。

完全な配列の例については、[`examples/arrays.vs`](../../../../../VeroScript/Resources/Scripts/examples/arrays.vs) を参照してください。参照ページは、[変数](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/variables.md)、[制御フロー](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/control-flow.md)、[配列](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/arrays.md)です。

次へ: [関数とコレクション](04-functions-and-collections.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/language/03-values-and-control-flow.md) · [fr-FR](../../fr-FR/language/03-values-and-control-flow.md) · [ja-JP](03-values-and-control-flow.md) · [ko-KR](../../ko-KR/language/03-values-and-control-flow.md) · [zh-CN](../../zh-CN/language/03-values-and-control-flow.md) · [zh-TW](../../zh-TW/language/03-values-and-control-flow.md)
