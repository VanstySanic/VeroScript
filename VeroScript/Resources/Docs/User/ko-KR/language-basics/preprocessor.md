# 전처리기

전처리기는 어휘 및 의미 분석 전에 실행됩니다. 조건부 컴파일, 객체형 매크로, 함수형 매크로, 문자열화, 토큰 붙여넣기 및 모듈 포함을 지원합니다.

## 객체와 유사한 매크로

객체형 매크로는 이름을 미리 정의된 토큰 시퀀스로 바꿉니다.

```VeroScript
#define VERSION 2
#define MAX_COUNT 64

int version = VERSION;
int capacity = MAX_COUNT;
```

컴파일러가 유형 검사를 수행하기 전에 매크로 확장이 발생합니다. 매크로에는 자체 VeroScript 정적 유형이 없습니다. 확장된 표현식은 여전히 일반 구문 및 유형 규칙을 충족해야 합니다.

## 함수형 매크로

함수형 매크로는 인수를 허용합니다. 확장으로 인해 우선 순위가 변경되지 않도록 복합 표현식의 인수를 괄호로 묶습니다.

```VeroScript
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int value = MAX(3, 8);
```

간단한 토큰 대체를 위해 매크로를 사용하세요. 유형 검사, 오버로드 또는 디버깅 동작이 중요한 경우 함수나 일반 함수를 선호하세요.

## 문자열화 및 토큰 붙여넣기

함수형 매크로 내에서 `#`은 인수를 텍스트로 변환하고 `##`은 인접한 토큰을 하나의 토큰에 붙여넣습니다.

```VeroScript
#define STR(x) #x
#define CAT(a, b) a##b

int VALUE = 9;
string name = STR(hello);
int result = CAT(VAL, UE);
```

매크로 확장 중에 문자열화 및 토큰 붙여넣기가 발생합니다. 붙여넣은 결과는 여전히 유효한 VeroScript 토큰이어야 합니다.

## 가변 매크로

함수형 매크로는 `...`을 사용하여 추가 인수를 허용하고 `__VA_ARGS__`을 통해 확장할 수 있습니다.

```VeroScript
#define LOG(fmt, ...) print(fmt, ##__VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

현재 구현에서는 가변 인수가 제공되지 않을 때 추가 쉼표를 제거하기 위해 `##__VA_ARGS__`을 지원합니다. 또한 `__VA_OPT__(tokens)`도 지원합니다. 가변 인수가 비어 있지 않으면 괄호 안의 토큰이 내보내지고 그렇지 않으면 생략됩니다.

```VeroScript
#define LOG(fmt, ...) print(fmt __VA_OPT__(,) __VA_ARGS__)

LOG("ready");
LOG("value={}", 42);
```

`__VA_OPT__`은 가변 매크로의 대체 목록에서만 유효하며 균형 잡힌 괄호 쌍이 뒤에 와야 합니다.

## 동적 내장 매크로

컴파일러는 현재 소스 모듈 컨텍스트에서 생성된 두 개의 매크로도 제공합니다.

| 매크로 | 확장 |
| --- | --- |
| `__FILE__` | 현재 모듈의 정규화된 논리적 경로입니다. 경로가 없는 소스 텍스트의 경우 `<memory>`입니다. |
| `__LINE__` | `1`에서 시작하는 매크로 호출을 포함하는 물리적 소스 라인입니다. |

일반적인 전역 매크로가 아닌 각 모듈의 전처리 컨텍스트에서 계산됩니다.

```VeroScript
#define REPORT(name) print("{}:{} {}", __FILE__, __LINE__, name)

int main()
{
	REPORT("ready");
	return 0;
}
```

`__FILE__`, `__LINE__`, `__VA_ARGS__` 및 `__VA_OPT__`은 예약된 이름입니다. 스크립트, 명령줄 초기 정의, 호스트 사전 정의 매크로 또는 함수-매크로 매개변수는 이를 재정의할 수 없습니다.

## 조건부 컴파일

`#if`, `#ifdef`, `#ifndef`, `#elif`, `#else` 및 `#endif`를 사용하여 이후 컴파일에 참여할 소스를 선택합니다.

```VeroScript
#define VERSION 2

#if VERSION >= 2
int feature_level = 2;
#else
int feature_level = 1;
#endif
```

소스에서 지원하는 전체 지시문은 `#include`, `#define`, `#define_export`, `#undef`, `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`, `#error`입니다. 조건에서는 `defined(NAME)`과 `defined NAME` 두 형식으로 매크로 존재 여부를 검사합니다. 매크로를 먼저 확장하고, 정의되지 않은 채 남은 식별자는 0으로 평가합니다.

`#if` 표현식은 64비트 정수 상수(10진수, C 형식의 앞자리 0을 사용한 8진수, 16진수), 괄호, 단항 `+ - ! ~`, 산술 `* / % + -`, 시프트, 비교, 동등, 비트 `& ^ |`, 단락 `&& ||`, `?:`를 지원합니다. 스크립트 리터럴의 이진 접두사나 숫자 구분자는 사용하지 않습니다. 실제로 평가되는 분기의 0 나누기와 오버플로는 오류이며, 단락된 분기는 평가하지 않습니다.

