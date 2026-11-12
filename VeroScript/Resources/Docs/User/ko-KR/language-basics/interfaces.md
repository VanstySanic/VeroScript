# 인터페이스

`interface`은 구조체나 클래스가 구현할 수 있는 멤버 계약 집합을 선언합니다. 인터페이스는 직접 인스턴스화할 수 없지만 매개변수, 반환 유형 또는 변수 유형으로 사용하여 다양한 구현에 균일하게 액세스할 수 있습니다.

## 인터페이스 선언

인터페이스 메서드는 일반적으로 순수 가상 메서드로 선언됩니다. 순수 가상 메서드는 `virtual`을 사용해야 하며 `= 0;`로 끝나야 합니다.

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};
```

순수 가상 메서드는 공개되어야 합니다. 해당 매개변수 목록, 반환 유형 및 `const` 자격이 함께 구현 계약을 구성합니다.

## 인터페이스 구현

구조체 또는 클래스는 상속 목록에서 인터페이스 이름을 지정하고 일치하는 메서드 구현을 제공합니다.

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Counter : public IReadable
{
public:
	int value = 0;

	int read() const override
	{
		return value;
	}
};
```

`override`을 사용하여 인터페이스 메서드 구현을 명시적으로 만듭니다. 아직 구현되지 않은 순수 가상 메서드가 있는 클래스나 구조체는 구체적인 개체로 인스턴스화할 수 없습니다.

유형은 여러 인터페이스를 구현할 수 있습니다.

```VeroScript
interface IId
{
	virtual int id() const = 0;
};

interface IName
{
	virtual string name() const = 0;
};

class Person : public IId, public IName
{
public:
	int id() const override
	{
		return 42;
	}

	string name() const override
	{
		return "Ada";
	}
};
```

## 기본 구현

인터페이스 메소드는 기본 구현을 형성하는 본문을 제공할 수도 있습니다. 구현 형식은 해당 구현을 상속하거나 `override`을 사용하여 자체 버전을 제공할 수 있습니다.

```VeroScript
interface IDescribable
{
	virtual string name() const = 0;

	string describe() const
	{
		return "name=" + name();
	}
};
```

기본 메서드는 인터페이스 계약의 일부로 유지됩니다. 구현되지 않은 순수 가상 메서드만 구체적인 인스턴스화를 방지합니다.

## 인터페이스 상속

인터페이스는 하나 이상의 상위 인터페이스에서 상속할 수 있습니다.

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

interface IResource : IReadable
{
	virtual void close() = 0;
};
```

`IResource`을 구현하는 형식은 해당 형식과 상위 인터페이스에서 선언된 모든 순수 가상 메서드를 충족해야 합니다. 인터페이스 상속 목록의 모든 유형은 그 자체가 인터페이스여야 합니다.

## 인터페이스 값 및 변환

구조체를 인터페이스 유형으로 변환하면 인터페이스에 필요한 구조체 값 표현이 생성됩니다. 해당 인터페이스 값에 액세스해도 원래 구조체는 수정되지 않습니다. 클래스를 인터페이스로 변환하면 클래스 객체의 공유 ID가 유지됩니다.

```VeroScript
interface ICounter
{
	virtual void add(int amount) = 0;
};

struct ValueCounter : ICounter
{
	int value = 0;
	void add(int amount)
	{
		value += amount;
	}
};

class SharedCounter : ICounter
{
public:
	int value = 0;
	void add(int amount) override
	{
		value += amount;
	}
};
```

따라서 인터페이스 값을 복사하고 호출하는 것은 기본 구현에 따라 다릅니다. 즉, 구조체 인터페이스 값은 값 의미 체계를 따르는 반면, 클래스 인터페이스 값은 클래스 핸들 의미 체계를 따릅니다. 인터페이스 변수는 새로운 구현 객체를 생성하는 데 사용할 수 없습니다.

중첩 형식, 지역 선언, 접근 규칙 및 단계별 제네릭 인수는 [형식 범위](scope.md)를 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/interfaces.md) · [fr-FR](../../fr-FR/language-basics/interfaces.md) · [ja-JP](../../ja-JP/language-basics/interfaces.md) · [ko-KR](interfaces.md) · [zh-CN](../../zh-CN/language-basics/interfaces.md) · [zh-TW](../../zh-TW/language-basics/interfaces.md)
