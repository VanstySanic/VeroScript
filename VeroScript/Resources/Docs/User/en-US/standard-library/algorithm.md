# Algorithms

Module file: `algorithm.vs`

Import the module and use its namespace-qualified API:

```VeroScript
import "algorithm.vs";

array<int> values = {9, 2, 7, 2, 5};
std::algorithm::sort(values);
bool found = std::algorithm::binary_search(values, 7);
```

The module provides generic, in-place operations and does not copy array elements unless an API explicitly returns a new array. Ordering operations require element types that support `<`; equality operations require `==`. `lower_bound` and `binary_search` require an already sorted array. `find_index`, `min_index`, and `max_index` return `-1` when no result exists.
Ordering requires a strict weak order; floating-point NaN is not valid input for ordered operations. `binary_search` matches values equivalent under `<`, so ordered operations do not require `==`.

## API

| Function | Behavior |
| --- | --- |
| `std::algorithm::sort<T>(array<T> values)` | Sorts the array in ascending order in place. |
| `std::algorithm::reverse<T>(array<T> values)` | Reverses the array in place. |
| `std::algorithm::find_index<T>(array<T> values, T value)` | Returns the first matching index, or `-1`. |
| `std::algorithm::count<T>(array<T> values, T value)` | Counts matching elements. |
| `std::algorithm::lower_bound<T>(array<T> values, T value)` | Returns the first position whose value is not less than `value`; the input must be sorted. |
| `std::algorithm::binary_search<T>(array<T> values, T value)` | Tests whether a sorted array contains `value`. |
| `std::algorithm::fill<T>(array<T> values, T value)` | Assigns `value` to every element in place. |
| `std::algorithm::copy<T>(array<T> values)` | Returns a new array with the same elements. |
| `std::algorithm::contains<T>(array<T> values, T value)` | Tests whether any element equals `value`. |
| `std::algorithm::equal<T>(array<T> left, array<T> right)` | Tests equal length and pairwise equality. |
| `std::algorithm::is_sorted<T>(array<T> values)` | Tests ascending order. Empty arrays are sorted. |
| `std::algorithm::min_index<T>(array<T> values)` | Returns the index of the first minimum, or `-1` for an empty array. |
| `std::algorithm::max_index<T>(array<T> values)` | Returns the index of the first maximum, or `-1` for an empty array. |
| `std::algorithm::rotate<T>(array<T> values, int64 middle)` | Rotates in place so the element at `middle` becomes first; `middle` may be `0` or `len(values)`. |
| `std::algorithm::shuffle<T>(array<T> values, array<unsigned int> engine)` | Applies an in-place Fisher–Yates shuffle using the caller-owned random state. |

Arrays are shared container values, so in-place operations update the caller's array without copying its elements.

`find_index`, `count`, `lower_bound`, `min_index` and `max_index` return `int64`. `lower_bound` may return `len(values)`; the missing-result sentinel for `find_index`, `min_index` and `max_index` is `-1`. The `middle` argument to `rotate` is also `int64`.


- [Standard library foundation](foundation.md)

<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](algorithm.md) · [fr-FR](../../fr-FR/standard-library/algorithm.md) · [ja-JP](../../ja-JP/standard-library/algorithm.md) · [ko-KR](../../ko-KR/standard-library/algorithm.md) · [zh-CN](../../zh-CN/standard-library/algorithm.md) · [zh-TW](../../zh-TW/standard-library/algorithm.md)
