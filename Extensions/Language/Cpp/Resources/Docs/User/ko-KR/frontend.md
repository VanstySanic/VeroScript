# C++ 프런트엔드

## 프로젝트 병합과 표현 경계

여러 파일의 병합은 linker가 아닙니다. 입력 순서로 정의를 선택하거나 internal
linkage 엔티티의 이름을 바꾸거나 임의의 ownership 및 storage 규칙을 해결하지
않습니다. `std::shared_ptr<T>`는 `T`로 지워지지 않으며 shared ownership에는
명시적인 adapter가 필요합니다. `std::size_t`와 `std::ptrdiff_t`는 대상 ABI에
따라 처리되고 pointer와 pointee의 cv qualification은 overload identity의
일부입니다.

`VeroScriptCppFrontend`는 제한된 C++17 부분집합을 안정적인 공개 AST로 파싱합니다.
[BindGen](bindgen.md)과 [Transpiler](transpiler.md)가 공유하며, 지원하지 않는 구성은
진단을 내고 자동으로 근사하지 않습니다.

공개 API는 `ParseSource`, `ParseFile`, `ParseFiles`, `MergeTranslationUnits`,
`MapTypeToVeroScript`, `ResolveTypeAliases`, `EvaluateEnumValues`,
`TryGetEnumValueRange`,
`GetFunctionIdentity`, `FormatDiagnostic`, `JoinTokens`입니다.
AST는 소스 위치, namespace, 접근성, 정규화된 이름, 생성자, 필드, 함수, enum, 기반 타입,
참조, 포인터 cv 수식 및 `constexpr`, `consteval`, `constinit`, `noexcept`를 보존합니다.

`FParseOptions`는 include, `#pragma`, 알 수 없는 전처리 지시문 거부와 함수 본문 파싱을 제어합니다. `FTypeMapOptions`는 알려진 호스트 형식의 원시 포인터, rvalue 참조, 알려진 호스트·열거형·형식 매개변수 및 include를 무시할 때 필요한 별칭에 대한 명시적 정책을 지정합니다. 정책을 의도적으로 설정하세요. 관대한 파싱이 성공해도 자동 바인딩이나 변환이 가능하다는 보장은 없습니다.

namespace, `using Name = Type`, `typedef Type Name`, 끝의 기본 형식 매개변수를 포함하는
형식 매개변수 class/function/alias template, scoped/unscoped enum, class/struct, 접근 지정자, 필드,
생성자, 메서드, free function, 전역, 고정 폭 정수, 표준 문자열과 대응되는 표준 컨테이너를
지원합니다. 이름 있는 전방 선언은 정의와 병합됩니다. `#pragma once`, include, 일반적인
header guard 및 `extern "C"` 선언은 허용되지만 일반 조건부 전처리와 알 수 없는 directive는
거부됩니다. 일반 raw string과 `u8` raw string은 전체 구분자를 검사한 뒤 UTF-8 리터럴로
정규화하며 wide raw string은 거부됩니다. 같은 형식의 단순 declarator 목록은 확장하지만
pointer/reference가 섞인 목록은 지원하지 않습니다. 클래스 외부의 생성자, 소멸자, 메서드,
연산자와 정적 데이터 정의는 선언과 매칭할 수 있으며 다른 입력 파일에 둘 수 있습니다.
C++17 attribute는 타입, enum, enumerator, 필드, 함수와 매개변수에 구조적으로 보존됩니다.
enum 초기화자는 대상 ABI의 정수 리터럴 너비와 부호를 보존하고 C++ 정수 승격을
적용합니다. 부호 없는 연산의 순환은 허용하며 부호 있는 오버플로와 잘못된 시프트
횟수는 거부합니다. 최종 값은 공개 `int64` 표현과 매핑된 기반 타입 범위에 들어가야
합니다. 음수인 부호 있는 값의 시프트와 단락 `&&`/`||` 표현식은 지원하지 않습니다.

기반 타입을 명시하지 않은 unscoped enum에서는 이전 멤버 참조에 초기화식의 타입을
유지하며 고정 기반 타입의 enum은 해당 타입의 정수 승격을 사용합니다. 이 차이에
의존하는 enum을 쓰는 MSVC 호스트는 `/Zc:enumTypes`(VS 2022 17.4 이상)를 켜거나
기반 타입을 명시해야 합니다. 이 옵션은 기본적으로 꺼져 있고 `/permissive-`도 켜지
않습니다. SDK는 호스트 컴파일러 옵션을 변경하지 않습니다.

여러 파일의 병합은 호환되는 외부 선언을 합치고 중복 정의, 중복 입력 경로, internal
linkage 충돌 및 anonymous namespace를 거부합니다. 비형식 매개변수, parameter pack,
specialization, SFINAE, concept, 종속 compile-time 표현식, union, C 배열, 알 수 없는
directive와 표현할 수 없는 포인터는 거부합니다. friend 타입/함수/멤버 선언은 record-level
선언으로 보존하지만 inline friend 정의는 거부합니다.

타입 검색은 선언 범위를 우선하며 상대 한정 이름과 `::` 절대 한정 이름을 지원합니다. 선언된 사용자 타입은 `string`, `vector` 같은 편의 매핑보다 우선합니다. 해석할 수 없는 한정 타입 이름은 오류를 발생시킵니다.

클래스, 열거형 및 바인딩된 템플릿 매개변수는 바깥쪽 범위의 별칭보다 우선합니다.
별칭을 직접 해석할 때는 `ResolveTypeAliases`의 세 인수 오버로드에
`KnownHostTypes`, `KnownEnumTypes`, `KnownTypeParameters` 조회 옵션을 전달하세요.

빈 `FType::DeclarationScope`는 전역 범위를 뜻하며 별칭을 치환할 때도 유지됩니다.
`FBaseType::Type`은 기반 타입의 조회 범위와 템플릿 인수를 구조적으로 보존합니다.
`bQualifyNamedTypes`는 소스 생성에 사용할 절대 한정 타입 이름을 요청합니다.
기본값은 카탈로그의 정규 이름을 유지합니다.

<!-- docs-navigation -->

---

[사용자 문서](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../en-US/frontend.md) · [fr-FR](../fr-FR/frontend.md) · [ja-JP](../ja-JP/frontend.md) · [ko-KR](frontend.md) · [zh-CN](../zh-CN/frontend.md) · [zh-TW](../zh-TW/frontend.md)
