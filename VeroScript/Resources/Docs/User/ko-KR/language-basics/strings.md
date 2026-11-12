# 문자열

VeroScript는 `string`과 `wstring`의 두 가지 텍스트 유형을 제공합니다. 둘 다 텍스트 값을 나타내지만 고유한 유형이므로 명시적인 변환 없이 할당, 비교 또는 연결에 혼합할 수 없습니다. 이 페이지에서는 `string.vs`의 문자열 동작과 일반적인 도우미에 대해 설명합니다. 리터럴 구문은 [리터럴](literals.md)에서 다룹니다.

## 텍스트 유형

일반 문자열 리터럴의 유형은 `string`이고, 넓은 문자열 리터럴의 유형은 `wstring`입니다.

```VeroScript
string name = "VeroScript";
wstring title = L"Language reference";
```

`string` 및 `wstring`은 모두 `len`, 동일성 비교 및 동일한 텍스트 유형의 값과의 연결을 지원합니다. 연결의 피연산자는 동일한 텍스트 유형에 속해야 합니다.

```VeroScript
string message = "Hello, " + name;
wstring heading = L"Vero" + L"Script";
```

두 텍스트 유형 간의 명시적 변환을 위해 `to_wstring` 또는 `to_string`을 사용하십시오.

```VeroScript
wstring wide_name = to_wstring(name);
string narrow_name = to_string(wide_name);
```

숫자 또는 부울 값을 텍스트로 변환할 때도 `to_string`을 사용합니다. 이는 암시적 혼합 유형 연결에 의존하는 대신 명시적인 변환을 유지합니다.

```VeroScript
string summary = "count=" + to_string(3);
```

## 문자열 표준 라이브러리

문자열 도우미는 `string.vs`에서 제공됩니다. 사용하기 전에 모듈을 가져오세요.

```VeroScript
import "string.vs";
```

일반적인 기능은 다음과 같습니다:

| 기능 | 설명 |
| --- | --- |
| `std::strings::substring(s, start, count)` | `start`에서 시작하여 지정된 수의 문자를 추출합니다. |
| `std::strings::find(s, needle)` | 하위 문자열을 찾아 해당 위치를 반환합니다. 찾을 수 없으면 음수 값을 반환합니다. |
| `std::strings::char_code(s, index)` | 인덱스의 문자 코드를 반환합니다. |
| `std::strings::contains(s, sub)` | 하위 문자열이 있는지 테스트합니다. |
| `std::strings::starts_with(s, prefix)` | 문자열이 접두사로 시작하는지 테스트합니다. |
| `std::strings::ends_with(s, suffix)` | 문자열이 접미사로 끝나는지 테스트합니다. |
| `std::strings::trim_left(s)` / `std::strings::trim_right(s)` | 왼쪽 또는 오른쪽에서 공백을 제거합니다. |
| `std::strings::trim(s)` | 양쪽에서 공백을 제거합니다. |
| `std::strings::split(s, separator)` | 문자열을 `array<string>`로 분할합니다. |
| `std::strings::join(parts, separator)` | 구분 기호를 사용하여 문자열 배열을 결합합니다. |
| `std::strings::to_lower(s)` / `std::strings::to_upper(s)` | 텍스트를 소문자 또는 대문자로 변환합니다. |
| `std::strings::replace(s, from, to)` | 일치하는 부분 문자열을 지정된 텍스트로 바꿉니다. |
| `std::strings::last_find(s, needle)` | 마지막으로 일치한 UTF-8 바이트 오프셋을 반환하며, 없으면 `-1`을 반환합니다. |
| `std::strings::repeat(s, count)` | `s`를 반복합니다. 음수 count는 런타임 오류입니다. |
| `std::strings::remove_prefix(s, prefix)` / `std::strings::remove_suffix(s, suffix)` | 일치하는 접두사 또는 접미사를 제거하고, 없으면 `s`를 반환합니다. |

이러한 함수는 새 값을 반환하고 입력 문자열을 수정하지 않습니다. 예를 들어:

```VeroScript
import "string.vs";

string raw = "  red,green,blue  ";
array<string> colors = std::strings::split(std::strings::trim(raw), ",");
string normalized = std::strings::join(colors, " | ");
print(normalized);
```

