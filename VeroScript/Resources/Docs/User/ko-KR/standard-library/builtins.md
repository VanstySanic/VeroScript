# 내장

[표준 라이브러리 기반](foundation.md#foundation-imports)을 참조하세요.

기본 제공과 [표준 라이브러리 모듈](index.md) 사이의 경계는 의도적입니다. 가장 작은 런타임 기본 요소에는 기본 제공을 사용하고 더 높은 수준의 문자열, 파일, 경로, 동시성 및 이벤트 기능에는 해당 모듈을 선호합니다.

## 출력

| 기능 | 설명 |
| --- | --- |
| `print(value)` | 표준 출력에 하나의 값을 씁니다. 형식 문자열과 추가 인수를 사용하면 중괄호 형식을 사용합니다. `{{` 및 `}}`는 리터럴 중괄호를 내보냅니다. |
| `printf(format, ...)` | printf 스타일 변환으로 형식화된 출력을 작성합니다. 해당 형식 구문은 `print`와 다릅니다. |
| `eprint(value)` | 오류 스트림에 하나의 값을 씁니다. |

```VeroScript
int main()
{
	int score = 97;
	print("score={}", score);
	printf("score=%03d", score);
	eprint("diagnostic");
	return 0;
}
```

형식화된 결과를 변수에 저장해야 하는 경우 표준 라이브러리 `format` 또는 `formatf` 함수를 사용하십시오. 필드, 변환, 너비, 정밀도 및 오류 경계는 [문자열 언어 동작](../language-basics/strings.md#%EC%84%9C%EC%8B%9D-%EC%A7%80%EC%A0%95) 및 [문자열 모듈](string.md)을 참조하세요.

## 콘솔 스트림

`std::console` 함수는 런타임에서 제공하므로 가져오기가 필요하지 않습니다.
스트림과 대화형 터미널 의미를 명확히 하기 위해 네임스페이스 전용으로 제공됩니다.

| 함수 | 설명 |
| --- | --- |
| `std::console::write(value)` | 줄바꿈을 추가하지 않고 표준 출력에 값을 씁니다. |
| `std::console::error(value)` | 줄바꿈을 추가하지 않고 오류 스트림에 값을 씁니다. |
| `std::console::flush()` | 대기 중인 표준 출력을 플러시합니다. |
| `std::console::clear()` | 호스트가 지원하면 대화형 출력을 지우고, 작업이 수행되었는지 반환합니다. |
| `std::console::is_interactive()` | 표준 입력과 표준 출력이 모두 대화형인지 반환합니다. |

다음 줄바꿈이나 호스트 도구로 제어를 넘기기 전에 진행 상황을 표시하려면
`std::console::flush()`를 사용하십시오. 디버그 어댑터는 플러시된 표준 출력과 오류 출력을
서로 다른 출력 이벤트로 전달합니다.

### 터미널 제어

| API | |
| --- | --- |
| `supports_color()`, `supports_cursor()`, `supports_clear()`, `is_output_terminal()` | 색상, 커서, 화면／줄 지우기 및 출력 터미널 기능을 각각 조회합니다. |
| `set_color(string foreground, string background)`, `reset_style()` | 전경／배경색 설정과 스타일 초기화. |
| `clear_line(string mode)` | 커서 뒤, 앞 또는 줄 전체를 지웁니다. 현재 위치를 포함하며 커서는 유지합니다. |
| `move_cursor(int column, int row)`, `set_cursor_visible(bool visible)` | 0부터 시작하는 열／행으로 이동, 커서 표시／숨김. |
| `width()`, `height()` | 표시 창의 열／행 수. 알 수 없으면 0. |

제어 함수는 `bool`을 반환하며 지원하지 않거나 실패하면 `false`입니다. 리디렉션 출력에는 제어 코드를 자동으로 쓰지 않으며 입력의 대화형 여부와 무관합니다. `clear()`는 표시 화면만 지우고 원점으로 이동합니다. 기록은 지우지 않습니다. 좌표는 `0..32766` 정수이며 알려진 창 밖이면 `false`입니다. 잘못된 형식, 색상 이름 및 줄 모드는 오류입니다. 변경에는 콘솔 출력 권한이 필요합니다. 색상은 `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`, `bright_` 변형 및 `default`입니다.

종료, 오류 및 취소 시 스타일과 커서 표시를 가능한 한 복원합니다. POSIX／사용자 호스트는 기본 스타일과 표시 상태로, Windows VM 및 세 AOT는 기존 네이티브 상태로 복원합니다. 임베디드 호스트는 기본적으로 제어를 제공하지 않습니다. C++、C ABI、C#、Python 조회／제어 콜백과 줄바꿈 없는 텍스트 콜백을 제공할 수 있으며 작업 스레드 동기화는 호스트 책임입니다. 모바일／브라우저는 일반 텍스트를 유지합니다. 예제: `console_demo.vs`.

```VeroScript
if (std::console::supports_color())
{
	std::console::set_color("bright_green", "default");
}
std::console::write("Ready\n");
std::console::flush();
std::console::reset_style();
```

## 전환수

| 기능 | 설명 |
| --- | --- |
| `to_int(value)` | `int`로 변환됩니다. 부동 소수점 값은 0쪽으로 잘립니다. 범위를 벗어나거나 유한하지 않은 값은 오류입니다. |
| `to_double(value)` | `double`으로 변환합니다. |
| `to_float(value)` | `float`로 변환합니다. 결과는 Binary32로 저장됩니다. |
| `to_unsigned(value)` | 음수, 범위를 벗어난 값 및 잘못된 값을 거부하여 `unsigned int`로 변환합니다. 부동 소수점 값은 0을 향해 잘립니다. |
| `to_int64(value)` | 범위 확인을 통해 `int64`로 변환합니다. |
| `to_uint64(value)` | 범위 확인을 통해 `uint64`로 변환합니다. |
| `to_string(value)` | 값을 문자열로 변환합니다. |
| `to_wstring(value)` | `string` 또는 `wstring` 값을 와이드 문자열로 명시적으로 변환합니다. |

```VeroScript
int main()
{
	int count = to_int(12.75);
	double ratio = to_double(3);
	unsigned int flags = to_unsigned(true);
	string text = to_string(count);
	wstring wide = to_wstring(text);
	print("count={}, ratio={}, flags={}", count, ratio, flags);
	print(wide);
	return 0;
}
```

변환 함수는 임의의 텍스트를 숫자로 구문 분석하지 않습니다. 숫자 입력에는 `std::io::read_int` 또는 `std::io::read_double`을 사용하십시오. 파일 또는 프로토콜 텍스트의 경우 스크립트에서 구문 분석 규칙을 명시적으로 정의합니다.

## 숫자 및 컬렉션 도우미

| 기능 | 설명 |
| --- | --- |
| `abs(value)` | 숫자 값의 절대값을 반환합니다. |
| `min(a, b)` | 두 숫자 값 중 더 작은 값을 반환합니다. |
| `max(a, b)` | 두 숫자 값 중 더 큰 값을 반환합니다. |
| `len(value)` | 문자열, 배열 또는 표준 컨테이너의 요소 수를 반환합니다. 다중 집합의 경우 총 발생 횟수를 반환합니다. |
| `range(int64 count)` | `int64` 개수를 받아 `0`부터 `count - 1`까지 담은 `array<int>`를 반환합니다. 음수 개수는 빈 배열을 만들고 `1000000`을 초과하면 런타임 오류입니다. |

```VeroScript
int main()
{
	array<int> values = range(4);
	print("size={}", len(values));
	print("low={}, high={}, distance={}", min(-2, 5), max(-2, 5), abs(-9));
	return 0;
}
```

`len(value)`는 음수가 아닌 `int64`(`0`부터 `9223372036854775807`)를 반환합니다. null, 지원하지 않는 유형 및 표현할 수 없는 길이는 런타임 오류입니다. 기존 컨테이너의 크기, 용량 및 요소 수 조회도 `int64`를 반환합니다. 길이와 선형 인덱스에는 `int64`를 사용하세요. `int`에 대입하면 일반적인 범위 검사 숫자 변환을 수행하며 값이 범위에 맞지 않으면 실패합니다. 더 넓은 타입도 네이티브 할당 또는 백엔드 컨테이너 제한을 없애지는 않습니다.

## 환경, 시간, 제어 흐름

| 기능 | 설명 |
| --- | --- |
| `std::environment::getenv(name)` | 호스트 프로세스 환경 변수를 읽습니다. 누락된 변수는 빈 문자열을 반환합니다. 이름은 비워둘 수 없습니다. |
| `std::environment::setenv(name, value)` | setenv(name, "")는 존재하는 빈 값을 설정합니다. 삭제는 unsetenv이며 없는 변수 삭제도 성공합니다. try_getenv는 없음과 빈 값을 구분합니다. 이름은 비어 있거나 =/NUL을 포함할 수 없고 값은 NUL을 포함할 수 없습니다. 이름 대소문자는 Windows/POSIX 규칙을 따릅니다. |
| `std::chrono::now()` | 시스템 시계에 따라 Unix epoch 이후의 초 수를 `double`로 반환합니다. |
| `std::this_thread::sleep(milliseconds)` | 지정된 밀리초 동안 현재 스크립트 스레드를 일시 중단합니다. 인수는 음수가 아니어야 합니다. |
| `assert(condition[, message])` | `bool` 조건과 선택적 `string` 메시지를 받습니다. 조건이 `false`이면 런타임 오류를 보고하고 `true`이면 출력하지 않습니다. |
| `exit(code)` | 지정된 정수 종료 코드로 스크립트 종료를 요청합니다. |

```VeroScript
int main()
{
	std::environment::setenv("VERO_MODE", "docs");
	print(std::environment::getenv("VERO_MODE"));

	double started = std::chrono::steady_now();
	std::this_thread::sleep(1);
	assert(std::chrono::steady_now() >= started);
	exit(0);
	return 0;
}
```

`assert`는 인수를 1개 또는 2개 받습니다. 첫 번째 인수는 `bool`이어야 하며, 두 번째 인수를 지정하는 경우 `string`이어야 합니다. 실패할 때 비어 있지 않은 메시지는 진단에 포함되고, 빈 메시지는 기본 어설션 진단을 사용합니다. 실패는 스크립트 예외가 아니라 런타임 오류이므로 `catch (...)` 또는 형식이 지정된 처리기가 잡을 수 없습니다.

환경 변수는 호스트 프로세스에 속합니다. 변경 사항이 이후 호스트 로직에 영향을 미치는지 여부는 호스트 플랫폼 및 임베딩 모드에 따라 다릅니다.

`std::chrono::steady_now()`는 단조 시계 값을 초 단위로 반환합니다. 경과 시간 측정에는 두
읽기의 차이를 사용하고 벽시각으로 사용하지 마십시오.

## 표준 입력

| 기능 | 설명 |
| --- | --- |
| `std::io::read_token()` | 표준 입력에서 공백으로 구분된 다음 토큰을 읽습니다. |
| `std::io::read_string()` | 다음 문자열 토큰을 읽습니다. |
| `std::io::read_int()` | 다음 정수 토큰을 읽고 구문 분석합니다. |
| `std::io::read_double()` | 다음 부동 소수점 토큰을 읽고 구문 분석합니다. |
| `std::io::read_bool()` | `true` 또는 `false`을 읽고 구문 분석합니다. |

입력이 끝났거나 잘못된 토큰으로 인해 진단이 생성됩니다. 이러한 함수는 간단한 명령줄 입력을 위한 것입니다. 더 복잡한 입력 프로토콜은 호스트 또는 파일 지향 표준 라이브러리 API에 속합니다.

## 구현 전용 이름

`__`로 시작하는 이름(예: `__read_file`, `__format`)은 컴파일러, 런타임 및 표준 라이브러리 구현 전용입니다. 일반 스크립트 모듈에서는 호출할 수 없으므로 [표준 라이브러리 참조](index.md)에 문서화된 공개 함수를 사용하세요.

- [표준 라이브러리 기반](foundation.md)

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/builtins.md) · [fr-FR](../../fr-FR/standard-library/builtins.md) · [ja-JP](../../ja-JP/standard-library/builtins.md) · [ko-KR](builtins.md) · [zh-CN](../../zh-CN/standard-library/builtins.md) · [zh-TW](../../zh-TW/standard-library/builtins.md)
