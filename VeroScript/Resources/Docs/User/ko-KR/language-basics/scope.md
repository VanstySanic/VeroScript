# 범위

범위는 소스 코드에서 이름이 유효한 위치를 결정합니다. 일반적인 VeroScript 범위에는 모듈, 네임스페이스, 함수, 블록 및 멤버 범위가 포함됩니다.

## 블록 범위

함수 또는 제어 흐름 문 내부의 중괄호는 블록을 만듭니다. 지역 변수는 선언부터 둘러싸는 블록 끝까지 유효합니다.

```VeroScript
int main()
{
	int outer = 1;

	if (outer > 0)
	{
		int inner = 2;
		print(outer);
		print(inner);
	}

	// inner is out of scope here.
	print(outer);
}
```

내부 블록은 외부 블록에 이미 선언된 이름에 액세스할 수 있습니다. 외부 블록은 내부 블록에 선언된 지역 변수에 접근할 수 없습니다.

## 함수 범위

함수 매개변수와 지역 변수는 함수의 범위에 속합니다. 다른 함수에서는 동일한 로컬 이름을 사용할 수 있습니다. 각 선언은 별개입니다.

```VeroScript
void first()
{
	int value = 1;
	print(value);
}

void second()
{
	int value = 2;
	print(value);
}
```

## 모듈 범위

최상위 수준에서 선언된 변수, 함수 및 유형은 현재 모듈에 속합니다. 내보내지 않은 선언은 일반적으로 모듈 내부에서만 사용할 수 있습니다. 내보낸 선언은 가져오기 규칙에 따라 다른 모듈에서 사용될 수 있습니다.

## 네임스페이스 범위

네임스페이스는 별도의 이름 범위에 선언을 배치합니다.

```VeroScript
namespace math
{
	int add(int left, int right)
	{
		return left + right;
	}
}

int main()
{
	print(math::add(1, 2));
}
```

네임스페이스 멤버는 `.` 또는 `::`을 사용하여 액세스할 수 있습니다. 네임스페이스는 모듈을 대체하지 않습니다. 모듈은 파일 및 패키지 가져오기를 구성하는 반면 네임스페이스는 선언 이름을 구성합니다.

### 전역 범위 한정

앞의 `::`는 현재 네임스페이스, 로컬 별칭, `using namespace`가 아니라 전역 범위에서 검색하도록 강제합니다. 루트 선언을 의도할 때 `::Name`을 사용하세요.

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

또한 루트 수준의 형식 또는 네임스페이스를 한정할 수 있으며 `using namespace ::Name`에서도 사용할 수 있습니다.

## 회원 범위

구조체, 클래스 또는 인터페이스의 필드와 메서드는 해당 형식의 멤버 범위에 속합니다. 액세스는 `public`, `protected` 및 `private`에 의해 제한됩니다.

```VeroScript
class Counter
{
private:
	int value = 0;

public:
	int get()
	{
		return value;
	}
};
```

멤버 함수는 `this`을 사용하여 현재 개체에 액세스할 수 있습니다. 정적 멤버는 특정 개체 인스턴스에 의존하지 않으며 다른 액세스 규칙을 갖습니다.

## 이름 숨기기

내부 범위는 외부 범위와 동일한 이름을 선언할 수 있습니다. 그러면 내부 선언이 외부 선언을 숨깁니다. 이름 조회를 읽기 어렵게 만들므로 불필요한 섀도잉을 피하세요.


## 중첩 타입과 지역 타입

타입 안에 class, struct, interface, enum 및 타입 별칭을 선언할 수 있습니다. `Outer::Inner`로 사용하고 제네릭 인수는 각 구간에 지정합니다(예: `Outer<int>::Inner<string>`). 공개 중첩 타입은 내보낸 바깥 타입을 통해 사용할 수 있습니다. private/protected 경로와 공개 별칭에는 일반 접근 검사가 적용됩니다.

실행 블록의 지역 타입 이름은 선언부터 블록 끝까지 보이며 안쪽 블록에서 가릴 수 있습니다. 보이는 타입, 별칭, 열거 상수, 제네릭 매개변수를 사용하지만 바깥 변수나 바깥 `this`를 캡처하지 않습니다. 인스턴스 멤버를 사용하려면 객체를 명시적으로 전달합니다. 정적 저장소는 구체 타입과 실행 인스턴스별로 유지됩니다.

```VeroScript
class Outer<T>
{
public:
	struct Inner<U>
	{
		T key;
		U value;
	};
};

void main()
{
	Outer<int>::Inner<string> item = Outer<int>::Inner<string>();
	item.key = 7;
	item.value = "saved";
	struct Result
	{
		int count = 1;
	};
	Result result = Result();
	print("{} {} {}", item.key, item.value, result.count);
}
```

지역 타입이 아닌 중첩 메서드는 `T Outer<T>::Inner<U>::read() { ... }`로 정의할 수 있으며 선언을 가져온 다른 파일에서도 정의할 수 있습니다. 지역 메서드는 타입 안 또는 처리기가 생성한 본문에서 정의합니다. 불완전 타입의 독립 전방 선언, 타입 본문 다시 열기, 지역 메서드의 파일 간 정의는 지원하지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/scope.md) · [fr-FR](../../fr-FR/language-basics/scope.md) · [ja-JP](../../ja-JP/language-basics/scope.md) · [ko-KR](scope.md) · [zh-CN](../../zh-CN/language-basics/scope.md) · [zh-TW](../../zh-TW/language-basics/scope.md)