`trim_left`, `trim_right`, `trim`은 ASCII 공백 바이트 `0x09`부터 `0x0D` 및 공백(`0x20`)을 제거합니다. 문자열 인덱스는 0부터 시작하는 UTF-8 바이트 오프셋이며 Unicode 코드 포인트 인덱스가 아닙니다. `len`, `find`, `last_find`는 `int64`를 반환하고 일치하지 않으면 `-1`입니다. 슬라이스 위치와 개수, `char_code` 인덱스 및 `repeat` 개수는 `int64`를 사용합니다. 범위와 반환 타입은 [문자열 모듈](../standard-library/string.md)을 참조하세요.

## 서식 지정

VeroScript는 두 가지 독립적인 형식 방언을 제공합니다. `format`은 중괄호 필드를 사용합니다. `formatf` 및 `printf`는 printf 스타일 변환을 사용합니다. 하나의 형식 문자열에 두 방언을 혼합하지 마십시오.

### 중괄호 형식 지정

`string.vs`의 공개 서명은 다음과 같습니다.

```text
string format(string fmt, array<string> args);
```

스크립트 코드는 일반적으로 스칼라 인수를 직접 전달합니다. 컴파일러는 `string`, 부울, 부동 소수점 값, 부호 있는 정수 또는 부호 없는 정수를 문자열로 변환하고 이를 인수 배열로 수집합니다.

```VeroScript
import "string.vs";

string message = format("name={}, score={}", "Ada", 97);
print(message);
```

`array<string>`이 이미 존재하는 경우 두 번째 인수로 직접 전달할 수 있습니다.

```VeroScript
import "string.vs";

array<string> args = array<string>();
args.push("Ada");
args.push("97");
string message = format("name={}, score={}", args);
```

필드 양식은 다음과 같습니다.

| 양식 | 의미 |
| --- | --- |
| `{}` | `0` 인덱스에서 시작하는 다음 자동 인수를 사용합니다. |
| `{N}` | `{0}`과 같은 명시적인 0부터 시작하는 인수 인덱스를 사용합니다. |
| `{:spec}` | 형식 사양과 함께 다음 자동 인수를 사용합니다. |
| `{N:spec}` | 형식 사양과 함께 명시적 인덱스를 사용합니다. |
| `{{` / `}}` | 리터럴 `{` / `}`를 내보냅니다. |

형식 사양의 형식은 `[alignment][width][.precision][type]`입니다.

| 부품 | 허용되는 값 | 의미 |
| --- | --- | --- |
| 정렬 | `<`, `>`, `^` | 왼쪽, 오른쪽 또는 가운데 정렬; 오른쪽 정렬이 기본값입니다. |
| 폭 | 10진수 `0`–`1024` | 짧은 결과를 공백으로 채웁니다. 값을 자르지 않습니다. |
| 정밀 | `.decimal-integer`, `0`–`1024` | 유한한 숫자 값을 구문 분석하고 해당 숫자의 고정 소수점 출력을 내보냅니다. |
| 유형 | `x`, `X` | 10진수 정수를 소문자 또는 대문자 16진수로 변환합니다. |

`format`의 `x`/`X` 변환은 부호 있는 32비트 10진수 정수를 구문 분석한 다음 해당 32비트 16진수 표현을 내보냅니다. 전체 부호 없는 64비트 범위가 필요한 경우 `formatf`와 함께 `%x` 또는 `%X`를 사용하십시오.

예를 들어:

```VeroScript
import "string.vs";

string right = format("{:>6}", "ok");   // "    ok"
string left = format("{:<6}", "ok");    // "ok    "
string center = format("{:^6}", "ok");  // "  ok  "
string fixed = format("{:.2}", "1.5");  // "1.50"
string hex = format("{:X}", "255");     // "FF"
string repeated = format("{0} / {0}", "same");
string braces = format("{{value}}", array<string>()); // "{value}"
```

자동 인덱스는 자동 필드를 사용할 때만 진행됩니다. 예상치 못한 결과를 방지하려면 하나의 형식 문자열 내에서 자동 또는 명시적 인덱싱을 일관되게 사용하십시오. 누락된 인수, 일치하지 않는 중괄호, 유효하지 않은 사양 및 숫자 사양에 대해 구문 분석할 수 없는 값으로 인해 런타임 오류가 발생합니다. 필드에서 참조되지 않는 추가 인수는 무시됩니다.

