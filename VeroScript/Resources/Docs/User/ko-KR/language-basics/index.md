# 언어 기초

이 섹션에서는 VeroScript의 기본 언어 규칙을 주제별로 설명합니다. 각 페이지는 상대적으로 독립적인 하나의 주제를 다루고 작은 예를 사용하여 구문과 동작을 정의합니다. 전체 API 및 런타임 메커니즘은 별도로 문서화됩니다.

## 소스, 모듈 및 범위

- [기본 구문](basic-syntax.md): 소스 파일, 명령문, 블록, 식별자 및 키워드.
- [설명](comments.md): 단일 행 및 블록 주석입니다.
- [범위](scope.md): 모듈, 네임스페이스, 함수 및 블록 범위.
- [네임스페이스](namespaces.md): 네임스페이스 선언, 정규화된 이름 및 `using namespace`.
- [`std` 네임스페이스](standard-namespace.md): 내장 유형, 런타임 함수 및 호환성 별칭의 전체 목록입니다.
- [모듈](modules.md): 모듈 가져오기, 내보내기, 다시 내보내기 및 라이브러리 패키지 가져오기.
- [전처리기](preprocessor.md): 매크로, 조건부 컴파일, 문자열화, 토큰 붙여넣기 및 모듈 포함.

## 값 및 유형

- [리터럴](literals.md): 정수, 부동 소수점 값, 문자열, 와이드 문자열, 부울 및 null 값.
- [데이터 유형](data-types.md): 기본 제공 유형, 고정 너비 정수, 컨테이너 및 유형 유추.
- [값 유틸리티 유형](value-utility-types.md): optional, weak, 공유 구조체 핸들 및 맵 항목 pair.
- [변수](variables.md): 선언, 초기화, 할당 및 유추된 변수.
- [상수](constants.md): `const` 변수 및 상수 멤버입니다.
- [선언](declarations.md): `using`, `typedef`, `= default` 및 `= delete`.
- [구조체](structs.md): 값 유형, 필드, 메서드, 생성 및 구조체 복사 의미 체계.
- [클래스](classes.md): 공유 ID, 멤버 액세스, 생성, 상속 및 가상 메서드.
- [인터페이스](interfaces.md): 메서드 계약, 기본 구현, 인터페이스 상속 및 변환.
- [Enums](enums.md): 열거자, 범위 지정, 기본 정수 유형 및 열거형 값.
- [배열](arrays.md): 동적 배열, 고정 크기 기본 배열, 인덱싱 및 범위 반복.
- [컨테이너](containers.md): 맵, 세트, 큐, 스택, 데크 및 채널.
- [문자열](strings.md): `string`, `wstring`, 문자열 작업 및 표준 라이브러리 도우미.
- [참조](references.md): 참조 매개변수, 읽기 전용 참조 및 참조 범위 반복.
- [리플렉션](reflection.md): `type<T>`, 필드 및 메서드 정보, 열거형 정보, 선언 메타데이터.
- [정적 멤버](static-members.md): 정적 유틸리티 형식, 형식 수준 필드, 정적 메서드 및 제네릭 형식의 독립 저장소.

## 표현식 및 연산자

- [연산자](operators.md): 산술, 비교, 논리, 비트 단위, 할당 및 연산자 오버로드.

## 제네릭, 함수 및 제어 흐름

- [제네릭](generics.md): 제네릭 유형, 제네릭 함수, 기본 유형 인수 및 `where` 제약 조건.
- [함수](functions.md): 선언, 매개변수, 반환 값, 기본값 및 오버로드.
- [람다 및 함수 값](lambdas.md): 함수 유형, 람다 식, 캡처 및 참조 매개변수.
- [제어 흐름](control-flow.md): 조건, 루프, `switch`, 점프 및 반환.

[애너테이션과 소스 메타데이터](annotations.md)

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/index.md) · [fr-FR](../../fr-FR/language-basics/index.md) · [ja-JP](../../ja-JP/language-basics/index.md) · [ko-KR](index.md) · [zh-CN](../../zh-CN/language-basics/index.md) · [zh-TW](../../zh-TW/language-basics/index.md)
