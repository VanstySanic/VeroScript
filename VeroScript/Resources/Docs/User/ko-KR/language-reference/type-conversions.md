# 유형 변환

유형 변환은 표현식을 다른 유형의 값으로 사용할 수 있는지 여부를 결정합니다. 변환이 유효한지 여부는 소스 유형, 대상 유형 및 표현식 컨텍스트에 따라 다릅니다.

## 숫자 유형

호환되는 정수 및 부동 소수점 유형은 언어의 숫자 변환 규칙에 따라 할당, 인수 전달 및 작업에 참여할 수 있습니다. 고정 너비 정수는 명시적인 데이터 너비 경계에서 유용합니다. 더 좁거나 부호 없는 유형으로 변환할 때 값 범위가 잘리거나 예기치 않게 변경되지 않는지 확인하세요.

```VeroScript
int count = 10;
double total = count;
int rounded = to_int(total);
```

의도가 중요한 경우 명시적인 대상 유형 구성 또는 변환 형식을 사용하세요. 숫자 변환 대신 문자열 연결이나 암시적 변환을 사용하지 마십시오.

## 변환 컨텍스트

VeroScript는 자동으로 허용되는 암시적 변환과 명시적으로 요청해야 하는 변환을 구분합니다.

| 컨텍스트 | 목적 | 주요 규칙 |
| --- | --- | --- |
| 대입, 인수, 반환 값 | 암시적 변환 | 호환되는 숫자 변환과 호환되는 기반 클래스·인터페이스 변환을 고려합니다. `explicit`이 아닌 단일 값 생성자도 참여할 수 있습니다. |
| `cast<T>(value)` | 명시적 변환 또는 생성 | 숫자, 스코프 enum, 참조, 인터페이스 및 명시적 생성자 변환을 지원하며 사용자 정의 변환 연산자도 요청할 수 있습니다. 검사된 참조 캐스트가 실패하면 런타임 오류입니다. |
| `try_cast<T>(value)` | 실패 가능한 변환 | `optional<T>`을 반환합니다. 호환되지 않는 참조 변환은 빈 optional이 되며 `explicit` 생성자를 호출하지 않습니다. |
| `const_cast<T>(value)` | const 접근 권한 제거 | 소스는 const이고 `T`와 동일한 정규화 타입이어야 합니다. 값 변환이나 런타임 타입 검사는 수행하지 않습니다. |

사용자 정의 변환은 연쇄되지 않습니다. `A`에서 `B`, `B`에서 `C` 변환이 각각 존재해도 `A`에서 `C`로 두 단계를 암시적으로 변환하지 않습니다.

## 텍스트 유형

`string` 및 `wstring`은 고유한 유형이므로 할당, 비교 또는 연결 시 직접 혼합할 수 없습니다. 명시적인 표준 라이브러리 변환 함수를 사용하십시오.

```VeroScript
string text = "hello";
wstring wide = to_wstring(text);
string restored = to_string(wide);
```

혼합 유형 암시적 연결에 의존하는 대신 숫자 또는 부울 값을 텍스트로 변환할 때 `to_string`을 사용합니다.

## 변환 생성자와 변환 연산자

값 매개변수 하나를 받는 생성자는 `explicit`으로 표시되지 않은 경우 암시적 변환에 참여할 수 있습니다.

```VeroScript
struct Meter
{
	int value;

	Meter(int input)
	{
		value = input;
	}
};

Meter assigned = 7;
```

직접 생성 또는 명시적 `cast<T>`에서만 호출하려면 생성자를 `explicit`으로 표시합니다.

```VeroScript
struct UserId
{
	int value;

	explicit UserId(int input)
	{
		value = input;
	}
};

UserId direct(7);
UserId converted = cast<UserId>(8);
// UserId invalid = 9; // Compile error.
```

`operator bool()`과 같은 사용자 정의 변환 연산자도 언어가 허용하는 컨텍스트에서 사용할 수 있습니다. 암시적 변환이 예측 가능하도록 놀라운 변환 연산자는 피하세요.
`cast<T>`는 일치하는 사용자 정의 변환 연산자를 명시적으로 요청할 수도 있습니다. `try_cast<T>`는 문서에 설명된 기본 형식, scoped enum 및 참조/인터페이스 경우로 제한되며 사용자 정의 생성자나 변환 연산자를 호출하지 않습니다.

## `cast<T>`

`cast<T>(value)`는 명시적 변환을 요청합니다. 호환되는 숫자, 스코프 enum과 기반 정수형, 검사된 클래스·인터페이스 참조, 대상 struct/class의 명시적 생성에 사용할 수 있습니다.

```VeroScript
double precise = cast<double>(7);
int narrowed = cast<int>(2.9);

enum class Mode
{
	Idle = 3
};

Mode mode = cast<Mode>(3);
int raw_mode = cast<int>(mode);
```

클래스와 인터페이스 참조에서 downcast 또는 cross-cast가 필요하면 런타임 관계를 검사합니다. 실패하면 런타임 오류입니다. struct를 인터페이스로 변환하면 값 표현이 생성되고, class를 인터페이스로 변환하면 공유 클래스 ID가 유지됩니다.

`cast<T>`는 const 접근 권한을 제거하지 않습니다. const 소스에서 얻은 결과도 const로 접근됩니다. 의도적인 const 해제에는 `const_cast<T>`를 사용하세요.

구조체 값으로의 캐스트가 성공하면 값 의미 체계를 따릅니다.

## 괄호식 캐스트 구문

