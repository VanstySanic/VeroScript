# 문자열 모듈

모듈 파일: `string.vs`

다음을 사용하여 가져옵니다.

```VeroScript
import "string.vs";
```

## 쿼리 및 슬라이싱

| 기능 | 설명 |
| --- | --- |
| `std::strings::substring(string s, int64 start, int64 count)` | 바이트 오프셋 `start`부터 최대 `count`개의 UTF-8 바이트를 반환합니다. |
| `std::strings::find(string haystack, string needle)` | 첫 번째 일치 UTF-8 바이트 오프셋을 `int64`로 반환하며 없으면 `-1`입니다. |
| `std::strings::char_code(string s, int64 index)` | `index`의 바이트를 `int`(`0`–`255`)로 반환합니다. |
| `std::strings::contains(string s, string sub)` | 하위 문자열이 있는지 테스트합니다. |
| `std::strings::starts_with(string s, string prefix)` | 문자열이 접두사로 시작하는지 테스트합니다. |
| `std::strings::ends_with(string s, string suffix)` | 문자열이 접미사로 끝나는지 테스트합니다. |

문자열 인덱스는 유니코드 코드 포인트 인덱스가 아닌 `0`부터 시작하는 UTF-8
바이트 오프셋입니다. 범위를 벗어난 인덱스는 런타임 오류를 생성합니다.
`std::strings::find`, `std::strings::last_find`, `std::strings::substring`, `std::strings::char_code`는 같은 바이트 오프셋 규칙을 사용합니다.

## 분할, 결합 및 변환

| 기능 | 설명 |
| --- | --- |
| `std::strings::split(string s, string separator)` | 문자열을 `array<string>`로 분할합니다. |
| `std::strings::join(array<string> parts, string separator)` | 구분 기호를 사용하여 문자열 배열을 결합합니다. |
| `std::strings::trim_left(string s)` | 왼쪽의 ASCII 공백(탭, 줄 바꿈, 세로 탭, 폼 피드, 캐리지 리턴, 스페이스)을 제거합니다. |
| `std::strings::trim_right(string s)` | 오른쪽에서 공백을 제거합니다. |
| `std::strings::trim(string s)` | 양쪽에서 공백을 제거합니다. |
| `std::strings::to_lower(string s)` | 텍스트를 소문자로 변환합니다. |
| `std::strings::to_upper(string s)` | 텍스트를 대문자로 변환합니다. |
| `std::strings::replace(string s, string from, string to)` | 일치하는 하위 문자열을 바꿉니다. `from`가 비어 있으면 원래 문자열을 반환합니다. |
| `std::strings::last_find(string haystack, string needle)` | 마지막 일치 항목의 UTF-8 바이트 오프셋을 반환하며 없으면 `-1`입니다. 빈 검색 문자열은 `len(haystack)`을 반환합니다. |
| `std::strings::repeat(string value, int64 count)` | 문자열을 `count`번 반복합니다. 음수 개수는 런타임 오류입니다. |
| `std::strings::remove_prefix(string value, string prefix)` | 접두사가 있으면 제거하고, 없으면 원래 문자열을 반환합니다. |
| `std::strings::remove_suffix(string value, string suffix)` | 접미사가 있으면 제거하고, 없으면 원래 문자열을 반환합니다. |

## 서식 지정

이 모듈은 두 가지 형식 지정 기능을 제공합니다. `format`은 중괄호 필드를 사용하고 `formatf`은 printf 스타일 변환을 사용합니다. 두 문법은 독립적이므로 혼합되어서는 안 됩니다.

공개 서명은 다음과 같습니다.

```text
string format(string fmt, array<string> args);
string formatf(string fmt, array<string> args);
```

스크립트 코드는 `string`, 부울, 부동 소수점 값 및 부호 있는 또는 부호 없는 정수를 직접 전달할 수 있습니다. 컴파일러는 해당 스칼라를 문자열로 변환하고 이를 `array<string>`로 수집합니다. 기존 `array<string>`를 두 번째 인수로 직접 전달할 수도 있습니다.

```VeroScript
import "string.vs";

string text = format("value={}, name={}", 42, "VeroScript");
string hex = format("{:8X}", 255);
string legacy = formatf("value=%d", 42);
```

### `format` 필드

| 양식 | 의미 |
| --- | --- |
| `{}` | 다음 자동 인수를 사용합니다. |
| `{N}` | 명시적인 0부터 시작하는 인덱스를 사용합니다. |
| `{:spec}` / `{N:spec}` | `[alignment][width][.precision][type]` 형식의 사양을 사용합니다. |
| `{{` / `}}` | 리터럴 중괄호를 내보냅니다. |