### printf 형식 지정

`formatf`의 공개 서명은 다음과 같습니다.

```text
string formatf(string fmt, array<string> args);
```

스크립트 코드는 스칼라 인수를 직접 전달할 수도 있습니다. `printf`은 동일한 문법을 사용하지만 표준 출력에 직접 씁니다. 변환의 형태는 다음과 같습니다.

```text
%[flags][width][.precision][length]conversion
```

| 부품 | 허용되는 값 또는 한계 |
| --- | --- |
| 플래그 | `-`은 왼쪽 정렬이고, `+`은 양수 기호를 표시하고, 공백 접두사는 양수에 공백이 있고, `#`는 대체 접두사를 활성화하고, `0`은 0 패딩을 활성화합니다. |
| 폭 | 10진수 `0`–`1024`; 동적 너비 `*`은 지원되지 않습니다. |
| 정밀 | `.digits`, `0`–`1024`; 동적 정밀도 `.*`은 지원되지 않습니다. |
| 길이 | `h`, `hh`, `l`, `ll`, `j`, `z` 및 `t`은 소스 호환성을 위해 허용되지만 VeroScript 값 너비 또는 ABI를 변경하지 마십시오. |
| 전환 | `d`, `i`, `u`, `o`, `x`, `X`, `f`, `F`, `e`, `E`, `g`, `G`, `c`, 및 `s`. |

변환에는 다음과 같은 인수 형식이 필요합니다.

| 전환 | 인수 및 결과 |
| --- | --- |
| `%d`, `%i` | 부호 있는 정수. |
| `%u` | 음수가 아닌 정수. |
| `%o` | 8진수. |
| `%x`, `%X` | 소문자 또는 대문자 16진수 정수입니다. |
| `%f`, `%F` | 고정 소수점 부동 소수점; 기본 정밀도는 `6`입니다. |
| `%e`, `%E` | 과학적 표기법; 대문자 변형은 대문자를 사용합니다. |
| `%g`, `%G` | 기본 부동 소수점 표현. 대문자 변형은 대문자를 사용합니다. |
| `%c` | `0`부터 `255`까지의 정수 또는 1바이트 문자열. 정밀도가 유효하지 않습니다. |
| `%s` | 문자열; 정밀도는 방출된 바이트 수를 제한합니다. |
| `%%` | 인수를 사용하지 않고 리터럴 `%`을 내보냅니다. |

`%c` 및 `%s`은 `-` 플래그와 너비만 허용합니다. 숫자 변환의 경우 정밀도가 정수 0 패딩보다 우선합니다. 후행 `%`, 인수 누락, 지원되지 않는 변환 또는 요청된 변환에 대해 구문 분석할 수 없는 값으로 인해 런타임 오류가 발생합니다. 부동 소수점 변환에는 유한한 숫자 값이 필요합니다. 변환에서 사용되지 않는 추가 인수는 무시됩니다.

```VeroScript
import "string.vs";

string text = formatf("d=%+06d x=%#08X f=%.2f s=%-5.3s c=%c", -42, 255, 3.14159, "abcdef", 65);
printf("%s%% done", text);
```

### 출력 함수와의 관계

```VeroScript
import "string.vs";

print("name={}, score={}", "Ada", 97); // print's multi-argument form uses braces
printf("score=%03d", 7);                // printf always uses printf syntax
string value = format("score={}", 7);   // use format when a string is needed
```

하나의 값으로 `print`은 이를 직접 작성합니다. 형식 문자열과 추가 인수를 사용하면 중괄호 형식을 사용합니다. `format` 및 `formatf`는 문자열을 반환하는 반면 `printf`은 표준 출력에 씁니다. `format`, `formatf` 및 `printf`은 참조 인수 또는 `void` 표현식을 형식 지정 인수로 허용하지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/strings.md) · [fr-FR](../../fr-FR/language-basics/strings.md) · [ja-JP](../../ja-JP/language-basics/strings.md) · [ko-KR](strings.md) · [zh-CN](../../zh-CN/language-basics/strings.md) · [zh-TW](../../zh-TW/language-basics/strings.md)
