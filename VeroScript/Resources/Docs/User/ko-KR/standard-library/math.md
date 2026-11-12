# 수학 모듈

모듈 파일: `math.vs`

다음을 사용하여 가져옵니다.

```VeroScript
import "math.vs";
```

## 기능

| 기능 | 설명 |
| --- | --- |
| `std::sqrt(double x)` | 제곱근을 반환합니다. |
| `std::sin(double x)` | 사인을 반환합니다. 인수의 단위는 라디안입니다. |
| `std::cos(double x)` | 코사인을 반환합니다. 인수의 단위는 라디안입니다. |
| `std::tan(double x)` | 탄젠트를 반환합니다. 인수의 단위는 라디안입니다. |
| `std::floor(double x)` | 음의 무한대로 반올림됩니다. |
| `std::ceil(double x)` | 양의 무한대로 반올림됩니다. |
| `std::round(double x)` | 가장 가까운 정수 값으로 반올림하며, 정확히 중간이면 0에서 멀어지는 방향으로 반올림합니다. |
| `std::trunc(double x)` | 0을 향해 반올림하여 소수 부분을 제거합니다. |
| `std::pow(double base, double exponent)` | `base`를 `exponent`의 거듭제곱으로 계산합니다. |
| `std::exp(double x)` | `x`의 자연 지수값을 반환합니다. |
| `std::log(double x)` | `x`의 자연 로그를 반환합니다. |
| `std::log2(double x)` | `x`의 밑이 2인 로그를 반환합니다. |
| `std::asin(double x)` / `std::acos(double x)` | 역 사인 또는 역 코사인을 라디안으로 반환합니다. |
| `std::atan2(double y, double x)` | `(x, y)`의 각도를 라디안으로 반환합니다. |
| `std::isfinite(double x)` / `std::isnan(double x)` | 값이 유한한지 또는 NaN인지 확인합니다. |
| `std::pi()` / `std::e()` | 수학 상수 π 또는 e를 반환합니다. |
| `std::clamp<T>(T x, T lo, T hi)` | 숫자 형식 `T` 값을 `[lo, hi]`로 제한합니다. |
| `std::clamp_int(int x, int lo, int hi)` | 정수를 `[lo, hi]`로 고정합니다. |

`std::clamp<T>`는 bool 이외의 기본 숫자 형식을 지원하고 일치하는 인수 형식으로 `T`를 추론할 수 있습니다. 역순 또는 NaN 경계는 거부하며 NaN 입력은 유지합니다. `std::clamp_int`는 int에 같은 연산을 호출합니다. 변환 규칙은 [기반 참조](foundation.md)를 따릅니다.

절대값, 최소값 및 최대값은 `math.vs`에서 내보내는 것이 아니라 런타임에서 제공되는 전역 내장 기능입니다.


- [표준 라이브러리 기반](foundation.md)

<a id="floating-results"></a>

부동소수점 정의역 오류나 오버플로는 NaN 또는 무한대를 만들 수 있습니다. 유한한 결과가 필요하면 `std::isnan` / `std::isfinite`로 확인하세요. NaN의 문자열 표기와 부호 표시는 대상 플랫폼에 따라 달라지므로 이식 가능한 식별자로 사용하지 마세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/math.md) · [fr-FR](../../fr-FR/standard-library/math.md) · [ja-JP](../../ja-JP/standard-library/math.md) · [ko-KR](math.md) · [zh-CN](../../zh-CN/standard-library/math.md) · [zh-TW](../../zh-TW/standard-library/math.md)
