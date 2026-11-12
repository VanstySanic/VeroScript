# C++ 코딩 규칙

적용 범위: [VeroScript 프로젝트 개발 및 기여](README.md).

이 파일은 현행 C++ 규칙의 권위 있는 기준입니다. **권장** 또는 **필요에 따라**라고 명시하지 않은 형식·구조 규칙은 필수입니다.

## 1. 들여쓰기, 범위와 중괄호

- 논리 계층마다 Tab 하나를 사용합니다. 네임스페이스, 타입, 메서드, 조건, 루프, lambda 및 지역 범위는 각각 한 단계 깊어집니다.
- 여는 중괄호는 독립된 줄에서 해당 선언 또는 제어문에 맞춥니다.

  ```cpp
  void Process(const FRequest& Request)
  {
  	if (Request.bEnabled)
  	{
  		Execute(Request);
  	}
  }
  ```

- 여는 중괄호 직후나 닫는 중괄호 직전에 빈 줄을 강제하지 않습니다. 빈 줄은 서로 다른 논리 구역만 구분합니다.
- 닫는 중괄호는 여는 중괄호에 맞춥니다. `else`, `else if`, `catch`, `do` 끝의 `while`은 새 줄에 두고 해당 닫는 중괄호에 맞춥니다. 절의 여는 중괄호도 독립된 줄에 둡니다.

  ```cpp
  if (bPrimary)
  {
  	HandlePrimary();
  }
  else
  {
  	HandleFallback();
  }
  ```

- 본문이 있는 모든 제어 분기는 중괄호와 독립 범위를 사용합니다. `if`, `else`, `for`, `while`, `do`, 범위 `for`, 각 `switch case`를 포함합니다.
- 중괄호를 생략할 수 있는 유일한 실행 분기는 **본문이 직접적인 `return` 하나뿐인 한 줄 가드**입니다.

  ```cpp
  if (!bInitialized) return;
  ```

예외는 `return;` 또는 `return <표현식>;`에만 적용됩니다. `else` 연결, 정리, 로그, 대입, 매크로 조건 또는 중첩 논리와 결합하지 않습니다. `break`, `continue`, `throw`, `co_return` 및 여러 문장을 포함하는 분기는 예외가 아니며 중괄호 범위가 필요합니다.

## 2. 타입 전체 배치

다음 순서로 배치하고 내용이 없는 빈 구역은 작성하지 않습니다.

1. 필요에 따라 타입 머리에 `friend`, `using`, `typedef` 같은 관계·별칭;
2. `DECLARE_XXX` 같은 필요한 매크로;
3. `public`, `protected`, `private` 고정 순서의 접근 영역;
4. 각 영역의 상속 `override` 메서드 먼저;
5. 내부 타입;
6. 타입 자체 메서드;
7. 멤버 변수 마지막.

권장 골격:

```cpp
class FExample : public IBase, protected ISecondary
{
	friend class FExampleFactory;
	using FBase = IBase;

	DECLARE_EXAMPLE_TYPE(FExample)

public:
	//~ Begin IBase Interface
	virtual void Initialize() override;
	virtual void Shutdown() override;
	//~ End IBase Interface

	enum class EState
	{
		Idle,
		Running,
	};

	struct FOptions
	{
		bool bEnabled = false;
	};

	void Start(const FOptions& Options);

protected:
	//~ Begin ISecondary Interface
	virtual bool CanStart() const override;
	//~ End ISecondary Interface

	void NotifyStarted();

private:
	void ResetState();

	EState State = EState::Idle;
	FOptions Options;
};
```

### 2.1 접근 영역

- `public`, `protected`, `private` 순서를 반드시 따릅니다.
- 같은 접근 수준은 가능하면 연속 영역 하나에 둡니다. 언어, 매크로 확장 또는 명확한 의미 경계가 필요할 때만 지정자를 반복하며 전체 순서는 유지합니다.
- 멤버 변수는 실제 접근 영역 끝에 두고 메서드나 내부 타입 사이에 섞지 않습니다.

