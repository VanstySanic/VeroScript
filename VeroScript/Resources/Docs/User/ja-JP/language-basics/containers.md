# コンテナ

VeroScript コンテナ タイプは、汎用引数を使用して要素またはキー/値のタイプを記述します。固定サイズのネイティブ配列を除き、コンテナは通常、共有ハンドルによって表されます。コンテナを別の変数に割り当てると、両方の変数が同じコンテナ オブジェクトを参照するようになります。

## 共通ルール

コンテナ型は型引数に山括弧を使用し、`len` はその要素またはエントリ数を返します。

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();

print(len(values));
print(len(counts));
```

挿入された要素は、宣言された要素タイプと一致する必要があります。構造体などの値の型は挿入時にコピーされます。クラスとその他のハンドル タイプは、ハンドル セマンティクスを保持します。

ほとんどのコンテナは範囲の反復をサポートしています。値ループ変数は要素のコピーを受け取ります。 `T&` ループ変数はコンテナ内の要素を変更できます。マップは、値、キー、およびキー/値エントリの反復もサポートします。

## `map` および `ordered_map`

`map<K, V>` は、キーを値に関連付け、インデックスを使用して値にアクセスします。

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;
scores["bob"] = 88;

print(scores["alice"]);
print(scores.has("bob"));
```

一般的な操作は次のとおりです。

|操作 |説明 |
| --- | --- |
| `m[key]` |キーの値の読み取りまたは書き込み |
| `m.has(key)` |キーが存在するかどうかをテストする |
| `m.remove(key)` |キーを削除し、削除されたかどうかを返します。
| `m.keys()` |キーを `array<K>` | として返します。
| `m.values()` |値を `array<V>` | として返します。
| `m.entries()` |キー/値エントリを配列として返します。
| `m.clear()` |マップをクリアする |
| `len(m)` |キーの数を返す |

`map` は順序付けされていません。 `keys()` によって返される順序は指定されていません。 `ordered_map` は、個別のキーの最初の挿入順序を保持します。既存のキーを更新しても、キーは移動されません。

## `multimap` および `ordered_multimap`

`multimap<K, V>` では、1 つのキーに複数の値を含めることができます。値を追加するには、`add` を使用します。インデックス作成により、キーに関連付けられた `array<V>` が返されます。

```VeroScript
multimap<string, int> grouped = multimap<string, int>();
grouped.add("alpha", 1).add("alpha", 2);

print(grouped["alpha"][0]);
print(len(grouped["alpha"]));
```

`remove(key)` は、そのキーの下に保存されているすべての値を削除します。 `ordered_multimap` はさらに、個別のキーの最初の挿入順序を保持します。

## `set` および `multiset`

`set<T>` には固有の要素が格納されます。既存の要素を追加しても、2 番目の要素は作成されません。

```VeroScript
set<string> names = set<string>();
names.add("alice");
names.add("alice");
print(names.has("alice"));
```

`remove(value)` は要素を削除し、`clear()` はセットを空にします。 `multiset<T>` は値の繰り返しを許可し、`count(value)` を提供します。

```VeroScript
multiset<int> numbers = {2, 1, 2, 3, 2};
print(numbers.count(2));
numbers.remove(2);
```

`multiset` の場合、`remove(value)` はオカレンスを 1 つ削除します。 `ordered_multiset` は、個別の値の最初の挿入順序を保持します。

## キュー、スタック、優先キュー、およびデキュー

|タイプ |セマンティクス |共通操作 |
| --- | --- | --- |
| `queue<T>` |先入れ先出し | `push`、`pop`、`front` |
| `stack<T>` |後入れ先出し | `push`、`pop`、`top` |
| `priority_queue<T>` |優先的に削除します。現在の実装では、最初に最大値が返されます。 `push`、`pop`、`top` |
| `deque<T>` |両端での挿入と削除、および整数インデックス | `push_front`、`push_back`、`pop_front`、`pop_back` |

たとえば、`queue<T>` は後ろに追加して前から削除しますが、`stack<T>` は先頭に追加して削除します。現在の要素数を取得するには、`len` を使用します。

## マップの反復

マップは、値またはキー/値のエントリによって反復できます。

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (int value : scores)
{
	print(value);
}

for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}
```

`scores.keys()` を反復処理し、`scores[key]` を使用して各値にアクセスすることもできます。順序なしマップの順序をプログラム ロジックとして使用してはなりません。

### スナップショットとエントリ ビュー

`m.entries()` は、値としての `array<pair<K, V>>` スナップショットを返します。`m` 自体を反復して取得した `pair<K, V>&` は、マップ エントリへの書き込み可能なビューです。`m.entries()` の要素への参照をマップ内部への参照として扱わないでください。直接のエントリ反復または `m[key]` を使ってマップを変更します。

## `channel`

`channel<T>` は、同時タスク間の通信に使用される共有ハンドルです。 `concurrent.vs` 標準ライブラリは、`channel_send`、`channel_recv`、および `channel_close` 関数を提供します。

```VeroScript
import "concurrent.vs";

channel<int> values = channel<int>();
std::concurrent::channel_send(values, 42);
int result = std::concurrent::channel_recv(values);
std::concurrent::channel_close(values);
```

チャネルは通常、`spawn`、`join`、およびタスク関数で使用されます。ブロッキング動作、転送可能なタイプ、およびタスクの有効期間は、一般的なコンテナー参照ではなく、同時実行ランタイム参照に属します。

コンテナーの長さと `multiset.count(value)` などの要素数は非負の `int64` を返します。deque のインデックスには `int64` を使い、map のキーは宣言したキー型を保持します。長さの変換と範囲は[組み込み関数](../standard-library/builtins.md)、チャネルの個数と容量は[並行処理](../standard-library/concurrent.md)を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/language-basics/containers.md) · [fr-FR](../../fr-FR/language-basics/containers.md) · [ja-JP](containers.md) · [ko-KR](../../ko-KR/language-basics/containers.md) · [zh-CN](../../zh-CN/language-basics/containers.md) · [zh-TW](../../zh-TW/language-basics/containers.md)