활성 분기의 `#error message`는 전처리를 중단하고 앞뒤 공백을 제거한 메시지를 진단에 보존합니다. 비활성 조건 분기에서는 무시합니다. 메시지는 매크로 확장하지 않습니다. `#warning`, `#line`, `#pragma`(`#pragma once` 포함)는 지원하지 않으며 해당 지시문 이름으로 보고됩니다.

명령줄 도구는 `-D` 또는 `--define`으로 단순 객체형 매크로를 제공할 수 있습니다. 소스 파일에서는 앞서 설명한 함수형 및 가변 인수 매크로도 정의할 수 있습니다.

```powershell
veroscript -D ENABLE_LOGGING=1 script.vs
```

## 모듈 포함

`#include`는 `export import`의 별칭으로 공개 선언과 공개 매크로를 가져와 다시 내보내며, 텍스트 삽입을 수행하지 않습니다.

```VeroScript
#include "math.vs"

int main()
{
	print(std::clamp_int(15, 0, 10));
}
```

모듈 종속성을 표현하고 다시 내보낼 때 `import` 및 `export import`을 사용합니다. `#include`를 C++ 스타일 텍스트 포함 메커니즘으로 취급하지 마십시오.

## 공개 매크로

<a id="exported-macro-contract"></a>

`#define_export`는 `#define`과 같은 객체형, 함수형 및 가변 인수 문법을 사용합니다. 일반 정의는 비공개입니다. 명시적 `import`는 경로 문자열 직후부터 대상의 최종 공개 매크로를 제공하며 `export import`는 다시 내보냅니다. `#include`는 공개 선언과 공개 매크로를 포함하는 `export import`의 별칭입니다. `.vslib` 라이브러리에도 적용됩니다. 호스트의 암시적 가져오기는 소비자 소스의 시작부터 확정된 공개 매크로도 제공하며 다시 내보내지는 않습니다. 제공 모듈과 명시적 의존성을 먼저 처리하고, 제공 모듈 사이의 매크로 의존성은 일반 `import`로 표시합니다. 비공개 매크로는 유지되며 충돌은 일반 가져오기 규칙을 따릅니다. 예를 들어 `veroscript --implicit-import settings.vs main.vs`를 사용하면 `main.vs`에 가져오기를 쓰지 않아도 공개 매크로를 사용할 수 있습니다.

```VeroScript
// settings.vs
#define PRIVATE_VALUE 7
#define_export VALUE 21
#define_export DOUBLE(x) ((x) * 2)

```

```VeroScript
// main.vs
import "settings.vs";
int main()
{
	print(DOUBLE(VALUE));
	return 0;
}
```

치환 본문은 호출자의 현재 매크로 환경에서 확장되며 비공개 보조 매크로를 캡처하지 않습니다. 필요한 보조 매크로도 명시적으로 내보내세요. `__FILE__`과 `__LINE__`은 호출 위치를 나타냅니다. 이름이 같은 서로 다른 원본 정의는 본문이 같아도 충돌합니다. 가져온 정의를 바꾸기 전에 `#undef`를 사용하세요. 같은 원본 정의의 반복 가져오기는 멱등이며 `#undef` 이후에는 정의를 복구합니다. 일반 `#define`으로 재정의하면 비공개가 되며 `#undef`는 공개 인터페이스에서 제거합니다.

공개 매크로 정의가 없는 순환 의존성은 계속 지원합니다. 순환 소스의 의존성 폐쇄에 비활성 지시문을 포함해 `#define_export`가 있으면 공개 인터페이스를 결정할 수 없어 로드를 거부합니다. 소스 가져오기 순환을 제거하세요. 라이브러리의 조건부 내보내기는 빌드 시 확정되며 애플리케이션 패키지는 공개 매크로 인터페이스를 보관하지 않습니다.

<a id="preprocess-resource-limits"></a>

## 전처리 리소스 제한

기본적으로 중간 치환 텍스트를 포함한 파일별 확장 출력은 32 MiB로 제한됩니다. 한 컴파일의 모든 파일은 268,435,456 내부 단위의 작업 예산을 공유합니다. 한도를 넘으면 전처리 오류가 발생하고 컴파일이 거부됩니다. 이 단위는 VM 명령 수나 실행 시간 보장이 아닙니다. 네이티브 호스트는 `FPreprocessContext::Budget`에 `FPreprocessBudget`을 제공할 수 있습니다. 0은 한도 0이며 무제한이 아닙니다. 이 한도는 컴파일 확장의 실행 예산과 별개입니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/preprocessor.md) · [fr-FR](../../fr-FR/language-basics/preprocessor.md) · [ja-JP](../../ja-JP/language-basics/preprocessor.md) · [ko-KR](preprocessor.md) · [zh-CN](../../zh-CN/language-basics/preprocessor.md) · [zh-TW](../../zh-TW/language-basics/preprocessor.md)
