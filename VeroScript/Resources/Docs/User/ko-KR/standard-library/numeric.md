# 수치 집계

모듈 파일: `numeric.vs`

`std::numeric::accumulate(values, initial)`은 명시적 초기값을 사용하므로 빈 배열도 지원합니다. `sum`과 `average`는 비어 있지 않은 배열이 필요하며 빈 입력은 어설션 실패를 일으킵니다. `average`는 각 요소를 `double`로 변환합니다.
`average`는 누적 평균을 사용해 유한한 값의 중간 합계가 넘치는 문제를 피합니다. 결과에는 부동소수점 반올림이 적용됩니다.


- [표준 라이브러리 기반](foundation.md)


## 큰 정수와 정확한 유리수

`bigint.vs`, `rational.vs` 또는 `core.vs`를 가져옵니다. 형식은 코어 네이티브 카탈로그가 제공합니다. 값은 불변이며 컨테이너, 필드, 인수, 반환 값 및 `std::attribute<T>`에서 값으로 복사됩니다.

정수 나눗셈은 0 방향으로 버리며 나머지는 피제수와 같은 부호입니다. 오른쪽 시프트는 산술 시프트이며 비트 연산은 무한 길이 2의 보수입니다. 유리수는 항상 기약 분수이며 분모는 양수입니다. 정규 텍스트는 `분자/분모`이며 분모가 1이면 정수만, 0이면 `0`을 출력합니다. 실수, bool, enum 및 일반 정수에서 암시적으로 변환하지 않습니다.

명시적 정수/문자열 생성자, `parse`, `try_parse` 및 변환 메서드를 사용합니다. 정수 입력은 64비트를 유지합니다. `from_double_exact`는 저장된 이진 값을 사용하므로 0.1은 1/10과 다릅니다. `to_double`/`to_float`는 대상 형식으로 직접 짝수 반올림하고 `_exact`는 정밀도 손실을 거부합니다. 분수는 정수 변환 전에 명시적으로 반올림해야 합니다. 일반적인 `try_` 실패는 출력을 유지하지만 제한, 취소 및 할당 실패는 오류를 전파합니다.

`std::numeric::gcd`, `lcm`, `divmod`, `pow`는 정확합니다. 정수 지수는 음수가 아니어야 하며 유리수는 음수 지수를 지원합니다. 개수와 진수는 정수만 허용하고 축소 전에 범위를 검사합니다. `sum`/`accumulate`는 정확하고 `average`는 double을 사용하는 근삿값입니다. 기본 서식에도 숫자 제한이 적용됩니다.

`round_with_mode`와 `to_decimal_with_mode`는 `std::numeric::rounding_mode::{toward_zero, floor, ceil, away_from_zero, half_even, half_away_from_zero}`를 사용합니다. 기본값은 half_even이며 요청한 소수 자릿수를 유지합니다. 네이티브 런타임이 필요하며 현재 독립 AOT 내보내기는 이러한 연산을 거부합니다.

```VeroScript
import "rational.vs";

std::bigint n = std::bigint("18446744073709551616");
auto qr = std::numeric::divmod(n, std::bigint(3));
assert(qr.first * std::bigint(3) + qr.second == n);
std::rational r = std::rational(1, 3) + std::rational(1, 6);
assert(r.to_string() == "1/2");
assert(r.to_decimal(4) == "0.5000");
```

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/numeric.md) · [fr-FR](../../fr-FR/standard-library/numeric.md) · [ja-JP](../../ja-JP/standard-library/numeric.md) · [ko-KR](numeric.md) · [zh-CN](../../zh-CN/standard-library/numeric.md) · [zh-TW](../../zh-TW/standard-library/numeric.md)