### 2.2 상속 메서드

- 각 접근 영역의 `override`는 타입 자체의 새 메서드보다 앞에 둡니다.
- 여러 기반 타입은 상속 목록의 왼쪽부터 오른쪽 순서로 그룹화하며 같은 기반 타입에서는 인터페이스 선언 순서를 따릅니다.
- 상속 메서드가 여러 개이거나 기반 타입이 불명확하거나 자체 메서드와 인접해 혼동될 때 쌍을 이루는 표식을 사용합니다.

  ```cpp
  //~ Begin ISerializable Interface
  virtual bool Serialize(FArchive& Archive) override;
  virtual bool Deserialize(FArchive& Archive) override;
  //~ End ISerializable Interface
  ```

- `Begin`과 `End`는 직접 기반 타입 또는 인터페이스 이름을 정확히 맞춘 쌍이어야 합니다. 명백한 `override` 하나에는 필요에 따라 생략하며 모든 함수에 기계적으로 붙이지 않습니다.

### 2.3 내부 타입

같은 접근 영역의 고정 우선순위:

1. `enum`, `enum class`;
2. `struct`;
3. `union`, 지역 별칭, 함수 객체 타입 등 기타 선언;
4. `class`.

- 내부 타입 그룹 사이에 빈 줄 하나를 둡니다. 같은 종류는 의존성과 이름에 따라 읽기 좋은 순서로 둡니다.
- 타입 머리에 둘 일반 `using`/`typedef`를 이유 없이 내부 타입 영역에 남기지 않습니다. 앞선 내부 타입에 의존하는 별칭만 기타 선언 위치에 둡니다.
- 완전한 정의 의존성이 순서와 충돌하면 전방 선언을 우선합니다. 불가피한 예외는 가까이에 완전한 타입 의존 이유를 설명합니다.

## 3. 헤더, 전방 선언과 구현 파일

### 3.1 헤더와 필요에 따른 구현 파일

- 자체 C++ `.h` 머리는 저작권과 라이선스를 나타내는 두 줄 SPDX 문구, 빈 줄, `#pragma once`, 빈 줄, include 또는 선언 순서입니다.

  ```cpp
  // SPDX-FileCopyrightText: <copyright-holder>
  // SPDX-License-Identifier: MIT

  #pragma once

  #include <cstdint>
  ```

- `.inl`은 포함되는 구현 조각이며 `.h`와 다릅니다. 저작권과 라이선스를 나타내는 두 줄 SPDX 문구와 빈 줄로 시작하되 기본적으로 `#pragma once`나 대응 `.cpp`가 필요하지 않습니다. 번역 단위마다 한 번만 포함할 헤더형 내용임을 확인한 경우만 `.h`처럼 `#pragma once`를 사용합니다.
- `.h`에 같은 파일명 stem의 `.cpp`를 필수로 요구하지 않습니다. 구현이나 빌드 책임상 별도 번역 단위가 필요할 때만 책임에 따라 `.cpp`를 구성하며 파일 수나 디렉터리 대칭을 위해 빈 구현 파일을 만들지 않습니다.
- 선언, 타입 정의, 상수, 템플릿 또는 `inline` 정의만 있는 헤더는 단독으로 존재할 수 있습니다. 헤더에서 보여야 하는 정의를 파일 분리를 위해 옮기지 않습니다.
- `.cpp`가 필요하면 같은 모듈의 구현 영역에 배치하고 구현하는 헤더를 명시적으로 포함합니다. `Public/`·`Private/` 구분이 없으면 보통 같은 디렉터리에 두고 구분이 있으면 `Private/`의 적절한 의미 하위 디렉터리를 사용하며 `Public/`와 동일한 상대 경로를 요구하지 않습니다.
- 구현 전용 `.cpp`는 같은 이름의 `.h`가 없어도 됩니다.
- 기존 빈 `.cpp`를 제거할 때는 실제 빌드, 등록, 명시적 인스턴스화 및 플랫폼 책임을 확인하고 빌드 참조도 함께 갱신합니다.
- 타사 및 생성 파일은 각각에 적용되는 파일 구성을 유지합니다.

