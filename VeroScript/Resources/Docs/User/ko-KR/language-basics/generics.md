# 제네릭

제네릭을 사용하면 유형이나 함수가 선언에 유형 매개변수를 사용하고 사용 위치에서 구체적인 유형을 대체할 수 있습니다. VeroScript는 `where` 유형 제약 조건과 함께 일반 구조체, 클래스, 인터페이스 및 함수를 지원합니다.

## 일반 유형

유형 이름 뒤의 꺾쇠 괄호 안에 유형 매개변수를 선언하십시오.

```VeroScript
class Duo<T, U = int>
{
public:
	T first;
	U second;

	Duo(T first_value, U second_value)
	{
		first = first_value;
		second = second_value;
	}
};

Duo<string> text_pair = Duo<string>("score", 10);
Duo<int, string> number_pair = Duo<int, string>(10, "points");
```

후행 유형 매개변수는 기본 유형을 가질 수 있으며 기본값은 이전 유형 매개변수를 참조할 수 있습니다. 명시적 유형 인수는 접두사를 형성합니다. 이후의 인수는 컨텍스트 또는 호출 인수에서 추론될 수 있습니다.

## 일반 함수

함수 이름 뒤에 함수 유형 매개변수를 선언합니다.

```VeroScript
T identity<T>(T value)
{
	return value;
}

string text = identity<string>("VeroScript");
int number = identity(42);
```

인수가 충분한 정보를 제공하는 경우 형식 인수를 생략할 수 있습니다. 특정 유형을 선택해야 하는 경우 이를 명시적으로 제공하십시오.

`template<T>`은 읽을 수 있는 일반 선언의 또 다른 철자입니다. 유형 또는 함수 이름 뒤에 `<T>`이 오는 것과 동일한 일반 규칙을 따릅니다.

```VeroScript
template<T>
T identity_again(T value)
{
	return value;
}
```

## 유형 제약

유형 매개변수에서 인터페이스 계약을 요구하려면 `where`을 사용하십시오.

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

T read_once<T>(T value) where T : IReadable
{
	return value;
}
```

유형 매개변수에는 여러 인터페이스 제약조건이 있을 수 있습니다.

```VeroScript
T describe<T>(T value) where T : IReadable, IName
{
	return value;
}
```

제약 조건은 컴파일 타임에 확인됩니다. 제네릭 함수를 호출하거나 제네릭 유형을 인스턴스화하는 데 사용되는 구체적인 유형은 선언된 모든 제약 조건을 충족해야 합니다.

인터페이스 형식 별칭이나 별칭 체인도 제약 조건으로 사용할 수 있습니다. `using Readable = IReadable;` 선언 후 `where T : Readable`을 쓸 수 있습니다. `IBox<int>` 같은 구체적인 제네릭 인터페이스의 별칭도 지원합니다. 별칭은 선언이 속한 모듈에서 해석되며, 구체적인 형식은 해석된 인터페이스를 구현해야 합니다.

## 일반 멤버 및 인터페이스

제네릭 유형은 인터페이스 또는 제네릭 기본 유형을 상속할 수 있으며 인터페이스 자체에는 유형 매개변수가 있을 수 있습니다.

```VeroScript
interface IBox<T>
{
	virtual T get() const = 0;
};

class Box<T> : public IBox<T>
{
public:
	T value;

