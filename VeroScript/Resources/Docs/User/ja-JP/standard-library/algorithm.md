# アルゴリズム

モジュール: `algorithm.vs`

API はジェネリックで、`copy` が新しい配列を返す点を除き配列をその場で変更します。
順序比較には `<`、等値比較には `==` が必要です。

| API | 動作 |
| --- | --- |
| `sort`, `reverse` | 配列をその場でソートまたは反転します。 |
| `find_index`, `count`, `contains` | 値を検索、計数、または含有判定します。見つからない `find_index` は `-1` です。 |
| `lower_bound`, `binary_search` | ソート済み配列を検索します。 |
| `fill`, `copy`, `equal` | 全要素を設定、新しい配列へのコピー、配列比較を行います。 |
| `is_sorted` | 昇順かどうかを判定します。 |
| `min_index`, `max_index` | 最初の最小/最大要素の index、空配列では `-1` を返します。 |
| `rotate` | `middle` の要素を先頭にするよう回転します。境界値も有効です。 |
| `shuffle` | `random.vs` の明示的状態でその場でシャッフルします。 |

`lower_bound` と `binary_search` にはソート済み配列が必要です。

## 関数シグネチャ

| 関数 | シグネチャ |
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

使用例：

```VeroScript
import "algorithm.vs";

array<int> values = {9, 2, 7, 2, 5};
std::algorithm::sort(values);
bool found = std::algorithm::binary_search(values, 7);
```

順序を扱うアルゴリズムでは `<` が厳密弱順序を満たす必要があります。浮動小数点の NaN は有効な入力ではありません。`binary_search` は `<` による同値性を使うため、順序を扱うアルゴリズムに `==` は不要です。

`find_index`、`count`、`lower_bound`、`min_index`、`max_index` は `int64` を返します。`lower_bound` は `len(values)` を返す場合があります。`find_index`、`min_index`、`max_index` で結果がない場合の値は `-1` です。`rotate` の `middle` 引数も `int64` です。


- [標準ライブラリ基盤](foundation.md)

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/algorithm.md) · [fr-FR](../../fr-FR/standard-library/algorithm.md) · [ja-JP](algorithm.md) · [ko-KR](../../ko-KR/standard-library/algorithm.md) · [zh-CN](../../zh-CN/standard-library/algorithm.md) · [zh-TW](../../zh-TW/standard-library/algorithm.md)
