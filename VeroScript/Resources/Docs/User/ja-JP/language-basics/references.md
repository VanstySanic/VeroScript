# 参照と参照セマンティクス

参照は、既存の保管場所またはハンドル スロットの別名を付けます。独立したオブジェクトは作成されず、別名を付けたオブジェクトを所有しません。 VeroScript 参照は、パラメータ、ローカル エイリアス、範囲反復、要素アクセス、参照の戻り、およびラムダ参照のキャプチャに使用されます。これらは汎用スクリプト ポインターではなく、ポインター演算をサポートしません。

## 参照パラメータ

推奨されるスペルでは、末尾に `&` が使用されます。

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
	print(value);
	return 0;
}
```

`ref T` と `const ref T` は同等のキーワード スタイルのスペルです。

```VeroScript
void reset(ref int value)
{
	value = 0;
}

int main()
{
	int value = 1;
	reset(ref value);
	return 0;
}
```

宣言と呼び出しマーカーは一致している必要があります。`T&` は `&expression` を使用し、`ref T` は `ref expression` を使用します。参照引数は、変数、配列要素、マップ要素、オブジェクト フィールドなどのストレージを識別する必要があります。

## 読み取り専用の参照

読み取り専用参照には、`const T&` または `const ref T` を使用します。ターゲットを読み取ることはできますが、エイリアスを介してターゲットを変更することはできません。

```VeroScript
int read_value(const int& value)
{
	return value;
}

int main()
{
	const int value = 42;
	print(read_value(&value));
	return 0;
}
```

可変参照は、const オブジェクトまたは const 参照の戻り値にバインドできません。関数がオブジェクトを検査する必要があるだけの場合は、読み取り専用参照を推奨します。

## ローカル参照の別名

ローカル変数は、既存のオブジェクトのエイリアスとして宣言できます。

```VeroScript
int main()
{
	int value = 1;
	int& alias = value;
	const int& view = value;

	alias = 2;
	print(view);
	return 0;
}
```

`alias` と `view` はどちらも `value` のストレージを参照します。参照変数は独立したオブジェクトにはならず、そのターゲットもコピーされません。

## 範囲反復における参照

通常の範囲変数は要素値を受け取ります。構造体などの値型の場合、ループ変数を変更しても元のコンテナーに書き戻されません。

```VeroScript
for (Point point : points)
{
	point.x = 0;
}
```

`T&` を使用して、ループ変数を元の要素のエイリアスにします。

```VeroScript
for (Point& point : points)
{
	point.x = 0;
}
```

配列、固定サイズのネイティブ配列、両端キュー、およびマップ値は、参照範囲の反復をサポートします。読み取り専用コレクションには `const T&` を使用します。

マップの反復にキーと値の両方が必要な場合は、`pair<K, V>&` を使用します。値のみが必要な場合は、`V&` を繰り返します。

```VeroScript
for (pair<string, int>& entry : counts)
{
	entry.second += 1;
}
```

## 要素の参照

配列またはマップ要素は参照引数として直接渡すことができます。

```VeroScript
void bump(int& value)
{
	value += 1;
}

bump(&values[0]);
bump(&counts["total"]);
```

要素参照は要素をコピーしないため、要素参照を通じて行われた変更は呼び出し後も元のコンテナに残ります。パス内のすべてのオブジェクトが有効である限り、ネストされたフィールドと要素パスでも参照を生成できます。

## 参照が返される

関数は変更可能な参照または読み取り専用の参照を返すことができます。参照を返す式は割り当て可能な左辺値のままで、インクリメントしたり、別の参照にバインドしたりできます。

```VeroScript
int Counter = 1;
array<int> Values = array<int>();

int& counter_ref()
{
	return Counter;
}

int& first_value()
{
	return Values[0];
}

const int& read_counter()
{
	return Counter;
}

int main()
{
	Values.push(40);
	counter_ref() += 4;
	first_value() += 2;
	print(read_counter());
	return 0;
}
```

参照戻りは、有効なままのストレージを指している必要があります。グローバル、コンテナ要素、オブジェクト フィールド、または既存の参照を返す場合があります。スタックストレージが関数で終わる通常の関数ローカル変数を返すことはできません。可変参照は const 参照の戻り値にバインドできません。

クラス ハンドル変数は、参照を返すことによって再バインドすることもできます。参照は、新しいオブジェクトをコピーするのではなく、ハンドル スロットを指します。これはクラス自体の共有 ID の動作とは異なるため、API で明示的に文書化する必要があります。

## 参照と関数の型

参照方向は関数タイプの一部です。関数値とラムダは `int&` または `const int&` を使用できますが、呼び出しには引き続き `&` 引数が必要です。

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	function(int&) -> void mutate = add_one;
	int value = 3;
	mutate(&value);
	print(value);
	return 0;
}
```

## 制限と有効期間

- 書き込み可能な参照引数にデフォルト値は指定できません。読み取り専用の参照引数は、呼び出し元が保持する一時値を使って対応するリテラルのデフォルト値を受け入れます。参照引数は可変長にできません。
- 参照はオブジェクトを所有せず、ポインター演算をサポートせず、通常のオブジェクトの寿命を自動的に延長しません。
- `ref`、`const ref`、`T&`、および `const T&` は、関数の型、オーバーロード、およびバインディング チェックに影響します。
- 通常の参照は、ターゲット ストレージが有効な間は使用する必要があります。
- Lambda 参照キャプチャはマネージド共有参照セルを使用するため、クロージャはそれを作成した関数よりも安全に存続できます。 [ラムダと関数の値](lambdas.md) を参照してください。

実行可能な参照サンプルは、[サンプル インデックス](../examples.md) にリストされています。

コンテナー要素への参照は、元のコンテナーと要素を借用します。コンテナー変数を再代入しても参照先は変わりません。同じ要素への代入、末尾への追加、別のキーの挿入では参照先を維持します。要素が失効した場合や、元の位置が別の要素になった場合は無効参照の診断になります。管理された参照戻り値と Lambda キャプチャの寿命規則は維持されます。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/references.md) · [fr-FR](../../fr-FR/language-basics/references.md) · [ja-JP](references.md) · [ko-KR](../../ko-KR/language-basics/references.md) · [zh-CN](../../zh-CN/language-basics/references.md) · [zh-TW](../../zh-TW/language-basics/references.md)
