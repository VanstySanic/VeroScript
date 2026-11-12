# 算法

模块文件：`algorithm.vs`

这些 API 是泛型的；`sort`、`reverse`、`fill`、`rotate` 和 `shuffle` 原地修改数组，`copy` 返回新数组，其余函数只查询。排序比较要求 `<`，相等比较要求 `==`。

| API | 行为 |
| --- | --- |
| `sort`、`reverse` | 原地升序排序或反转数组。 |
| `find_index`、`count`、`contains` | 查找、计数或判断是否包含值；`find_index` 未找到时返回 `-1`。 |
| `lower_bound`、`binary_search` | 在已排序数组中查找。 |
| `fill`、`copy`、`equal` | 原地填充、复制为新数组、比较两个数组。 |
| `is_sorted` | 判断数组是否按升序排列。 |
| `min_index`、`max_index` | 返回第一个最小/最大元素的索引；空数组返回 `-1`。 |
| `rotate` | 原地旋转，使 `middle` 位置的元素成为第一个元素；边界值有效。 |
| `shuffle` | 使用 `random.vs` 的显式状态执行原地 Fisher–Yates 洗牌。 |

`lower_bound` 和 `binary_search` 要求数组已经排序。
有序算法要求 `<` 构成严格弱序；浮点 NaN 不满足这个前提，不应传给有序算法。`binary_search` 按 `<` 判断等价，因此有序算法不要求 `==`。

## 完整函数签名

| 函数 | 签名 |
| --- | --- |
| `sort` | `std::algorithm::sort<T>(array<T> values)` |
| `reverse` | `std::algorithm::reverse<T>(array<T> values)` |
| `find_index` | `std::algorithm::find_index<T>(array<T> values, T value)` |
| `count` | `std::algorithm::count<T>(array<T> values, T value)` |
| `lower_bound` | `std::algorithm::lower_bound<T>(array<T> values, T value)` |
| `binary_search` | `std::algorithm::binary_search<T>(array<T> values, T value)` |
| `fill` | `std::algorithm::fill<T>(array<T> values, T value)` |
| `copy` | `std::algorithm::copy<T>(array<T> values)` |
| `contains` | `std::algorithm::contains<T>(array<T> values, T value)` |
| `equal` | `std::algorithm::equal<T>(array<T> left, array<T> right)` |
| `is_sorted` | `std::algorithm::is_sorted<T>(array<T> values)` |
| `min_index` | `std::algorithm::min_index<T>(array<T> values)` |
| `max_index` | `std::algorithm::max_index<T>(array<T> values)` |
| `rotate` | `std::algorithm::rotate<T>(array<T> values, int64 middle)` |
| `shuffle` | `std::algorithm::shuffle<T>(array<T> values, array<unsigned int> engine)` |

调用示例：

```VeroScript
import "algorithm.vs";

array<int> values = {9, 2, 7, 2, 5};
std::algorithm::sort(values);
bool found = std::algorithm::binary_search(values, 7);
```

`find_index`、`count`、`lower_bound`、`min_index` 和 `max_index` 返回 `int64`。`lower_bound` 可能返回 `len(values)`；`find_index`、`min_index` 和 `max_index` 无结果时返回 `-1`。`rotate` 的 `middle` 参数也是 `int64`。


- [标准库基础](foundation.md)

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/algorithm.md) · [fr-FR](../../fr-FR/standard-library/algorithm.md) · [ja-JP](../../ja-JP/standard-library/algorithm.md) · [ko-KR](../../ko-KR/standard-library/algorithm.md) · [zh-CN](algorithm.md) · [zh-TW](../../zh-TW/standard-library/algorithm.md)
