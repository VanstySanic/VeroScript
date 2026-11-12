# 17: Optional 値、weak 参照、共有構造体ハンドル

このチュートリアルでは、VeroScript のコンパイラ提供ユーティリティ型を使って小さな状態共有例を作ります。`optional<T>`、`weak<T>`、`handle<T>`、`pair<T, U>` を使う場面と、それぞれの値および所有の規則を学びます。

`shared<T>` は `handle<T>` の推奨表記です。両者は同じ型であり、コンテナー、ジェネリック関数、`weak<T>` の中でも混在できます。`std::shared<T>` と `std::handle<T>` も同じ型を表します。明示的な `shared<T>()` は非 null のラッパーを作り、`nullptr` の代入はハンドルを空にします。構造体は生成時にラッパーへコピーされ、その後のハンドルのコピーは同じオブジェクトを共有します。`weak<shared<T>>` はラッパーを監視し、`lock()` は同じオブジェクトへの強いハンドル、または失効後の `nullptr` を返します。`.value` にアクセスする前に結果を確認してください。`shared` は予約された組み込み型エイリアスであり、構文キーワードではありません。既存のソフトなジェネリック規則に従い、同名・同アリティのユーザー定義 `handle<T>` がある場合、両方の表記はそのユーザー型を指します。

## 目標

このチュートリアルの終了時には、次のことができるようになります。

- センチネル値を使わず、存在しない可能性のある結果を表す；
- オブジェクトを存続させずにクラスを観測する；
- クラス ハンドルを通じて構造体ペイロードを共有する；
- マップ エントリのスナップショットと書き込み可能なビューを区別する。

完全なリファレンスは [値ユーティリティ型](../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/value-utility-types.md)です。各例は実行前に `--check` で確認してください。

## ステップ 1: optional の結果を表す

空の結果には `optional<T>()` を、値を含む結果には `optional<T>(value)` を使います。`get()` を呼ぶ前に `has_value()` を確認します。

```VeroScript
optional<int> find_score(bool available)
{
	if (available)
	{
		return optional<int>(95);
	}
	return optional<int>();
}

int main()
{
	optional<int> missing = find_score(false);
	print(missing.value_or(0));

	optional<int> found = find_score(true);
	if (found.has_value())
	{
		print(found.get());
	}

	return 0;
}
```

デフォルト値で十分な場合は `value_or` が便利です。明示的に状態を確認した後は `get()` を使えます。空の optional に対する呼び出しは `assert` により失敗します。`try_cast<T>` も、失敗する可能性のある変換で同じ結果パターンを使います。

## ステップ 2: 所有せずにクラスを観測する

クラス変数は強いハンドルです。`weak<T>` はオブジェクトを観測しますが、その存続期間を延長しません。

```VeroScript
class Session
{
public:
	int id = 0;
};

int main()
{
	Session owner = Session();
	owner.id = 42;

	weak<Session> watcher = weak<Session>(owner);
	if (!watcher.expired())
	{
		Session locked = watcher.lock();
		print(locked.id);
	}

	owner = nullptr;
	print(watcher.expired());
	return 0;
}
```

`lock()` はオブジェクトが存続していれば強いハンドルを、そうでなければ null ハンドルを返します。メンバーにアクセスする前に、必ず `expired()` または `lock()` の結果を確認してください。weak 値のコピーは別の観測を作り、片方のリセットは他方をリセットしません。

## ステップ 3: 構造体ペイロードを共有する

構造体の代入は通常、値をコピーします。プログラムの複数箇所で同じ構造体ペイロードを変更する必要がある場合は、`handle<T>` に格納します。

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void move_right(handle<Point> point)
{
	point.value.x += 1;
}

int main()
{
	shared<Point> first = shared<Point>(Point());
	handle<Point> second = first;

	first.value.x = 10;
	move_right(second);
	print(first.value.x);
	print(second.value.x);

	return 0;
}
```

`handle<T>` を別の値へ代入するとクラス ハンドルがコピーされるため、両方が同じ `value` を参照します。`handle<T>(source)` は新しい共有オブジェクトへペイロードをコピーします。`nullptr` の代入はハンドルをクリアします。完全な例は [examples/handle_struct.vs](../../../../../VeroScript/Resources/Scripts/examples/handle_struct.vs) です。

## ステップ 4: `pair` のビューを選ぶ

マップ API は `pair<K, V>` を使います。`entries()` の結果は値スナップショットの配列なので、スナップショットを変更してもマップは変わりません。マップを `pair<K, V>&` で直接反復すると、書き込み可能なエントリ ビューになります。

```VeroScript
int main()
{
	ordered_map<string, int> scores = ordered_map<string, int>();
	scores["alice"] = 95;
	scores["bob"] = 88;

	for (pair<string, int> snapshot : scores.entries())
	{
		print("{} -> {}", snapshot.first, snapshot.second);
	}

	for (pair<string, int>& entry : scores)
	{
		entry.second += 1;
	}

	for (auto& [key, value] : scores)
	{
		print("{} -> {}", key, value);
	}

	return 0;
}
```

独立したエントリ値のコレクションが必要な場合は `m.entries()` を使います。エントリへ書き戻す場合はマップを直接反復します。完全な例は [examples/pair_map.vs](../../../../../VeroScript/Resources/Scripts/examples/pair_map.vs) です。

## 演習

1. `find_score` を変更し、整数の代わりに `optional<string>` のメッセージを返します。
2. 2 つ目の `weak<Session>` 観測を追加し、一方をリセットしても他方がリセットされないことを確認します。
3. `handle<Point>` の例に `y` 方向の移動関数を追加し、2 つ目のハンドルから呼び出します。
4. `map<string, Point>` を追加し、`pair<string, Point>&` を通じて入れ子のフィールドを更新します。

## 覚えておくこと

- `optional<T>` は空または値ありを表す値型です。
- `weak<T>` はクラスの存続期間を延長せずに観測します。
- `handle<T>` は、ペイロードを持つ public フィールド `value` を含むクラス オブジェクトを共有します。
- `entries()` の `pair<K, V>` は値スナップショットであり、マップを直接反復した pair 参照は書き戻します。

次へ: [標準名前空間 std を使用する](14-standard-namespace.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/advanced/17-value-utility-types.md) · [fr-FR](../../fr-FR/advanced/17-value-utility-types.md) · [ja-JP](17-value-utility-types.md) · [ko-KR](../../ko-KR/advanced/17-value-utility-types.md) · [zh-CN](../../zh-CN/advanced/17-value-utility-types.md) · [zh-TW](../../zh-TW/advanced/17-value-utility-types.md)
