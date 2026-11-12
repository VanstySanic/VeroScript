# 容器

VeroScript 的容器类型使用泛型参数描述元素类型或键值类型。除固定长度原生数组外，容器通常以共享句柄表示；将容器赋给另一个变量时，两个变量可以指向同一个容器对象。

## 容器的共同规则

容器通过尖括号写出类型参数，并可以使用 `len` 获取元素数量：

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();

print(len(values));
print(len(counts));
```

容器元素必须符合声明的元素类型。结构体等值类型在插入时按值复制，类和其他句柄类型保留其句柄语义。

大多数容器支持范围遍历。使用值类型循环变量时，循环变量是元素的值副本；使用 `T&` 时，可以通过循环变量修改容器中的元素。映射容器还支持值遍历、键遍历和键值对遍历。

## `map` 和 `ordered_map`

`map<K, V>` 是键值映射，使用下标访问值：

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;
scores["bob"] = 88;

print(scores["alice"]);
print(scores.has("bob"));
```

常用操作包括：

| 操作 | 说明 |
| --- | --- |
| `m[key]` | 读取或写入键对应的值 |
| `m.has(key)` | 判断键是否存在 |
| `m.remove(key)` | 删除键，返回是否删除成功 |
| `m.keys()` | 返回键组成的 `array<K>` |
| `m.values()` | 返回值组成的 `array<V>` |
| `m.entries()` | 返回键值对组成的数组 |
| `m.clear()` | 清空映射 |
| `len(m)` | 返回当前键的数量 |

`map` 是无序映射，`keys()` 的遍历顺序未定义。`ordered_map` 按键第一次插入的顺序保存键；更新已有键的值不会改变该键的位置。

## `multimap` 和 `ordered_multimap`

`multimap<K, V>` 允许一个键对应多个值。使用 `add` 追加值，下标访问返回该键对应的 `array<V>`：

```VeroScript
multimap<string, int> grouped = multimap<string, int>();
grouped.add("alpha", 1).add("alpha", 2);

print(grouped["alpha"][0]);
print(len(grouped["alpha"]));
```

`remove(key)` 会删除该键下的全部值。`ordered_multimap` 额外保持不同键的首次插入顺序。

## `set` 和 `multiset`

`set<T>` 保存唯一元素。重复添加同一个元素不会产生第二个元素：

```VeroScript
set<string> names = set<string>();
names.add("alice");
names.add("alice");
print(names.has("alice"));
```

`remove(value)` 删除元素，`clear()` 清空集合。`multiset<T>` 允许同一个值出现多次，并可以使用 `count(value)` 查询出现次数：

```VeroScript
multiset<int> numbers = {2, 1, 2, 3, 2};
print(numbers.count(2));
numbers.remove(2);
```

`remove(value)` 对 `multiset` 删除一个出现项。`ordered_multiset` 按不同值第一次出现的顺序保存其值序列。

## 队列、栈、优先队列和双端队列

| 类型 | 语义 | 常用操作 |
| --- | --- | --- |
| `queue<T>` | 先进先出 | `push`、`pop`、`front` |
| `stack<T>` | 后进先出 | `push`、`pop`、`top` |
| `priority_queue<T>` | 按优先级取出；当前实现优先取最大值 | `push`、`pop`、`top` |
| `deque<T>` | 两端插入和删除，也支持整数下标 | `push_front`、`push_back`、`pop_front`、`pop_back` |

例如，`queue<T>` 从尾部加入元素，从头部取出；`stack<T>` 从栈顶加入和取出。所有容器的当前数量都可以使用 `len` 获取。

## 映射遍历

映射可以按值或按键值对遍历：

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

也可以通过 `scores.keys()` 遍历键，再使用 `scores[key]` 访问值。无序映射的键顺序不能作为程序逻辑的依据。

### 快照和条目视图

`m.entries()` 返回值类型的 `array<pair<K, V>>` 快照数组。直接遍历 `m` 得到的 `pair<K, V>&` 是映射条目的可写视图。不要把 `m.entries()` 元素的引用当成映射内部条目的引用；应通过直接遍历映射条目或使用 `m[key]` 修改映射。

## `channel`

`channel<T>` 是用于并发任务之间通信的共享容器句柄。标准库 `concurrent.vs` 提供 `channel_send`、`channel_recv` 和 `channel_close` 函数：

```VeroScript
import "concurrent.vs";

channel<int> values = channel<int>();
std::concurrent::channel_send(values, 42);
int result = std::concurrent::channel_recv(values);
std::concurrent::channel_close(values);
```

通道通常与 `spawn`、`join` 和任务函数一起使用。阻塞行为、可传输类型和任务生命周期属于并发运行时文档，而不是普通容器操作的一部分。

容器长度及元素数量（包括 `multiset.count(value)`）返回非负 `int64`。Deque 索引使用 `int64`，map 键仍保留其声明的键类型。长度转换及范围规则见[内置函数](../standard-library/builtins.md)，通道数量和容量见[并发模块](../standard-library/concurrent.md)。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/language-basics/containers.md) · [fr-FR](../../fr-FR/language-basics/containers.md) · [ja-JP](../../ja-JP/language-basics/containers.md) · [ko-KR](../../ko-KR/language-basics/containers.md) · [zh-CN](containers.md) · [zh-TW](../../zh-TW/language-basics/containers.md)
