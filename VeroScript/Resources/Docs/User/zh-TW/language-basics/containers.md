# 容器

VeroScript 的容器類型使用泛型參數來描述元素類型或鍵值類型。除固定長度原生陣列外，容器通常以共享控制代碼表示；將容器賦給另一個變數時，兩個變數可以指向同一個容器物件。

## 容器的共同規則

容器透過尖括號寫出類型參數，並可使用 `len` 取得元素數量：

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();

print(len(values));
print(len(counts));
```

容器元素必須符合宣告的元素型別。結構體等值型別在插入時會按值複製，類別和其他控制代碼型別保留其控制代碼語意。

大多數容器支援範圍遍歷。使用值類型循環變數時，循環變數是元素的值副本；使用 `T&` 時，可以透過循環變數修改容器中的元素。映射容器也支援值遍歷、鍵遍歷和鍵值對遍歷。

## `map` 和 `ordered_map`

`map<K, V>` 是鍵值映射，使用下標存取值：

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;
scores["bob"] = 88;

print(scores["alice"]);
print(scores.has("bob"));
```

常用操作包括：

| 操作 | 說明 |
| --- | --- |
| `m[key]` | 讀取或寫入鍵對應的值 |
| `m.has(key)` | 判斷鍵是否存在 |
| `m.remove(key)` | 刪除鍵，傳回是否刪除成功 |
| `m.keys()` | 回傳鍵組成的 `array<K>` |
| `m.values()` | 傳回值組成的 `array<V>` |
| `m.entries()` | 傳回鍵值對組成的陣列 |
| `m.clear()` | 清空對映 |
| `len(m)` | 傳回目前鍵的數量 |

`map` 是無序映射，`keys()` 的遍歷順序未定義。 `ordered_map` 按鍵第一次插入的順序儲存鍵；更新已有鍵的值不會改變該鍵的位置。

## `multimap` 和 `ordered_multimap`

`multimap<K, V>` 允許一個鍵對應多個值。使用 `add` 追加值，下標存取傳回該鍵對應的 `array<V>`：

```VeroScript
multimap<string, int> grouped = multimap<string, int>();
grouped.add("alpha", 1).add("alpha", 2);

print(grouped["alpha"][0]);
print(len(grouped["alpha"]));
```

`remove(key)` 會刪除該鍵下的全部值。 `ordered_multimap` 額外保持不同鍵的首次插入順序。

## `set` 和 `multiset`

`set<T>` 儲存唯一元素。重複加入同一個元素不會產生第二個元素：

```VeroScript
set<string> names = set<string>();
names.add("alice");
names.add("alice");
print(names.has("alice"));
```

`remove(value)` 刪除元素，`clear()` 清空集合。 `multiset<T>` 允許同一個值出現多次，並且可以使用 `count(value)` 查詢出現次數：

```VeroScript
multiset<int> numbers = {2, 1, 2, 3, 2};
print(numbers.count(2));
numbers.remove(2);
```

`remove(value)` 對 `multiset` 刪除一個出現項目。 `ordered_multiset` 以不同值第一次出現的順序儲存其值序列。

## 佇列、堆疊、優先佇列和雙端佇列

| 類型 | 語意 | 常用操作 |
| --- | --- | --- |
| `queue<T>` | 先進先出 | `push`、`pop`、`front` |
| `stack<T>` | 後進先出 | `push`、`pop`、`top` |
| `priority_queue<T>` | 依優先權取出；目前實作優先取最大值 | `push`、`pop`、`top` |
| `deque<T>` | 兩端插入與刪除，也支援整數下標 | `push_front`、`push_back`、`pop_front`、`pop_back` |

例如，`queue<T>` 從尾部加入元素，從頭部取出；`stack<T>` 從堆疊頂部加入和取出。所有容器的目前數量都可以使用 `len` 取得。

## 映射遍歷

映射可以按值或按鍵值對遍歷：

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

也可以透過 `scores.keys()` 遍歷鍵，再使用 `scores[key]` 存取值。無序映射的鍵順序不能作為程式邏輯的依據。

### 快照和項目檢視

`m.entries()` 返回值型別的 `array<pair<K, V>>` 快照陣列。直接遍歷 `m` 取得的 `pair<K, V>&` 是映射項目的可寫入檢視。不要把 `m.entries()` 元素的引用當作映射內部項目的引用；應透過直接遍歷映射項目或使用 `m[key]` 修改映射。

## `channel`

`channel<T>` 是用於並發任務之間通訊的共用容器控制代碼。標準函式庫 `concurrent.vs` 提供 `channel_send`、`channel_recv` 和 `channel_close` 函式：

```VeroScript
import "concurrent.vs";

channel<int> values = channel<int>();
std::concurrent::channel_send(values, 42);
int result = std::concurrent::channel_recv(values);
std::concurrent::channel_close(values);
```

通道通常與 `spawn`、`join` 和任務函式一起使用。阻塞行為、可傳輸型別和任務生命週期屬於並發執行階段文件，而不是普通容器操作的一部分。

容器長度及元素數量（包含 `multiset.count(value)`）傳回非負 `int64`。Deque 索引使用 `int64`，map 鍵仍保留其宣告的鍵型別。長度轉換及範圍規則請見[內建函式](../standard-library/builtins.md)，通道數量和容量請見[並行模組](../standard-library/concurrent.md)。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/language-basics/containers.md) · [fr-FR](../../fr-FR/language-basics/containers.md) · [ja-JP](../../ja-JP/language-basics/containers.md) · [ko-KR](../../ko-KR/language-basics/containers.md) · [zh-CN](../../zh-CN/language-basics/containers.md) · [zh-TW](containers.md)
