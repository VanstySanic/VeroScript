# 演算法

模組檔案：`algorithm.vs`

這些 API 是泛型的；除了 `copy` 明確回傳新陣列外，其餘操作都會原地修改陣列。排序比較要求 `<`，相等比較要求 `==`。

| API | 行為 |
| --- | --- |
| `sort`、`reverse` | 原地升冪排序或反轉陣列。 |
| `find_index`、`count`、`contains` | 搜尋、計數或判斷是否包含值；`find_index` 找不到時回傳 `-1`。 |
| `lower_bound`、`binary_search` | 在已排序陣列中搜尋。 |
| `fill`、`copy`、`equal` | 原地填充、複製為新陣列、比較兩個陣列。 |
| `is_sorted` | 判斷陣列是否依升冪排列。 |
| `min_index`、`max_index` | 回傳第一個最小/最大元素的索引；空陣列回傳 `-1`。 |
| `rotate` | 原地旋轉，使 `middle` 位置的元素成為第一個元素；邊界值有效。 |
| `shuffle` | 使用 `random.vs` 的明確狀態執行原地 Fisher–Yates 洗牌。 |

`lower_bound` 和 `binary_search` 要求陣列已排序。

## 完整函式簽章

| 函式 | 簽章 |
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

呼叫範例：

```VeroScript
import "algorithm.vs";

array<int> values = {9, 2, 7, 2, 5};
std::algorithm::sort(values);
bool found = std::algorithm::binary_search(values, 7);
```

有序演算法要求 `<` 構成嚴格弱序；浮點 NaN 不適合作為輸入。`binary_search` 依 `<` 判斷等價，因此有序演算法不要求 `==`。

`find_index`、`count`、`lower_bound`、`min_index` 和 `max_index` 傳回 `int64`。`lower_bound` 可能傳回 `len(values)`；`find_index`、`min_index` 和 `max_index` 無結果時傳回 `-1`。`rotate` 的 `middle` 參數也是 `int64`。


- [標準函式庫基礎](foundation.md)

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/algorithm.md) · [fr-FR](../../fr-FR/standard-library/algorithm.md) · [ja-JP](../../ja-JP/standard-library/algorithm.md) · [ko-KR](../../ko-KR/standard-library/algorithm.md) · [zh-CN](../../zh-CN/standard-library/algorithm.md) · [zh-TW](algorithm.md)
