# 정적 멤버

`static`으로 선언된 멤버는 하나의 개체 인스턴스가 아닌 형식 자체에 속합니다. 정적 필드에는 하나의 유형 수준 저장 위치가 있으며 정적 메서드에는 객체 수신기가 필요하지 않습니다.

## 정적 유틸리티 형식

`static class`와 `static struct`는 인스턴스화할 수 없는 유틸리티 컨테이너를 선언합니다. 내부에 선언된 모든 필드와 메서드는 정적으로 취급되며 형식 이름과 `::`을 사용해 액세스합니다.

```VeroScript
public static class Math
{
	public double Square(double value)
	{
		return value * value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Square(4.0));
	print(Limits::MaxRetries);
}
```

정적 클래스 또는 정적 구조체는 생성할 수 없으며 기본 형식으로 사용할 수도 없습니다. 제네릭으로 선언하거나 생성자, 소멸자, `using` 멤버 또는 기본 초기화 블록을 선언할 수 없습니다. 필드에는 `mutable`을 사용할 수 없고 메서드에는 `virtual`, `override`, `final`, 순수 가상 또는 `const` 같은 인스턴스 한정자를 사용할 수 없습니다. 액세스 섹션은 계속 적용됩니다. 정적 클래스는 클래스의 기본 액세스를 따르고 정적 구조체는 구조체의 기본 액세스를 따릅니다. 인스턴스, 상속 또는 인스턴스 상태가 필요하면 일반 `class` 또는 `struct`를 사용하세요.

## 정적 필드

유형 이름과 `::`을 통해 정적 필드에 액세스합니다. 아래 `Total` 필드는 모든 `Counter` 인스턴스에서 공유됩니다.

```VeroScript
class Counter
{
public:
	static int Total = 0;
	int id;

	Counter()
	{
		Counter::Total = Counter::Total + 1;
		id = Counter::Total;
	}
};

Counter first = Counter();
Counter second = Counter();
print("{} {}", first.id, Counter::Total);
```

정적 필드에는 여전히 액세스 제어가 적용됩니다. 형식 외부의 코드가 액세스해야 하는 경우 `public` 섹션에서 필드를 선언합니다.

## 정적 메소드

호출을 위한 객체를 생성하지 않고 유형 이름을 통해 정적 메서드를 호출합니다.

```VeroScript
class Counter
{
public:
	static int Total = 0;

	static int Peek()
	{
		return Counter::Total;
	}
};

int current = Counter::Peek();
```

정적 메서드에는 암시적 인스턴스 개체가 없으므로 비정적 필드에 직접 액세스하거나 인스턴스 종속 멤버를 호출할 수 없습니다. 인스턴스 멤버가 필요할 때 객체를 명시적으로 전달합니다.

```VeroScript
class Formatter
{
public:
	string prefix;

	static string with_prefix(Formatter formatter, string value)
	{
		return formatter.prefix + value;
	}
};
```

## 제네릭 형식의 정적 멤버

일반 유형의 각 구체적인 유형 인스턴스에는 독립적인 정적 저장소가 있습니다. 예를 들어 `Box<int>::Count`과 `Box<string>::Count`은 별도의 값입니다.

```VeroScript
class Box<T>
{
public:
	static int Count = 0;
	T value;
};

Box<int>::Count = Box<int>::Count + 1;
Box<string>::Count = Box<string>::Count + 1;
```

정적 멤버를 사용하는 경우 전체 형식 이름으로 한정하는 것이 좋습니다. 이렇게 하면 멤버가 개체 인스턴스가 아닌 형식에 속한다는 것이 분명해집니다.

## 멤버 기본값과 초기화

인스턴스 필드 기본값은 해당 인스턴스의 일반 생성 과정에서 평가됩니다. 제네릭 형식을 포함하여 소유 형식에서 뒤에 선언된 정적 메서드를 호출할 수 있으며 컴파일 시 고정한 스냅샷이 아닌 현재 정적 값을 읽습니다. 다른 인스턴스를 생성해도 형식의 정적 필드를 다시 초기화하지 않습니다. 접근 제어, const 및 생성자 초기화 규칙은 계속 적용됩니다.

```VeroScript
class Sample<T>
{
public:
	static int Seed = 7;
	T snapshot = Sample<T>::Read();

	static T Read()
	{
		return cast<T>(Sample<T>::Seed);
	}
};

int main()
{
	Sample<int> first = Sample<int>();
	Sample<int>::Seed = 11;
	Sample<int> second = Sample<int>();
	print(first.snapshot);
	print(second.snapshot);
	print(first.snapshot);
	return 0;
}
```

출력은 각 줄에 `7`, `11`, `7`입니다.

제네릭 또는 중첩 형식 내부에서 자신의 어휘 이름은 현재 특수화를 가리키며 바깥 형식 및 값 인수를 유지합니다. `Outer<int>::Inner` 같은 명시적 바깥 인수는 그대로 유지됩니다. 같은 구체 형식의 별칭은 정적 저장소를 공유합니다. 이름이 같은 로컬 값은 식에서 값으로 남으며 선행 `::`는 루트 범위를 선택합니다. [범위](scope.md)를 참고하세요.

형식의 정적 필드는 각 실행 상태의 모듈 시작 시 초기화되며 처음 도달할 때 초기화하는 함수 로컬 static과 다릅니다. 식별할 수 있는 제네릭 정적 의존성은 이를 사용하는 초기화자보다 먼저 준비됩니다. 람다를 저장하는 것만으로 본문을 호출하지 않으며 nullable 필드나 빈 컨테이너가 요소를 생성하지도 않습니다. 이를 임의의 지연 초기화나 무관한 형식 전체의 순서로 취급하지 마세요. 의존성을 단순하게 유지하고 순서가 중요한 부작용은 `main`에서 명시적으로 수행하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/static-members.md) · [fr-FR](../../fr-FR/language-basics/static-members.md) · [ja-JP](../../ja-JP/language-basics/static-members.md) · [ko-KR](static-members.md) · [zh-CN](../../zh-CN/language-basics/static-members.md) · [zh-TW](../../zh-TW/language-basics/static-members.md)