`(T)식`은 `cast<T>(식)`의 다른 표기입니다. 숫자, 스코프 열거형, 참조/인터페이스, 명시적 생성,
일치하는 사용자 정의 변환 연산자를 포함하여 동일한 명시적 변환 규칙을 따릅니다.

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);

int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print("a={}, calls={}", a, calls);
	return 0;
}
```

이 형식은 단항식 위치에서 인식됩니다. 단순 사용자 형식 이름은 값 식과 모호해질 수 있으므로 대상
형식이 분명하지 않을 때는 `cast<T>`를 사용하세요.

`(void)식;`은 문장 위치에서만 사용할 수 있는 명시적 결과 폐기 구문입니다. 식을 정확히 한 번
평가하고 부작용과 런타임 오류는 유지한 뒤 결과만 버립니다. `for` 초기화 문에서도 사용할 수
있습니다. 값 식이 아니므로 인수, 대입 값, 산술 피연산자 또는 중첩 식으로 사용할 수 없습니다.
일반적인 `식;`도 런타임 동작은 같으며, `(void)` 접두사는 결과를 버리려는 의도를 명시합니다.

## `try_cast<T>`

`try_cast<T>(value)`는 기본형, 스코프 enum, 참조·인터페이스 변환의 실패를 값으로 처리하는 방식입니다.

```VeroScript
class Base
{
};

class Derived : public Base
{
};

class Other
{
};

Base base = Derived();
optional<Derived> found = try_cast<Derived>(base);
optional<Other> missing = try_cast<Other>(base);

if (found.has_value())
{
	print("derived");
}

if (!missing.has_value())
{
	print("not compatible");
}
```

`try_cast`는 변환에 성공하면 engaged optional을 반환합니다. 검사된 참조 변환에 실패하면 빈 optional을 반환합니다. null 참조 변환은 nullable 대상에 대한 성공한 변환이므로 `nullptr`을 포함한 engaged optional을 만들 수 있습니다. 숫자 변환은 변환된 값을 생성하므로 예를 들어 `try_cast<int>(2.9)`에는 `2`가 들어갑니다.

`try_cast<T>`는 `explicit` 생성자를 선택하지 않습니다. 명시적 생성이 목적이면 `cast<T>`를 사용하세요.

## `const_cast<T>`

`const_cast<T>(value)`는 제한적인 const 탈출입니다. 소스 식은 const여야 하며 별칭을 해석한 정규화 타입이 `T`와 정확히 같아야 합니다.

```VeroScript
class Counter
{
public:
	int value = 41;
};

int main()
{
	readonly<Counter> stored = Counter();
	const_cast<Counter>(stored.get()).value = 42;
	print(stored.get().value);
	return 0;
}
```

이 예제는 공유 클래스 객체의 필드를 변경합니다. `const_cast<int>(view)`는 대입 가능한 참조가 아니라 스칼라 값을 생성하므로 `const_cast<int>(view) = 42`는 유효하지 않습니다. 캐스트는 스칼라 복사본을 원래 저장소의 별칭으로 바꾸지 않습니다.

이 연산은 컴파일 시점의 const 접근 권한만 제거합니다. 값을 변환하거나 런타임 형식을 변경하거나 검사된 캐스트를 수행하지 않습니다. 제어된 `readonly<T>` 래퍼와 같이 의도적으로 수정 가능한 저장소에서만 사용하세요. 실제로 변경할 수 없는 저장소에 적용하면 `const`가 제공하려던 보호를 무력화합니다.

다음 형식은 거부됩니다.

```VeroScript
const int number = 1;
// const_cast<double>(number); // The target type must be exactly int.
// const_cast<int>(mutable_value); // The source must be const.
```

## 구조체, 클래스, 인터페이스 및 컨테이너

구조체 변환은 값 의미 체계를 따르고 구조체 값의 복사본을 만듭니다. 그 자체가 공유 핸들인 구조체 필드는 여전히 해당 필드의 핸들 의미를 유지합니다. 클래스 변환은 공유 객체 ID를 유지합니다.

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Point : IReadable
{
	int value;
	int read() const override
	{
		return value;
	}
};

Point point = Point();
IReadable readable = point;
```

구조체를 인터페이스로 변환하면 인터페이스에 필요한 값 표현이 생성됩니다. 클래스를 인터페이스로 변환하면 클래스 객체의 공유 ID가 유지됩니다. 구체적인 유형은 인터페이스에 필요한 모든 순수 가상 메소드를 구현해야 합니다.

컨테이너 요소, 키 및 값 유형은 해당 선언 규칙을 충족해야 합니다. 일반 함수나 유형은 해당 인수가 선언된 제약 조건을 충족하는 경우에만 인스턴스화될 수 있습니다. `where` 제약 조건은 변환 규칙을 완화하지 않습니다.

## 의존하지 말아야 할 전환

`nullptr`은 nullable 핸들 유형에만 적용됩니다. 텍스트 유형은 일반 할당을 통해 교환할 수 없으며 구조체 값은 필드가 유사해 보인다는 이유만으로 클래스 핸들과 교환할 수 없습니다. 호스트 바인딩 및 인터페이스 경계에서 명시적인 대상 유형을 선호합니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-reference/type-conversions.md) · [fr-FR](../../fr-FR/language-reference/type-conversions.md) · [ja-JP](../../ja-JP/language-reference/type-conversions.md) · [ko-KR](type-conversions.md) · [zh-CN](../../zh-CN/language-reference/type-conversions.md) · [zh-TW](../../zh-TW/language-reference/type-conversions.md)