	T get() const override
	{
		return value;
	}
};
```

제네릭 선언의 멤버는 여전히 액세스 제어, 가상 메서드, 오버로드 및 정적 멤버에 대한 일반 규칙을 따릅니다. 일반 정적 멤버는 각 구체적인 유형에 대해 독립적인 저장소를 갖습니다. [정적 멤버](static-members.md)를 참조하세요.

## 유형 매개변수의 경계

형식 매개 변수는 컴파일러가 해당 선언에서 설정할 수 있는 작업에만 사용할 수 있습니다. 인터페이스 메서드를 호출하거나 제한된 멤버에 액세스하기 전에 `where` 제약 조건을 추가하세요. 선언되지 않은 암시적 멤버에 의존하지 마세요.

`T::member` 같은 정규화된 정적 접근은 구체 형식을 대입한 뒤 검사하며 해당 형식에 접근 가능한 정적 필드나 메서드가 있어야 합니다. 접근, 오버로드, const／참조 규칙도 적용됩니다. 자기 형식, 별칭, 필드 기본값 예제는 [정적 멤버](static-members.md)를 참고하세요.

중첩 형식, 지역 선언, 접근 규칙 및 단계별 제네릭 인수는 [형식 범위](scope.md)를 참조하세요.

## 메서드 템플릿과 값 매개변수

`T`, `typename T`, `class T`는 같은 형식 매개변수 선언입니다. 인스턴스 메서드, static 메서드, 본문이 있는 비가상 인터페이스 보조 메서드는 자체 매개변수를 가질 수 있습니다. 내부 매개변수는 외부 이름을 가릴 수 없으며 메서드 템플릿에 virtual, override, final을 지정할 수 없습니다.

값 매개변수는 bool, 고정 너비 정수, 스크립트 또는 등록된 호스트 열거형을 지원합니다. `template<typename T, T Value>`처럼 앞선 형식에 의존할 수 있습니다. 예: `template<typename T, int Count = 4> struct Buffer { T values[Count]; };`. 값은 변경할 수 없는 컴파일 시간 상수입니다. 순수 스칼라 식, 검사된 캐스트, 열거형 멤버, 평가 가능한 const 초기화만 사용하며 조건을 만족하는 [constexpr 함수](../language-reference/constexpr.md)를 평가할 수 있습니다. 다른 함수나 호스트 콜백은 실행하지 않습니다. 오버플로, 0 나누기, 범위 초과, 열거형 식별 불일치는 오류입니다. 고정 배열 길이는 구체화 후 검사합니다.

기본값 이후의 자체 매개변수는 모두 기본값이 필요합니다. 기본값은 선언 위치에서 바인딩되며 외부 및 앞선 매개변수를 참조할 수 있습니다. 호출은 명시적 접두 인수, 인수 형식 구조의 추론, 기본값 순서로 처리합니다. 실행 시간 수치로 값 매개변수를 추론하지 않습니다.

`box.echo<string>("x")`는 템플릿을 명시적으로 선택하고 `box.echo("x")`는 추론을 허용합니다. `f<>()`는 템플릿 후보만 고려합니다. 변환 품질을 먼저 비교하며 같으면 일반 후보가 우선합니다. 나머지는 증명 가능한 구조적 구체성만 비교하고 결정할 수 없으면 모호성 오류를 냅니다. 형식 조회는 자체 전체 매개변수 수의 정확한 일치를 기본값 보완보다 우선합니다.

`identity<int>`, `box.echo<string>`, static 메서드 참조는 callable을 만듭니다. 명시적 fn 대상 시그니처도 추론을 돕습니다. 동적 문자열 이름은 열린 메서드 템플릿을 자동 구체화하지 않습니다. 클래스 외부 정의는 각 owner와 메서드에 template 헤더를 사용하고 위치로 대응하며 기본값을 반복하지 않습니다.

`method_templates.vs`, `value_template_parameters.vs`, `template_methods_main.vs`를 참조하세요. Core VM/Mixed/JIT 또는 Core AOT를 사용합니다. 독립 소스 내보내기는 지원하지 않는 템플릿을 출력 전에 진단합니다. 컴파일된 라이브러리는 원본 소스 없이 새로운 인수를 쓸 수 있도록 열린 선언을 보존합니다. 리플렉션 조회만으로 실행 본문을 만들지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/generics.md) · [fr-FR](../../fr-FR/language-basics/generics.md) · [ja-JP](../../ja-JP/language-basics/generics.md) · [ko-KR](generics.md) · [zh-CN](../../zh-CN/language-basics/generics.md) · [zh-TW](../../zh-TW/language-basics/generics.md)
