# 05: 구조체, 클래스 및 인터페이스

이 튜토리얼에서는 읽을 수 있는 작은 객체 예제를 사용하여 세 가지 사용자 정의 유형(값에 대한 구조체, 공유 ID에 대한 클래스, 교체 가능한 동작에 대한 인터페이스)을 비교합니다.

## 1단계: 구조체로 값 표현

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	int distance_score() const
	{
		return x * x + y * y;
	}
};

int main()
{
	Point point = Point();
	point.x = 3;
	point.y = 4;
	print(point.distance_score());
	return 0;
}
```

구조체는 일반적으로 값 그룹을 나타냅니다. 구조체를 다른 변수에 할당하는 것은 일반적으로 값 복사로 이해되며 이는 좌표, 설정 및 작은 레코드에 잘 작동합니다.

## 2단계: 클래스와의 공유 ID 표현

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}

	void add(int amount)
	{
		value = value + amount;
	}
};

int main()
{
	Counter first = Counter(10);
	Counter second = first;
	second.add(5);
	print(first.value);
	return 0;
}
```

`first`과 `second`가 동일한 클래스 개체를 참조하기 때문에 `15`이 인쇄됩니다. 클래스 할당은 독립 개체가 아닌 개체 ID를 복사합니다. 별도의 상태가 필요할 때 다른 객체를 생성하세요.

클래스 멤버는 기본적으로 전용이므로 이 예제에서는 생성자와 멤버에 `public:`을 사용합니다. 대규모 프로그램에서는 호출자가 제어된 작업을 대신 사용해야 하는 경우 필드를 비공개로 유지하세요.

### 변환 생성자와 `explicit`

값 매개변수가 하나인 비-`explicit` 생성자는 암시적 변환에 참여할 수 있습니다. 직접 생성하거나 `cast<T>`를 사용할 때만 생성되게 하려면 `explicit`으로 표시합니다.

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
```

`UserId invalid = 9;`는 거부됩니다. 이렇게 하면 오버로드 결정이 예측 가능해지고 래퍼 유형이 실수로 생성되는 것을 막을 수 있습니다.

`(T)식`은 `cast<T>(식)`의 문법 설탕이며 정확히 같은 변환 규칙을 따릅니다.

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);
```

단순 사용자 형식 이름이 같은 이름의 값 식과 모호해질 수 있다면 `cast<T>`를 사용하세요. `(void)식;`은 문장 위치에서만 사용할 수 있는 명시적 결과 폐기 구문입니다. 식을 한 번 평가하고 부작용과 런타임 오류는 유지한 뒤 결과만 버립니다. 일반 `식;`도 런타임 동작은 같습니다. `for` 초기화 문에서도 사용할 수 있지만 `(void)식`을 중첩 값이나 함수 인수로 사용할 수는 없습니다.

```VeroScript
int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print(calls);
	return 0;
}
```

### `mutable` 캐시 필드

`const` 메서드가 캐시 같은 구현 상태를 갱신해야 할 때만 비-static `struct` 또는 `class` 인스턴스 필드에 컨텍스트 `mutable`을 사용합니다. 객체 전체를 쓰기 가능하게 하거나 스레드 안전하게 만들지는 않으며, `const`, `static` 또는 참조 필드와 결합할 수 없습니다.

```VeroScript
struct Label
{
	string source;
	mutable string cached;

	string text() const
	{
		if (cached == "")
		{
			cached = source;
		}
		return cached;
	}
};
```

### 이식 가능한 읽기 전용 페이로드

`readonly<T>`는 `T`를 일반 값으로 저장하므로 래퍼 자체를 복사하거나 컨테이너에 넣을 수 있지만, `get()`은 `const T&`만 노출합니다. `readonly<Struct>`를 생성하면 구조체 값을 캡처하고, `readonly<Class>`를 생성하면 클래스 핸들을 유지합니다.

`const_cast<T>(value)`는 원본 표현식이 const이고 정규 런타임 유형이 `T`와 같을 때만 사용하세요. 변환이나 런타임 유형 검사 없이 컴파일 시간의 const 접근 능력만 제거합니다. 일반 `cast<T>`는 의도적으로 const 접근을 유지합니다.

위의 `Label` 선언을 유지하고 이 단계에는 다음 `main`을 사용합니다. 읽기 전용 wrapper 전에 `label`을 생성하며 첫 출력은 `source`입니다.

