# 配列

VeroScript には、動的配列 `array<T>` と固定サイズのネイティブ配列 `T[N]` の 2 つの配列形式があります。どちらもインデックス作成と範囲反復をサポートしていますが、実行時のセマンティクスが異なる異なるタイプです。

## 動的配列

`array<T>` は、要素タイプ `T` によってパラメータ化された動的コンテナです。

```VeroScript
array<int> values = array<int>();
values.push(10);
values.push(20);

print(values[0]);
print(len(values));
```

ブレースの初期化もサポートされています。

```VeroScript
array<int> values = array<int>{1, 2, 3};
```

一般的な操作は次のとおりです。

|操作 |説明 |
| --- | --- |
| `values[index]` |インデックスにある要素の読み取りまたは変更 |
| `values.push(value)` |要素を追加して配列を返します |
| `values.remove(value)` |最初の等しい要素を削除し、それが削除されたかどうかを返します。
| `values.remove_at(index)` |インデックスにある要素を削除します |
| `values.clear()` |配列をクリアして配列を返します |
| `values.reserve(count)` |容量を予約し、配列を返します。
| `values.capacity()` |現在の容量を返す |
| `len(values)` |要素の数を返す |

`array<T>` に挿入されるすべての要素は、`T` と一致する必要があります。構造体とネイティブ配列は、挿入時に値のセマンティクスに従ってコピーされます。クラスとその他のハンドル タイプは、ハンドル セマンティクスを保持します。

動的配列は、共有コンテナ ハンドルを介して保存されます。 1 つの `array<T>` 変数を別の変数に代入すると、両方の変数が同じ動的配列を参照します。独立したコピーが必要な場合は、独立した配列を構築し、その要素をコピーします。

## 固定サイズのネイティブ配列

ネイティブ配列では、その長さが要素タイプの後に置かれます。

```VeroScript
int numbers[3] = {10, 20, 30};
```

長さはタイプの一部です。 `int[3]`、`int[4]`、および `array<int>` は異なるタイプです。ネイティブ配列は構造体フィールドにすることも、多次元にすることもできます。

```VeroScript
struct GridRow
{
	int cells[3] = {1, 2, 3};
};

int matrix[2][3] = {
	{1, 2, 3},
	{4, 5, 6}
};
```

多次元ネイティブ配列は、入れ子になったネイティブ配列で構成されます。 `len(matrix)` は最初の次元の長さを返し、`len(matrix[0])` は 2 番目の次元の長さを返します。

ネイティブ配列型の次元数は最大 64 です。各次元の要素数は最大 65,536、全次元を平坦化した要素数の積は最大 1,048,576 です。いずれかの上限を超えるソースはコンパイル時に拒否されます。

## インデックス作成

配列インデックスは `0` から始まります。

```VeroScript
array<string> names = array<string>{"Ada", "Linus"};
string first = names[0];
names[1] = "Grace";
```

インデックス式の結果には要素タイプがあります。インデックスは、現在の配列の有効範囲内の整数である必要があります。範囲外にアクセスすると、実行時エラーが発生します。

## 範囲の反復

配列を反復処理するには、コロンを付けて `for` を使用します。通常のループ変数は要素値を受け取ります。

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

参照ループ変数を使用して元の要素を変更します。

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

構造体の配列の場合、値渡しループ変数を変更しても配列に書き戻されません。元の要素を変更するには、`T&` を使用します。動的配列とネイティブ配列の両方が参照の反復をサポートします。

## 配列形式の選択

長さが固定されており、データ レイアウトの一部である場合は、`T[N]` を使用します。実行時に長さが変更される場合、または `push` や `remove_at` などの操作が必要な場合は、`array<T>` を使用します。可変コンテナーを関数間で共有する必要がある場合は `array<T>` を使用し、固定レイアウト値が必要な場合はネイティブ配列を使用します。

`len(values)` と `values.capacity()` は非負の `int64` を返します。線形インデックス、`remove_at(index)`、`reserve(count)` は `int64` を受け取ります。インデックスは `0 <= index < len(values)`、予約容量は非負でホスト上で表現可能でなければなりません。確保上限は引き続き適用されます。範囲検査付き変換は[組み込み関数](../standard-library/builtins.md)を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/arrays.md) · [fr-FR](../../fr-FR/language-basics/arrays.md) · [ja-JP](arrays.md) · [ko-KR](../../ko-KR/language-basics/arrays.md) · [zh-CN](../../zh-CN/language-basics/arrays.md) · [zh-TW](../../zh-TW/language-basics/arrays.md)
