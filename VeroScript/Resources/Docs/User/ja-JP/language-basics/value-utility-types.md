# 値ユーティリティ型

VeroScript には、値の不在を表す型、クラス オブジェクトを所有しない観測、共有構造体ペイロード、およびマップ エントリを扱う、コンパイラが提供する小さなジェネリック型群があります。これらは import なしで使用できます。[`std` 名前空間](standard-namespace.md)にある `std::` 表記は、同じ型への互換エイリアスです。

## `optional<T>`

`optional<T>` は、値を持たないか、型 `T` の値を 1 つ持つ値型です。状態は明示的で、デフォルト構築した optional は空です。

### 構築とメソッド

| 形式 | 意味 |
| --- | --- |
| `optional<T>()` | 空の optional を構築します。 |
| `optional<T>(value)` | `value` を含む engaged optional を構築します。 |
| `has_value() const` | 値が存在するか返します。 |
| `get() const` | 保存された値を返します。空の状態で呼ぶと `assert` により失敗します。 |
| `value_or(fallback) const` | 値があればそれを、空なら `fallback` を返します。 |
| `reset()` | optional を空にします。 |

```VeroScript
optional<int> score = optional<int>();
print(score.has_value());
print(score.value_or(0));

score = optional<int>(95);
if (score.has_value())
{
	print(score.get());
}

score.reset();
```

optional は `T` の値セマンティクスを引き継ぎます。構造体ペイロードでは、engaged optional のコピーが構造体をコピーします。クラス ペイロードではクラス ハンドルをコピーするため、共有 ID が維持されます。`reset()` は、格納されたクラス ハンドルがあればそれも解放します。

失敗する可能性のある変換では、`try_cast<T>` が `optional<T>` を返します。`get()` を呼ぶ前に `has_value()` を確認してください。詳しくは [型変換](../language-reference/type-conversions.md) を参照してください。

## `weak<T>`

`weak<T>` は、クラス オブジェクトを所有しない観測です。`T` はスクリプト クラス、インターフェイス、またはサポートされるホスト クラス型である必要があります。プリミティブ型と構造体は有効な対象ではありません。weak 値は対象の存続期間を延長しません。

### weak 参照のメソッド

| 形式 | 意味 |
| --- | --- |
| `weak<T>()` | 空の weak 参照を構築します。 |
| `weak<T>(target)` | `target` にあるクラス ハンドルを観測します。 |
| `lock() const` | 対象が存続していれば強い `T` ハンドルを、そうでなければ null ハンドルを返します。 |
| `expired() const` | 対象をロックできなくなったか返します。 |
| `reset()` | この weak 参照をクリアします。 |

```VeroScript
class Session
{
public:
	int id = 0;
};

Session owner = Session();
weak<Session> observer = weak<Session>(owner);

Session locked = observer.lock();
if (locked != nullptr)
{
	print(locked.id);
}
```

weak 値をコピーすると、同じ対象を観測する別の weak 参照が作られます。片方をリセットしても、他方はリセットされません。強いクラス ハンドルがすべてなくなると `expired()` は true になり、`lock()` は null ハンドルを返します。メンバーにアクセスする前に必ず結果を確認してください。

## `shared<T>` / `handle<T>`

`handle<T>` は、コンパイラが提供する 1 型引数のジェネリック クラスで、`value` という public フィールドを持ちます。構造体の値を共有クラス ID の背後に置く場合に便利です。デフォルト コンストラクターはハンドルを作成し、値型の `value` をデフォルト構築します。`T` がクラスの場合、ペイロードはクラス ハンドルのセマンティクスに従い、デフォルトでは null ハンドルです。`handle<T>(value)` はペイロードを新しい共有オブジェクトへコピーします。

`shared<T>` は `handle<T>` の推奨表記です。両者は同じ型であり、コンテナー、ジェネリック関数、`weak<T>` の中でも混在できます。`std::shared<T>` と `std::handle<T>` も同じ型を表します。明示的な `shared<T>()` は非 null のラッパーを作り、`nullptr` の代入はハンドルを空にします。構造体は生成時にラッパーへコピーされ、その後のハンドルのコピーは同じオブジェクトを共有します。`weak<shared<T>>` はラッパーを監視し、`lock()` は同じオブジェクトへの強いハンドル、または失効後の `nullptr` を返します。`.value` にアクセスする前に結果を確認してください。`shared` は予約された組み込み型エイリアスであり、構文キーワードではありません。既存のソフトなジェネリック規則に従い、同名・同アリティのユーザー定義 `handle<T>` がある場合、両方の表記はそのユーザー型を指します。

