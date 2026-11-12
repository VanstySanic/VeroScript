# 関数

関数は、パラメーター リストと戻り値の型を持つ名前付き呼び出し可能宣言です。関数は、モジュール スコープで定義することも、構造体、クラス、インターフェイスのメンバーとして定義することもできます。

## 関数の宣言

関数宣言には、戻り値の型、名前、パラメーター リスト、および本体が含まれます。

```VeroScript
int add(int left, int right)
{
	return left + right;
}
```

`void` は、関数が値を返さないことを示します。

```VeroScript
void print_message(string message)
{
	print(message);
}
```

## `main` エントリ ポイント

アプリケーション エントリ ポイントは、`int` または `void` を返し、最大 1 つの `string[]` パラメータを取ることができます。 `int` 戻り値はプロセスの終了コードになります。 `void` は `0` として扱われます。エントリ パラメータは、参照パラメータ、可変長パラメータ、またはデフォルトのパラメータにすることはできません。

```VeroScript
int main(string[] args)
{
	if (len(args) == 0)
	{
		return 2;
	}
	print(args[0]);
	return 0;
}
```

`main` を提供できるのはエントリ モジュールのみです。ライブラリ モジュールはアプリケーション エントリ ポイントを定義できません。

`int main` が正常に戻る場合、または明示的に戻ることなく閉じ括弧に到達した場合、結果コードは戻り値または `0` になります。 `exit(n)` は `main` 戻り値よりも優先され、`exit(0)` にはスクリプトが明示的に終了を要求したことが記録されます。エンジン API の場合、ゼロ以外の `ExitCode` は、それ自体がエンジンの障害を意味するわけではありません。ホストは、`bSuccess`、`ExitCode`、および `bExitRequested` を一緒に検査する必要があります。

関数本体のパラメーターとローカル変数は、通常の関数とブロックスコープの規則に従います。

## パラメータと戻り値

引数は宣言順にパラメータと照合されます。それらの型は言語の変換規則を満たしている必要があります。

```VeroScript
double average(double total, int count)
{
	return total / count;
}
```

`return` 式の型は、関数の戻り値の型と互換性がある必要があります。 `void` 関数は、式なしで `return` を使用して、実行を早期に終了できます。

## デフォルトの引数

パラメータはデフォルト値を提供できます。呼び出し元がその引数を省略した場合、宣言されたデフォルトの式が使用されます。

```VeroScript
int clamp(int value, int minimum = 0, int maximum = 100)
{
	if (value < minimum)
	{
		return minimum;
	}
	if (value > maximum)
	{
		return maximum;
	}
	return value;
}

int main()
{
	print(clamp(120));
	print(clamp(-1, -10, 10));
}
```

デフォルトのパラメータは、パラメータ リストの末尾のシーケンスを形成する必要があります。必須パラメータをオプションのパラメータの後に配置しないでください。

## 関数のオーバーロード

同じ名前の複数の関数は、パラメーター リストで区別されている場合、1 つのスコープ内で宣言できます。

```VeroScript
int abs_value(int value)
{
	return value < 0 ? -value : value;
}

double abs_value(double value)
{
	return value < 0.0 ? -value : value;
}
```

関数が呼び出されるとき、コンパイラは名前、引数の数、引数の型、および利用可能な変換を使用してオーバーロードを選択します。複数の候補が同等に一致する場合、オーバーロードの曖昧さによりコンパイルは失敗します。

## 参照パラメータ

参照パラメーターを使用すると、関数は呼び出し元が所有するオブジェクトを変更できます。公開されている例では `T&` が好まれます。 `ref T` は、同等のキーワード スタイルのスペルです。

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
}
```

引数は、呼び出しサイトで一致する `&` または `ref` マーカーを使用します。 VeroScript には汎用のスクリプト ポインター タイプがなく、参照ではポインター演算がサポートされていません。

## 可変個引数パラメーター

関数は `T... name` を使用して、1 つの要素型で一連の可変引数を宣言できます。

```VeroScript
void print_all(string... values)
{
	for (string value : values)
	{
		print(value);
	}
}
```

関数内では、可変引数パラメーターはその要素型の配列として動作します。可変個引数パラメーターは最後になければなりません。

## 関数の値とラムダ

関数は値として渡すことができ、ラムダは匿名関数を作成できます。関数の型では、そのパラメーター リストと戻り値の型が説明されます。

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

int main()
{
	function(int) -> int increment = function(int value) -> int => value + 1;
	print(apply(increment, 41));
}
```

`fn` は、`function` の短縮表記です。関数値とラムダのキャプチャ ルール、およびジェネリック関数とメンバー関数については、専用のリファレンスで説明されています。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/functions.md) · [fr-FR](../../fr-FR/language-basics/functions.md) · [ja-JP](functions.md) · [ko-KR](../../ko-KR/language-basics/functions.md) · [zh-CN](../../zh-CN/language-basics/functions.md) · [zh-TW](../../zh-TW/language-basics/functions.md)