정렬은 `<`, `>` 또는 `^`이며 오른쪽 정렬이 기본값입니다. 너비와 정밀도는 `0`–`1024`로 제한되며 공백 패딩을 사용합니다. 유일한 유형 코드는 소문자 및 대문자 16진수의 경우 `x` 및 `X`입니다. 정밀도는 유한한 숫자 값을 구문 분석하고 고정 소수점 숫자를 내보냅니다. 자동 인덱스와 명시적 인덱스를 혼합하지 마십시오. 누락된 인수, 일치하지 않는 중괄호 및 잘못된 사양은 런타임 오류를 발생시킵니다.

<a id="integer-formatting"></a>

format x/X는 양수 UINT64_MAX, 음수 INT64_MIN까지 64비트로 처리하고 음수는 64비트 보수입니다. -1은 f 16개입니다. printf 정수는 double을 경유하지 않습니다. 폭과 정밀도는 계속 1024 이하입니다.

### `formatf` 전환

변환 형태는 `%[flags][width][.precision][length]conversion`입니다. 지원되는 플래그는 `-`, `+`, 공백, `#` 및 `0`입니다. 너비와 정밀도는 `0`–`1024`으로 제한되며 동적 `*`은 지원되지 않습니다. 길이 수정자 `h`, `hh`, `l`, `ll`, `j`, `z` 및 `t`는 소스 호환성을 위해 허용되지만 VeroScript 값 너비 또는 ABI를 변경하지 않습니다.

지원되는 변환은 다음과 같습니다.

| 전환 | 의미 |
| --- | --- |
| `%d`, `%i` | 부호 있는 정수. |
| `%u`, `%o`, `%x`, `%X` | 음수가 아닌 10진수, 8진수 또는 소문자/대문자 16진수 정수입니다. |
| `%f`, `%F`, `%e`, `%E`, `%g`, `%G` | 부동 소수점; 기본 정밀도는 `6`입니다. |
| `%c` | `0`부터 `255`까지의 정수 또는 1바이트 문자열. 정밀도가 유효하지 않습니다. |
| `%s` | 문자열. 정밀도는 출력되는 바이트 수를 제한합니다. |
| `%%` | 인수를 사용하지 않고 리터럴 `%`입니다. |

`printf`은 `formatf`과 동일한 변환 문법을 사용하며 표준 출력에 직접 씁니다. `print`의 다중 인수 형식은 `format`의 중괄호 문법을 사용합니다. 변환에 사용되지 않은 추가 인수는 무시됩니다. 전체 규칙 및 오류 경계는 [문자열 언어 동작](../language-basics/strings.md#%EC%84%9C%EC%8B%9D-%EC%A7%80%EC%A0%95)을 참조하세요.
`std::strings::to_lower`와 `std::strings::to_upper`는 ASCII 영문자만 변환하고 다른 UTF-8 바이트는 그대로 둡니다. Unicode 대소문자 변환은 수행하지 않습니다.
`std::strings::trim_left`, `std::strings::trim_right`, `std::strings::trim`은 양 끝의 ASCII `0x09`~`0x0D`와 공백(`0x20`)을 제거합니다. 내부와 비 ASCII 바이트는 보존합니다.

`len(s)`, `std::strings::find`, `std::strings::last_find`는 `int64`를 반환하며 일치 항목이 없으면 `-1`입니다. `std::strings::substring`의 바이트 오프셋과 개수는 `int64`입니다. `start`는 `len(s)`와 같아도 되고 `count`는 음수가 아니어야 하며 남은 바이트 수를 넘으면 사용 가능한 접미사 길이로 줄입니다. `std::strings::char_code`는 `int64` 바이트 인덱스를 받아 바이트 값을 `int`(`0`–`255`)로 반환합니다. `std::strings::repeat`는 `int64` 개수를 받고 결과 길이가 `int64` 범위를 넘으면 거부합니다. 할당 제한도 계속 적용됩니다.

- [표준 라이브러리 기반](foundation.md)

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/string.md) · [fr-FR](../../fr-FR/standard-library/string.md) · [ja-JP](../../ja-JP/standard-library/string.md) · [ko-KR](string.md) · [zh-CN](../../zh-CN/standard-library/string.md) · [zh-TW](../../zh-TW/standard-library/string.md)