### 3.2 선언과 정의 순서

- `.h`에 선언되어 `.cpp` 구현이 필요한 메서드는 헤더 선언 순서대로 정의합니다.
- 그룹도 유지합니다. 상속 인터페이스 구현과 타입 자체의 공개·보호·비공개 메서드 상대 순서를 바꾸지 않습니다.
- 익명 네임스페이스 보조 함수, 파일 지역 상수 및 구현 전용 타입은 정의 앞에 둘 수 있으나 헤더 선언에 대응하는 정의의 상대 순서는 바꾸지 않습니다.
- 선언 추가 시 `.cpp` 정의 위치를 확인하며 모든 구현을 일괄적으로 파일 끝에 추가하지 않습니다.

### 3.3 전방 선언과 여러 최상위 타입

- 모든 전방 선언은 `enum` → `struct` → 기타 선언 → `class` 순서입니다.
- `enum` 전방 선언에는 유효한 opaque-enum 형식을 사용합니다. `enum class EName;` 또는 기저 타입을 명시한 `enum EName : std::uint8_t;`입니다. 기저 타입 없는 비범위 `enum EName;`은 선언하지 않습니다.
- 한 헤더에서 여러 최상위 타입을 정의할 수 있습니다. 완전한 타입 의존성이 허용하면 `enum` → `struct` → 기타 선언 → `class` 순서를 따릅니다.
- 기타 선언은 `union`, `using`/`typedef` 별칭, 함수 타입 별칭을 포함합니다. 클래스 템플릿은 `class`, 구조 템플릿은 `struct`로 분류하며 모든 `template`을 기타로 묶지 않습니다.
- 완전한 타입 의존성 때문에 다른 순서가 필요하면 전방 선언을 먼저 시도합니다. 피할 수 없으면 의존성을 우선하고 가까이에 짧게 설명합니다.

### 3.4 멤버 기본값과 불리언 비트 필드

- 초기값이 필요하고 기본 상태를 선언 위치에서 직접 명확하게 표현할 수 있으면 그곳에 기본값을 줍니다.
- 생성자 인수, 실행 상태 또는 다른 멤버의 초기화 결과에 의존하는 값은 생성자 초기화 목록에 둡니다. 규칙을 맞추려고 오해를 부르는 고정값을 쓰지 않습니다.
- 주소, 참조, 원자 연산 또는 안정적인 외부 바이너리 배치가 필요하지 않은 압축 저장용 불리언 스위치는 1비트 필드를 우선합니다. 예는 `uint8 bEnabled : 1;` 또는 `std::uint8_t bEnabled : 1;`입니다. 너비는 `: 1`이고 `uint8`은 저장 타입입니다.
- 현재 최소 표준은 C++17입니다. C++17 비트 필드는 클래스 내 기본 멤버 초기화를 사용할 수 없습니다. 0이 아니거나 명시적 상태가 필요하면 생성자 초기화 목록에서 설정합니다. 지원하는 표준으로 변경하기 전에는 `uint8 bEnabled : 1 = true;`를 쓰지 않습니다.

## 4. `switch`와 실행 분기

- 모든 `switch`에 `default`가 필요합니다.
- `case`와 `default`는 `switch` 내용 수준에 맞추고 본문의 중괄호는 한 단계, 내부 문장은 다시 한 단계 들여씁니다.

```cpp
switch (Kind)
{
case EKind::First:
	{
		HandleFirst();
		break;
	}
case EKind::Second:
	{
		HandleSecond();
		break;
	}
default:
	{
		HandleUnknown();
		break;
	}
}
```

