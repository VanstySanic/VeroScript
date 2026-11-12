# 알고리즘

모듈 파일: `algorithm.vs`

API는 제네릭이며 새 배열을 반환하는 `copy`를 제외하면 배열을 제자리에서 수정합니다.
순서 비교에는 `<`, 동등성 비교에는 `==`가 필요합니다.

| API | 동작 |
| --- | --- |
| `sort`, `reverse` | 배열을 제자리에서 정렬하거나 뒤집습니다. |
| `find_index`, `count`, `contains` | 값을 검색, 계산하거나 포함 여부를 확인합니다. 없으면 `find_index`는 `-1`입니다. |
| `lower_bound`, `binary_search` | 정렬된 배열을 검색합니다. |
| `fill`, `copy`, `equal` | 값을 채우고, 새 배열로 복사하고, 두 배열을 비교합니다. |
| `is_sorted` | 오름차순인지 확인합니다. |
| `min_index`, `max_index` | 첫 최소/최대 인덱스를 반환하며 빈 배열은 `-1`입니다. |
| `rotate` | `middle`의 요소가 첫 요소가 되도록 회전합니다. 경계값도 유효합니다. |
| `shuffle` | `random.vs`의 명시적 상태로 제자리 섞기를 수행합니다. |

`lower_bound`와 `binary_search`에는 정렬된 배열이 필요합니다.

## 전체 함수 시그니처

| 함수 | 시그니처 |
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

사용 예:

```VeroScript
import "algorithm.vs";

array<int> values = {9, 2, 7, 2, 5};
std::algorithm::sort(values);
bool found = std::algorithm::binary_search(values, 7);
```

순서 알고리즘에는 `<`가 엄격 약순서를 이뤄야 합니다. 부동소수점 NaN은 유효한 입력이 아닙니다. `binary_search`는 `<`에 따른 동등성을 사용하므로 순서 알고리즘에는 `==`가 필요하지 않습니다.

`find_index`, `count`, `lower_bound`, `min_index`, `max_index`는 `int64`를 반환합니다. `lower_bound`는 `len(values)`를 반환할 수 있습니다. `find_index`, `min_index`, `max_index`에서 결과가 없으면 `-1`입니다. `rotate`의 `middle` 인수도 `int64`입니다.


- [표준 라이브러리 기반](foundation.md)

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/algorithm.md) · [fr-FR](../../fr-FR/standard-library/algorithm.md) · [ja-JP](../../ja-JP/standard-library/algorithm.md) · [ko-KR](algorithm.md) · [zh-CN](../../zh-CN/standard-library/algorithm.md) · [zh-TW](../../zh-TW/standard-library/algorithm.md)
