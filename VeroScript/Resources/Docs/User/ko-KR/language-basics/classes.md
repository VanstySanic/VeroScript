# 수업

`class`은 공유 ID를 가진 사용자 유형을 정의합니다. 클래스 값은 핸들이고 null일 수 있으며 필드, 메서드, 생성자, 소멸자, 상속 및 가상 메서드를 지원합니다. 구조체와 달리 클래스 변수를 할당하면 일반적으로 개체 자체가 아닌 개체 핸들이 복사됩니다.

## 선언 및 액세스 제어

클래스에는 필드와 멤버 함수가 포함될 수 있습니다. 클래스 멤버는 기본적으로 `private`이므로 호출자에게 노출되는 인터페이스에 대해 명시적인 `public` 섹션을 사용합니다.

```VeroScript
class Counter
{
	int value = 0;

public:
	void add(int amount)
	{
		value += amount;
	}

	int get() const
	{
		return value;
	}
};
```

멤버는 `protected` 또는 `private`일 수도 있습니다. 액세스 지정자는 클래스, 파생 클래스 및 외부 코드에 표시되는 멤버를 제어합니다. `friend`는 지정된 함수 또는 클래스에 대한 추가 액세스 권한을 부여할 수 있습니다.

## 구성 및 null 개체

클래스는 하나 이상의 생성자를 선언할 수 있습니다. 생성 표현식은 새로운 클래스 객체를 생성합니다:

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}
};

Counter counter = Counter(10);
```

생성자 초기화 목록에서는 생성자 본문보다 먼저 직접 기본 생성자를 호출하고 직접 인스턴스 필드를 초기화할 수 있습니다. 목록의 필드 항목은 필드 선언 순서대로 초기화됩니다. 선언 이니셜라이저가 있는 필드는 이 목록에 다시 나타날 수 없습니다. `const` 필드 규칙은 [상수](constants.md)를 참조하세요.

생성 또는 소멸 중에 해당 객체의 가상 메서드를 호출하면 현재 실행 중인 생성자 또는 소멸자가 속한 타입의 구현을 사용합니다. 도우미 함수나 바인딩된 메서드를 통한 호출도 이 규칙을 따릅니다. 기본 타입의 생성자는 아직 초기화되지 않은 파생 부분의 재정의 메서드를 호출하지 않습니다. 해당 단계에서 구현이 없는 순수 가상 메서드를 호출하면 런타임 오류가 발생합니다. 생성이 완료되면 일반적인 동적 디스패치는 실제 파생 타입을 사용합니다.

생성에 실패한 타입의 소멸자는 실행되지 않습니다. 초기화된 멤버와 생성이 완료된 기본 타입은 생성의 역순으로 해제됩니다.

클래스 핸들에는 `nullptr`이 포함될 수 있습니다.

```VeroScript
Counter empty = nullptr;
if (empty == nullptr)
{
	print("empty");
}
```

null 핸들을 통해 인스턴스 멤버를 호출하면 런타임 오류가 발생합니다. 핸들을 사용하기 전에 핸들이 유효한 객체를 참조하는지 확인하세요.

## 공유 ID

하나의 클래스 변수를 다른 클래스 변수에 할당하면 두 변수가 모두 동일한 객체를 참조하게 됩니다.

```VeroScript
class Box
{
public:
	int value = 0;

	void set(int next)
	{
		value = next;
	}
};

Box first = Box();
Box second = first;
second.set(10);

print(first.value);  // 10
```

독립적인 ID가 필요할 때 다른 개체를 구성합니다. 핸들을 복사하지 마십시오. 클래스 필드, 컨테이너의 클래스 값 및 클래스 매개변수는 동일한 공유 ID 의미 체계를 따릅니다.

## Const 개체 및 쿼리 메서드

멤버 함수는 `const`으로 한정될 수 있습니다. 즉, 현재 개체를 통해 멤버를 수정할 수 없습니다. const 객체는 const 객체에 유효한 메서드만 호출할 수 있습니다.

```VeroScript
class Counter
{
public:
	int value = 0;

	int get() const
	{
		return value;
	}
};

