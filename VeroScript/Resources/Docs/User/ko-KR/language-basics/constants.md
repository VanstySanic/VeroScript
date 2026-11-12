# 상수

상수는 선언된 이름을 통해 수정할 수 없는 변수 또는 멤버입니다. VeroScript는 상수로 `const`을 사용합니다.

`const`는 읽기 전용을 지정하며 컴파일 시간 초기화를 요구하지 않습니다. 지원되는 스칼라 값을 컴파일 중에 확정하려면 [constexpr](../language-reference/constexpr.md)를 사용하세요.

## 상수 변수

`const`은 로컬 및 모듈 수준 변수에 사용할 수 있습니다. 상수는 선언 시 초기화되어야 합니다.

```VeroScript
const int max_count = 100;

int main()
{
	const string product_name = "VeroScript";
	print(product_name);
}
```

초기화 후에는 해당 이름을 통해 상수를 할당할 수 없습니다.

```VeroScript
const int limit = 10;
// limit = 20; // Compile error.
```

상수에는 여전히 선언에 작성된 유형이 있습니다. `const`은 값을 변환하거나 해당 범위를 변경하지 않습니다.

## 상수 멤버

구조체나 클래스는 상수 필드를 선언할 수 있습니다.

```VeroScript
class Settings
{
public:
	const int version = 1;
};
```

선언 이니셜라이저가 없는 비정적 필드는 생성자 초기화 목록에서 초기화할 수 있습니다.

```VeroScript
class Settings
{
public:
	const int version;

	Settings(int initial_version)
		: version(initial_version)
	{
	}
};
```

초기화 목록에는 현재 형식이 직접 선언한 인스턴스 필드만 지정할 수 있습니다. 생성자 초기화 목록의 직접 기본 형식 항목이 먼저 실행되고, 그다음 목록의 필드 항목이 필드 선언 순서대로 실행된 후 생성자 본문이 실행됩니다. 선언 이니셜라이저가 있는 필드는 이 목록에 다시 나타날 수 없으며 같은 필드를 두 번 지정할 수도 없습니다. 생성자 본문에서의 쓰기는 일반 대입이므로 `const` 필드를 수정할 수 없습니다. 선언 이니셜라이저와 목록에서 생략된 필드에는 기존 기본 구성 규칙이 계속 적용됩니다.

## `const` 멤버 함수

멤버 함수는 매개 변수 목록 뒤에 `const`을 사용하여 현재 개체의 변경 가능 상태를 수정할 수 없음을 나타낼 수 있습니다.

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
```

Const 멤버 함수는 쿼리에 적합합니다. 멤버 함수를 호출할 수 있는지 여부는 개체의 상수와 함수의 `const` 한정자에 따라 달라집니다.

### `mutable` 예외

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

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/constants.md) · [fr-FR](../../fr-FR/language-basics/constants.md) · [ja-JP](../../ja-JP/language-basics/constants.md) · [ko-KR](constants.md) · [zh-CN](../../zh-CN/language-basics/constants.md) · [zh-TW](../../zh-TW/language-basics/constants.md)
