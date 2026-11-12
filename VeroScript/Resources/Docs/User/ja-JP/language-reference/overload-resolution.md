# オーバーロードの解決

複数の関数、メソッド、または演算子が 1 つのスコープ内で名前を共有する場合、コンパイラーは呼び出しコンテキストから呼び出し可能な候補を 1 つ選択します。

## 候補関数

オーバーロード解決では、まず名前と呼び出し形式で候補を収集し、次に以下をチェックします。

- 引数の数が一致するかどうか。
- 引数の型がパラメータ変換規則を満たしているかどうか。
- `ref`、`const ref`、または `&` マーカーが宣言と一致するかどうか。
- ジェネリック型引数を推論でき、すべての `where` 制約が保持されるかどうか。
- デフォルトの引数が省略された末尾の位置を埋めることができるかどうか。

通常、戻り値の型だけでは、同じ名前のオーバーロードを区別するのに十分ではありません。

```VeroScript
int parse(int value)
{
	return value;
}

string parse(string value)
{
	return value;
}
```

引数が指定されると、コンパイラはその型と利用可能な変換を使用して、最も一致する候補を選択します。一致する候補がない場合、または複数の候補が同等に一致する場合、コンパイルは診断を報告します。

候補の照合では、互換性のある数値変換や、単一の値を受け取る非 `explicit` 変換コンストラクターなどの暗黙的な変換も考慮されます。`explicit` コンストラクターは通常の代入や引数変換の候補にならないため、直接構築または `cast<T>` で要求します。コンパイラーは複数のユーザー定義変換を連鎖させません。

## 参照パラメータ

参照マーカーはパラメーター マッチングの一部であり、呼び出しサイトで記述する必要があります。

```VeroScript
void update(int& value)
{
	value = value + 1;
}

void update(const int& value)
{
	print(value);
}

int number = 1;
update(&number);
```

参照マーカーを省略したり、読み取り専用の参照を書き込み可能として使用したりしないでください。明示的なマーカーにより、参照オーバーロードが明確になります。 `ref T` および `ref expression` は、互換性のあるスペルとして引き続きサポートされます。

## 汎用オーバーロード

ジェネリック候補は、その型引数が決定でき、その制約が保持され、そのパラメーターが引数を受け入れることができる場合にのみ実行可能です。

```VeroScript
T identity<T>(T value)
{
	return value;
}

int number = identity(42);
string text = identity<string>("text");
```

推論があいまいな場合は、明示的な型引数を指定するか、パラメーターを調整します。

## 演算子のオーバーロード

演算子のオーバーロードは、同じ候補一致ルールに従います。メンバー演算子には暗黙的な現在のオブジェクトが含まれますが、非メンバー演算子は明示的なオペランドを使用します。そのパラメータと戻り値の型は、演算子の実装規則を満たしている必要があります。宣言については、[演算子](../language-basics/operators.md) を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-reference/overload-resolution.md) · [fr-FR](../../fr-FR/language-reference/overload-resolution.md) · [ja-JP](overload-resolution.md) · [ko-KR](../../ko-KR/language-reference/overload-resolution.md) · [zh-CN](../../zh-CN/language-reference/overload-resolution.md) · [zh-TW](../../zh-TW/language-reference/overload-resolution.md)