const Counter counter = Counter();
print(counter.get());
```

개체 상태만 검사하는 작업에는 `const` 멤버 함수를 선호합니다. 변경 가능한 멤버 함수는 `const` 개체를 통해 호출할 수 없습니다.

### `mutable` 구현 필드

`mutable`은 `struct` 또는 `class`의 비정적 인스턴스 필드에 대한 문맥적 필드 지정자입니다. `const` 멤버 함수에서 해당 필드 하나를 갱신할 수 있습니다.

```VeroScript
class Label
{
	string source;
	mutable string cached;

public:
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

`mutable`은 전체 객체를 쓰기 가능하게 만들지 않으며 스레드 안전성을 제공하지 않고 `const`, `static`, 참조 필드와 함께 사용할 수 없습니다. 전역 예약어가 아니므로 필드 선언 밖에서는 문법이 허용되는 경우 일반 식별자로 사용할 수 있습니다.

## 상속 및 가상 메서드

클래스는 하나 이상의 기본 클래스에서 상속하고 인터페이스를 구현할 수 있습니다. 동적으로 전달되는 메서드에는 `virtual`을 사용하고 기본 메서드 재정의를 명시적으로 만들려면 `override`을 사용합니다.

```VeroScript
class Base
{
public:
	virtual int value() const
	{
		return 1;
	}
};

class Derived : public Base
{
public:
	int value() const override
	{
		return 2;
	}
};

int main()
{
	Base object = Derived();
	print(object.value());
}
```

추상 클래스는 `virtual int value() const = 0;`과 같은 순수 가상 메서드를 선언할 수 있습니다. 구현되지 않은 순수 가상 슬롯이 있는 클래스는 인스턴스화할 수 없습니다. 가상 소멸자는 파생 개체가 기본 클래스 ID를 통해 소멸될 때 올바른 소멸 경로를 보장합니다.

인터페이스, 상속 액세스 및 다중 상속에 대한 전체 규칙은 [인터페이스](interfaces.md) 문서에서 다룹니다.

## 소멸자

클래스는 개체 수명이 끝날 때 실행되는 소멸자를 `~TypeName()`으로 선언할 수 있습니다. 소멸자는 가상이거나 순수 가상일 수도 있습니다. 상속이 관련된 경우 객체가 어떻게 파괴되는지에 따라 가상 소멸자 디자인을 선택하십시오.


## 클래스 외부 정의

클래스 안에서 멤버를 선언하고 같은 소스 모듈의 뒤쪽에서 `Type::member`로 정의할 수 있습니다. 매개변수와 반환 타입, 참조 한정자 및 `const`는 선언과 일치해야 합니다. 생성자에는 반환 타입이 없습니다. 이는 소스 정의이며 선언과 구현을 별도로 링크하는 스크립트 모듈로 나누지 마세요.

```VeroScript
class Counter
{
public:
	Counter(int seed);
	int Add(int delta) const;
	int value;
};

Counter::Counter(int seed)
{
	this.value = seed;
}

int Counter::Add(int delta) const
{
	return this.value + delta;
}

void main()
{
	Counter counter = Counter(6);
	print(counter.Add(1));
}
```

중첩 형식, 지역 선언, 접근 규칙 및 단계별 제네릭 인수는 [형식 범위](scope.md)를 참조하세요.

## 자기 참조 필드

제네릭 클래스는 자신의 특수화를 참조하는 핸들을 필드에 저장할 수 있습니다. 필드는 `nullptr`로 초기화할 수 있으며, 다른 객체를 대입해도 해당 객체의 동일성이 유지됩니다.

```VeroScript
class Node<T>
{
public:
	T value;
	Node<T> next = nullptr;
};
```

구조체 필드의 직접 또는 상호 값 재귀는 계속 금지됩니다. 순환 상속과 무한한 템플릿 확장도 거부되며 컴파일 깊이 제한은 그대로 적용됩니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/classes.md) · [fr-FR](../../fr-FR/language-basics/classes.md) · [ja-JP](../../ja-JP/language-basics/classes.md) · [ko-KR](classes.md) · [zh-CN](../../zh-CN/language-basics/classes.md) · [zh-TW](../../zh-TW/language-basics/classes.md)