```VeroScript
int main()
{
	Label label = Label();
	label.source = "source";
	readonly<Label> published = readonly<Label>(label);
	print(published.get().text());

	// Deliberate escape: mutate the payload stored by the wrapper.
	const_cast<Label>(published.get()).cached = "recomputed";
	return 0;
}
```

### `cast`와 `try_cast` 선택

변환이 필요하고 검사된 참조 변환의 실패를 오류로 처리하려면 `cast<T>`를 사용합니다. 관계가 성립하지 않을 수 있고 호출자가 `optional<T>`을 확인해야 한다면 `try_cast<T>`를 사용합니다.

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

int main()
{
	Base value = Derived();
	optional<Derived> found = try_cast<Derived>(value);
	optional<Other> missing = try_cast<Other>(value);

	if (found.has_value())
	{
		print("derived");
	}
	if (!missing.has_value())
	{
		print("not compatible");
	}
	return 0;
}
```

`try_cast<T>`는 기본형, 스코프 enum 및 참조/인터페이스 변환을 처리하지만 `explicit` 생성자는 호출하지 않습니다. 전체 규칙은 [형 변환](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-reference/type-conversions.md)을 참조하세요.


## 3단계: 인터페이스를 사용하여 동작 설명

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Sensor : public IReadable
{
public:
	int value = 42;

	int read() const override
	{
		return value;
	}
};

int read_twice(IReadable value)
{
	return value.read() + value.read();
}

int main()
{
	Sensor sensor = Sensor();
	print(read_twice(sensor));
	return 0;
}
```

인터페이스는 객체를 생성하지 않습니다. 구현이 제공해야 하는 사항을 명시합니다. `read_twice`은 하나의 구체적인 센서 유형이 아닌 `IReadable` 계약에 따라 달라집니다.

## 4단계: 정적 유틸리티 형식 정의

형식이 작업이나 상수를 위한 이름 있는 컨테이너일 뿐이라면 `static class` 또는 `static struct`를 사용하세요. 선언된 모든 멤버를 정적으로 처리하므로 객체를 만들지 않고 형식 이름과 `::`으로 호출합니다.

```VeroScript
public static class Math
{
	public int Base = 40;
	public int Add(int value)
	{
		return Math::Base + value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Add(2));
	print(Limits::MaxRetries);
}
```

정적 유틸리티 형식은 생성하거나 상속할 수 없으며 제네릭 형식으로 선언할 수도 없습니다. 생성자, 소멸자, `using` 멤버, 기본 초기화 블록, `mutable` 필드 또는 `virtual`과 `const` 같은 인스턴스 한정자도 사용할 수 없습니다. 객체별 상태나 상속이 필요하면 일반 `class` 또는 `struct`를 사용하세요.

## 실용적인 선택

- 독립적인 값 복사가 유용한 경우 `struct`을 사용합니다.
- 프로그램의 여러 부분이 변경 가능한 ID를 공유해야 하는 경우 `class`을 사용합니다.
- 호출자가 구체적인 구현보다는 동작에 의존해야 하는 경우 `interface`를 사용합니다.
- 객체 ID나 인스턴스 상태가 필요하지 않은 경우 정적 유틸리티 형식을 사용합니다.

## 연습

1. `Point`에 `move` 메서드를 추가합니다.
2. `reset`를 `Counter`에 추가한 다음 `value`를 비공개로 유지합니다.
3. `FixedSensor`이라는 두 번째 `IReadable` 구현을 만듭니다.

[구조체](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/structs.md), [클래스](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/classes.md) 및 [인터페이스](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/interfaces.md)를 참조하세요. 결합된 예는 [`examples/object_basics.vs`](../../../../../VeroScript/Resources/Scripts/examples/object_basics.vs)입니다.

다음: [참조, 참조 반환 및 람다 캡처](06-references-and-captures.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/language/05-structs-classes-and-interfaces.md) · [fr-FR](../../fr-FR/language/05-structs-classes-and-interfaces.md) · [ja-JP](../../ja-JP/language/05-structs-classes-and-interfaces.md) · [ko-KR](05-structs-classes-and-interfaces.md) · [zh-CN](../../zh-CN/language/05-structs-classes-and-interfaces.md) · [zh-TW](../../zh-TW/language/05-structs-classes-and-interfaces.md)