値を 1 つ渡す `auto h = shared(value)` では `T` を推論します。引数なしの構築には、明示的な代入先の型または型引数が必要です。

ソース出力にはバックエンドの制限があります。C++ 出力は共有ラッパーと弱参照をサポートします。C# 出力は共有ラッパーをサポートしますが、構造体または未確定のジェネリック型のペイロードには `shared<Point>(Point())` のように値を明示的に渡す必要があり、`weak<T>` は現在サポートされていません。独立した Python ソース出力は共有ラッパーと弱参照をサポートしませんが、Python ランタイムバインディングはコアを通じて両方をサポートします。完全なライフタイムの例は VM、Mixed、JIT バックエンドで実行できます。

### 構築と共有

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void bump_x(handle<Point> point)
{
	point.value.x += 1;
}

shared<Point> first = shared<Point>(Point());
handle<Point> second = first;
first.value.x = 10;
bump_x(second);
print(first.value.x);
```

`first` を `second` に代入するとクラス ハンドルがコピーされるため、両方の変数が同じ `value` を観測して変更します。`nullptr` を代入するとハンドルがクリアされます。`T` が構造体の場合、`handle<T>(source)` は新しいペイロードのコピーを作るため、後から `source` を変更してもそのコピーは変わりません。

`handle<T>` は共有ラッパーであり、一般的なクラスの代替ではありません。`h.value` の構造体をインターフェイスへ変換すると値のコピーがボックス化されます。インターフェイスから共有可変 ID を公開する必要がある場合は、そのインターフェイスを実装するクラスを使用してください。

完全な実行可能例は [examples/handle_struct.vs](../../../../Scripts/examples/handle_struct.vs) です。

## `pair<T, U>`

`pair<T, U>` は、次の 2 つの public フィールドを持つ、コンパイラ提供のジェネリック構造体です。

| フィールド | 型 |
| --- | --- |
| `first` | `T` |
| `second` | `U` |

マップ API はキーと値のエントリを `pair` で表します。`m.entries()` は、値としての `pair<K, V>` スナップショットの配列を返します。

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (pair<string, int> entry : scores.entries())
{
	print("{} -> {}", entry.first, entry.second);
}
```

マップ自体を反復すると、書き込み可能なエントリ ビューを取得できます。`pair<K, V>&` はマップへ書き戻し、`pair<K, V>` はコピーです。

```VeroScript
for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}

for (auto& [key, value] : scores)
{
	value += 1;
}
```

`entries()` から返された `pair` を元のマップ エントリへの参照として扱わないでください。範囲反復の完全な規則は [参照](references.md) と [コンテナ](containers.md) を参照してください。マップの実行可能例は [examples/pair_map.vs](../../../../Scripts/examples/pair_map.vs) です。

## 名前と互換性

`optional`、`readonly`、`weak`、およびリフレクション情報型は、厳密に予約された組み込み型名です。スクリプトで再宣言することはできません。

`pair<T, U>` と `handle<T>` はソフトなジェネリック組み込みです。コンパイラは、同じ名前かつ同じジェネリック引数数のユーザー宣言がない場合に限り、2 型引数の `pair` と 1 型引数の `handle` を提供します。非ジェネリックの `pair` や `handle`、または別の引数数の宣言は、対応するソフトなジェネリック型と共存できます。

標準名前空間のエイリアスは `std::optional`、`std::weak`、`std::shared`, `std::handle`、`std::pair` です。これらのエイリアスは別の型を作りません。

## 関連リファレンス

- [データ型](data-types.md): 組み込み型、コンテナ、読み取り専用ラッパー。
- [ジェネリック](generics.md): ユーザー定義ジェネリック型とソフト組み込み。
- [型変換](../language-reference/type-conversions.md): `cast`、`try_cast`、`const_cast`、変換結果。
- [コンテナ](containers.md): マップ操作と範囲反復。

スクリプトオブジェクトの破棄開始時に弱参照の寿命も終了します。破棄中および破棄後の lock() は null、expired() は true を返します。デストラクターから this を外部に保存しても復活せず、その後のフィールドやメソッドへのアクセスは拒否されます。ホストオブジェクトには従来のホスト寿命契約が適用されます。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/value-utility-types.md) · [fr-FR](../../fr-FR/language-basics/value-utility-types.md) · [ja-JP](value-utility-types.md) · [ko-KR](../../ko-KR/language-basics/value-utility-types.md) · [zh-CN](../../zh-CN/language-basics/value-utility-types.md) · [zh-TW](../../zh-TW/language-basics/value-utility-types.md)