- 유일한 실행 경로가 `return` 또는 `return <표현식>`으로 메서드를 직접 끝내는 `case`에는 1절의 한 줄 가드 예외를 적용할 수 있습니다. 로그, 정리, 지역 선언, 조건 또는 다른 문장을 섞지 않습니다. 다른 모든 `case`와 `default`는 독립 중괄호 범위를 사용합니다.
- 업무 동작을 하지 않는 `default`도 중괄호 범위와 명시적인 `break`를 유지합니다. 아무것도 하지 않는 이유가 문맥상 명백하지 않을 때만 짧은 영어 주석을 추가합니다.
- 암묵적 fallthrough에 의존하지 않습니다. 의도적인 통과는 프로젝트가 허용하는 명시적 표식과 설명을 사용합니다.
- 1절의 엄격한 직접 `return` 가드 예외 이외에는 모든 실행 분기에 중괄호를 사용합니다.

## 5. 전처리 매크로

- `#if`, `#ifdef`, `#ifndef`, `#elif`, `#else`, `#endif`는 감싼 코드와 **같은 논리 계층**에 들여씁니다. 전처리 조건은 추가 계층을 만들지 않습니다.
- 조건을 제거해도 코드의 기존 계층과 가독성을 유지합니다.

  ```cpp
  void Initialize()
  {
  	#if WITH_DIAGNOSTICS
  	RegisterDiagnostics();
  	#endif

  	StartRuntime();
  }
  ```

- 조건 매크로는 가능한 한 완전한 의미 구역을 감싸며 단일 선언이나 표현식을 읽기 어려운 조각으로 나누지 않습니다.
- 짝을 이루는 매크로는 들여쓰기가 같아야 합니다. 긴 구역의 `#endif`에 필요에 따라 짧은 조건 설명을 추가합니다.
- 매크로가 생성하는 선언도 이 문서의 타입 배치 순서를 따릅니다.

## 6. 주석과 영역

- 한 줄 설명은 `//`로 선언 또는 구역 바로 위에 둡니다. 짧은 줄 끝 주석은 가독성을 방해하지 않을 때만 사용합니다.
- 여러 줄 API 의미, 소유권, 스레드, 실패 조건 또는 복잡한 불변 조건에는 `/** ... */` 문서 주석을 사용합니다.
- `GetName()` 앞의 `// Gets the name`처럼 명백한 이름을 반복하지 않습니다.
- 멤버 변수 설명은 단위, 소유권, 유효 기간, 스레드 제약, 캐시 무효화 조건 또는 다른 필드 관계를 우선합니다.
- `//~ Begin <Name> Interface` / `//~ End <Name> Interface`는 상속 인터페이스 구역에만 사용하며 일반 메서드 영역 제목으로 남용하지 않습니다.
- 다른 큰 구역은 짧은 설명을 사용할 수 있지만 고정 배치로 구조 자체가 설명되도록 하는 것을 우선합니다.

## 7. 기타 권장 사항

- `.cpp`와 `.h`의 include 순서나 그룹은 현재 규정하지 않습니다. 해당 파일의 가독성과 빌드 제약을 따릅니다. 전방 선언 배치는 여전히 3.3절을 따릅니다.
- null 포인터에는 `nullptr`를 사용하고 정보 손실 가능성이 있는 변환은 명시합니다.
- 자원 소유권, null 가능 객체 및 스레드 간 콜백은 타입 또는 메서드 주석에서 명확하게 설명합니다.
- 빈 함수, 기본 구현, 의도적으로 무시한 매개변수는 가장 짧고 읽기 쉽게 표현합니다. 형식만 맞추려고 의미 없는 지역 변수나 빈 범위를 만들지 않습니다.

## 8. 유지 관리자가 보완할 주제

이름 세부 사항, 템플릿·concept, 예외 정책, 스마트 포인터 정책, 테스트 프레임워크, 형식 도구와 줄 너비는 아직 필수 규칙이 아닙니다. 유지 관리자가 결정한 뒤 보완합니다. include 순서는 현재 명시적으로 규정하지 않습니다.
