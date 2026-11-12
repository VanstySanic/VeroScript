# 구조체

`struct`은 값 의미 체계로 사용자 유형을 정의합니다. 구조체에는 필드, 메서드, 생성자 및 소멸자가 포함될 수 있습니다. 또한 하나의 기본 유형에서 상속하거나 인터페이스를 구현할 수도 있습니다. 구조체 객체 자체는 값으로 복사됩니다. 각 필드는 여전히 자체 유형의 의미를 따릅니다.

## 선언 및 멤버

구조체 선언은 이름과 멤버 목록으로 구성됩니다. 구조체 멤버는 기본적으로 `public`입니다.

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	void translate(int dx, int dy)
	{
		this.x += dx;
		this.y += dy;
	}
};
```

멤버 함수 내에서 `this`은 현재 구조체 개체를 참조합니다. 순수 이름으로 액세스할 수 있는 멤버는 `this.member`로도 액세스할 수 있습니다.

구조체는 `public`, `protected` 및 `private` 액세스 제어를 지원합니다. 멤버는 기본적으로 공개되지만 액세스를 제한해야 하는 경우 명시적인 액세스 지정자를 사용합니다.

```VeroScript
struct Account
{
public:
	int id = 0;

private:
	int balance = 0;
};
```

## 구성 및 초기화

필드는 기본 초기화 표현식을 제공할 수 있습니다. 구조체는 생성자를 선언할 수도 있습니다. 생성은 인수 목록에서 오버로드를 선택합니다.

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point()
	{
	}

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}
};

Point origin = Point();
Point point = Point(3, 4);
Point literal = Point{7, 8};
```

생성자 초기화 목록에서는 생성자 본문보다 먼저 직접 인스턴스 필드를 초기화할 수 있습니다. 목록의 필드 항목은 필드 선언 순서대로 초기화됩니다. 선언 이니셜라이저가 있는 필드는 이 목록에 다시 나타날 수 없습니다. `const` 필드 규칙은 [상수](constants.md)를 참조하세요.

중괄호 형식은 형식화된 초기화이며 동일한 생성자 일치 규칙을 따릅니다. 명시적 생성자가 선언되지 않은 경우 필드 기본값은 여전히 기본 생성에 참여합니다.

## 값 의미

구조체를 할당하면 해당 값이 복사됩니다. 복사본에서 직접 필드를 수정해도 원본 구조는 변경되지 않습니다.

```VeroScript
struct Counter
{
	int value = 0;
};

Counter first = Counter();
Counter second = first;
second.value = 10;

print(first.value);  // 0
print(second.value); // 10
```

값 의미론은 구조체 개체 자체를 설명합니다. 필드가 `array<T>`, `map<K, V>` 또는 다른 공유 핸들 유형인 경우 해당 필드는 구조체가 복사될 때 자체 공유 핸들 동작을 유지합니다. 독립 컨테이너가 필요한 경우 컨테이너 내용을 명시적으로 복사합니다.

## 방법 및 참고자료

구조체 메서드는 현재 개체를 읽거나 수정할 수 있습니다. 함수가 호출자의 구조체를 직접 수정해야 하는 경우 참조 매개변수를 사용하세요.

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}

	void translate(int dx, int dy)
	{
		x += dx;
		y += dy;
	}
};

void reset(ref Point point)
{
	point.x = 0;
	point.y = 0;
}

int main()
{
	Point point = Point{3, 4};
	point.translate(1, 2);
	reset(ref point);
	print("{}, {}", point.x, point.y);
	return 0;
}
```

일반 매개변수와 값 범위별 반복은 구조체 값 복사본을 생성합니다. 참조 매개변수 및 참조 범위 반복은 원래 객체에 기록됩니다. 자세한 규칙은 [참고자료](references.md)를 참조하세요.

## `mutable` 구현 필드

비정적 인스턴스 필드는 `const` 쿼리에서 갱신할 수 있는 구현 상태를 나타낼 때 `mutable`로 표시할 수 있으며, 가장 일반적인 용도는 캐시입니다.

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

이 예외는 표시된 필드에만 적용됩니다. const 객체 전체를 일반적으로 쓰기 가능하게 만들거나 동시 액세스의 안전성을 보장하지 않습니다. `mutable` 필드는 `const`, `static` 또는 참조 필드와 함께 사용할 수 없습니다.

## 상속과 인터페이스

구조체는 하나의 구조체 베이스에서 상속하고 하나 이상의 인터페이스를 구현할 수 있습니다.

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Counter : IReadable
{
	int value = 0;

	int read() const
	{
		return value;
	}
};
```

구조체를 인터페이스 유형으로 변환하면 인터페이스에 필요한 값 표현이 생성됩니다. 해당 인터페이스 값을 변경해도 원래 구조체에 다시 쓰지 않습니다. 인터페이스 상속은 [인터페이스](interfaces.md) 페이지에서 다룹니다. 일반 제약 조건은 [일반](generics.md) 페이지에서 다룹니다.

## 기본 특수 멤버

```VeroScript
struct Record
{
	int value = 0;
	Record() = default;
	Record(const Record& other) = default;
	Record& operator=(const Record& other) = default;
	bool operator==(const Record& other) const = default;
	bool operator!=(const Record& other) const = default;
};
```

구조체는 멤버별 복사 생성, 대입, 동등 비교를 명시적으로 생성할 수 있습니다. 복사 생성은 필드 초기화 코드를 다시 실행하지 않습니다. 대입은 기존 하위 객체를 갱신하고 원래 객체의 참조를 반환합니다. const 또는 참조 필드가 있으면 기본 대입을 사용할 수 없을 수 있습니다. 복사와 비교의 사용 가능 여부는 각각 판단합니다.

클래스는 공유 핸들 복사를 유지하면서 기본 동등 비교를 생성할 수 있습니다. 삭제되었거나 접근할 수 없는 연산을 원시 복사로 대체하지 않습니다. 명시적 복사 생성자는 직접 생성에 사용할 수 있지만 암시적 초기화에는 사용할 수 없습니다. 새 생성 결과에는 추가 복사가 필요하지 않습니다. 스크립트 이동 연산은 추가하지 않습니다.

이 선언은 코어 실행 기능을 사용합니다. 혼합／JIT 정책의 가상 머신 대체 실행과 코어 AOT도 포함합니다. 독립 C++, C#, Python 소스 내보내기는 아직 지원하지 않는 기본 멤버를 거부합니다. 예제: `default_special_members.vs`.

중첩 형식, 지역 선언, 접근 규칙 및 단계별 제네릭 인수는 [형식 범위](scope.md)를 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/structs.md) · [fr-FR](../../fr-FR/language-basics/structs.md) · [ja-JP](../../ja-JP/language-basics/structs.md) · [ko-KR](structs.md) · [zh-CN](../../zh-CN/language-basics/structs.md) · [zh-TW](../../zh-TW/language-basics/structs.md)
