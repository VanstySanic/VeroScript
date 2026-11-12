# 事件模块

模块文件：`delegate.vs`

导入方式：

```VeroScript
import "delegate.vs";
```

公共脚本推荐使用 `std::delegate` 和 `std::multicast_delegate`。未限定写法对应同一类型，两种写法都需要导入 `delegate.vs`。成员名称统一使用小写。

## 单播委托

`delegate` 表示最多带三个参数的单个处理函数。类型参数的第一个参数是返回类型，后续参数是调用参数：

```VeroScript
std::delegate<int, int> mitigate = std::delegate<int, int>();
mitigate.bind(function(int value) -> int => value / 2);
int result = mitigate.execute(10);
```

支持 `delegate<R>`、`delegate<R, A>`、`delegate<R, A, B>` 和 `delegate<R, A, B, C>`。成员如下：

| 成员 | 说明 |
| --- | --- |
| `bind(handler)` | 绑定或替换处理函数。 |
| `unbind()` | 清除当前处理函数。 |
| `is_bound()` | 判断是否已经绑定。 |
| `execute(...)` | 执行处理函数；未绑定时会触发断言。 |
| `execute_if_bound(...)` | 仅在已绑定时执行，并返回是否执行。 |

`execute_if_bound` 只返回是否执行，不返回处理函数的结果。

`delegate` 的处理函数槽是私有的。`unbind()` 会清空委托保存的处理函数引用；通过 `is_bound()` 查询状态。

## 多播委托

`multicast_delegate` 保存一组返回 `void` 的处理函数，支持零到三个参数：

```VeroScript
std::multicast_delegate<int> on_score = std::multicast_delegate<int>();
on_score.add(function(int value) -> void => print(value));
on_score.broadcast(42);
```

成员如下：

| 成员 | 说明 |
| --- | --- |
| `add(handler)` | 添加处理函数。 |
| `remove(handler)` | 删除匹配的处理函数。 |
| `broadcast(...)` | 按当前注册顺序调用全部处理函数。 |
| `count()` | 以非负 `int64` 返回处理器数量。 |
| `clear()` | 清空处理函数。 |

广播开始时会先复制处理函数列表，因此广播期间对注册列表的修改不会改变当前这一轮已经确定的调用快照。

`multicast_delegate` 的 `handlers` 字段在源代码中是公开字段；需要保持注册顺序和快照语义时，应优先使用 `add`、`remove` 和 `clear`。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/events.md) · [fr-FR](../../fr-FR/standard-library/events.md) · [ja-JP](../../ja-JP/standard-library/events.md) · [ko-KR](../../ko-KR/standard-library/events.md) · [zh-CN](events.md) · [zh-TW](../../zh-TW/standard-library/events.md)
