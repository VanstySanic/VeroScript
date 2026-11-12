# 람다 및 함수 값

람다는 저장, 전달 및 호출할 수 있는 함수 값입니다. 함수 유형은 `function` 철자를 사용합니다. `fn`은 해당 압축 별칭입니다.

## 함수 유형

함수 유형은 매개변수 목록과 반환 유형을 결합합니다. 더 긴 선언을 계속 읽을 수 있도록 공개 코드에서 화살표 주위에 공백을 유지하십시오.

```VeroScript
function(int) -> int increment = function(int value) -> int => value + 1;
function() -> string get_name = function() -> string => "VeroScript";
function(int&, int) -> void mutate = function(int& value, int amount) -> void
{
	value += amount;
};
```

함수 값은 매개변수, 반환 값 또는 지역 변수일 수 있습니다.

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

function(int) -> int double_value = function(int x) -> int => x * 2;
int result = apply(double_value, 21);
```

`fn(int) -> int`과 `function(int) -> int`은 동일합니다. 공개 문서와 더 긴 선언에서는 일반적으로 전체 `function` 철자를 사용합니다.

## 람다 표현식

람다는 표현식 본문이나 블록 본문을 사용할 수 있습니다.

```VeroScript
function(int) -> int increment = function(int x) -> int => x + 1;

function(int) -> int square = function(int x) -> int
{
	return x * x;
};
```

일반 함수에 사용되는 것과 동일한 괄호 구문을 사용하여 함수 값을 호출합니다.

```VeroScript
int a = increment(41);
int b = square(5);
```

함수 유형에 참조 매개변수가 있는 경우 호출에서는 명시적인 참조 마커를 사용해야 합니다. 공개 예제에서는 `T&` 및 `&expression`을 선호합니다. `ref T` 및 `ref expression`은 계속 지원되는 철자법입니다.

```VeroScript
void add_to(int& value, int amount)
{
	value = value + amount;
}

function(int&, int) -> void mutate = add_to;
int value = 40;
mutate(&value, 2);
```

## 가치 포착

람다는 바깥쪽 범위에서 지역 변수를 캡처할 수 있습니다. 일반 람다 및 `[=]` 요청 값 캡처. 명시적 목록은 나열된 이름만 캡처합니다.

```VeroScript
int base = 40;

function(int) -> int add = function(int x) -> int => x + base;
function(int) -> int add_explicit = [base](int x) -> int => x + base;
```

값 캡처는 클로저 생성 시 초기화되며 기본적으로 읽기 전용입니다. 매개변수 목록 뒤에 `mutable`을 쓰면 소유한 캡처를 수정하고 다음 호출에도 유지할 수 있습니다. 함수 값 복사는 환경을 공유하고 생성 식을 다시 평가하면 독립 환경을 만듭니다. 일반 캡처의 const 원본은 계속 읽기 전용입니다.

VM, Mixed, JIT 및 Core 기반 AOT는 실행 서비스를 공유합니다. 독립 C++/C# 소스 출력은 값 캡처와 스칼라 지역 static을 지원합니다. 참조 초기화 캡처, 복잡한 static 객체, 바깥 static 참조에는 Core 경로가 필요합니다. Python 소스 출력은 람다와 지역 static을 지원하지 않습니다.

캡처나 수신 객체를 소유하는 람다가 참조를 반환하는 경우에도 독립 C++ 출력에는 Core AOT가 필요합니다. 네이티브 참조는 함수 값이 해제된 뒤 클로저의 저장 공간을 유지할 수 없습니다.

빈 캡처 목록 `[]`은 둘러싸는 로컬을 캡처하지 않고 일반 이름 조회를 통해 전역 이름을 계속 사용할 수 있습니다.

```VeroScript
function(int) -> int increment = [](int x) -> int => x + 1;
```

## 참조 캡처

VeroScript는 참조 캡처를 지원합니다. `[&]`은 참조로 사용하는 바깥쪽 지역 변수를 캡처하고, `[&name]`은 참조로 명명된 변수만 캡처합니다. 기본 캡처는 명시적 값 캡처와 결합될 수 있습니다.

```VeroScript
int running = 40;
function() -> int next = [&]() -> int
{
	running++;
	return running;
};

print(next());
print(running);

int step = 2;
function() -> int advance = [=, &running]() -> int
{
	running += step;
	return running;
};
```

사용 가능한 캡처 형식은 다음과 같습니다.

- `[&]`: 참조로 로컬을 둘러싸는 데 사용된 캡처.
- `[&name]`: 참조로 `name`를 캡처합니다.
- `[=]` 또는 일반 `function(...)` 람다: 사용된 둘러싸는 로컬을 값으로 캡처합니다.
- `[=, &name]`: `name`을 제외하고 기본적으로 값으로 캡처합니다.
- `[&, name]`: `name`을 제외하고 기본적으로 참조로 캡처합니다.
- `[]`: 둘러싸는 로컬을 캡처하지 않습니다.

참조 캡처된 로컬은 안전한 공유 스토리지로 승격됩니다. 따라서 클로저는 매달린 스택 슬롯을 유지하지 않고도 이를 생성한 함수보다 오래 지속될 수 있습니다. 둘러싸는 범위, 동일한 바인딩에 대한 또 다른 클로저 및 현재 클로저는 동일한 변경 가능 상태를 관찰합니다. 바깥쪽 바인딩이 const인 경우 참조 캡처는 읽기 전용 특성을 유지합니다.

명시적 캡처 목록의 이름은 허용된 엔클로징 바인딩을 참조해야 합니다. 캡처는 이미 기본 캡처로 처리된 양식을 중복적으로 반복할 수 없습니다. 빈 캡처 목록은 주변 로컬에 액세스할 수 없습니다. 전역 및 개체 멤버는 자체 이름 조회의 대상이 되며 의미 체계를 처리합니다.

## 멤버 함수에서 람다 반환하기

멤버 함수는 현재 개체 상태를 캡처하는 함수 값을 반환할 수 있습니다.

```VeroScript
class Counter
{
public:
	int value;

	Counter(int initial)
	{
		value = initial;
	}

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

Counter counter = Counter(42);
function() -> int get = counter.getter();
int current = get();
```

여기서 `value`는 범위를 벗어난 일반 로컬이 아닌 개체 멤버입니다. 클로저는 여전히 클래스 핸들 및 객체 수명 의미 체계를 따릅니다. 참조 캡처는 임의의 개체가 자동으로 값으로 복사되는지 여부가 아니라 바인딩에 액세스하는 방법을 변경합니다.

람다가 수신자 자체를 사용하면 수신자는 해당 형식의 의미 체계를 따릅니다.
클래스 수신자는 공유 핸들로 유지되므로 이후 필드 변경을 관찰합니다. 구조체
수신자는 값 스냅샷으로 캡처됩니다. 중첩 람다는 같은 규칙에 따라 이미 캡처된
수신자를 유지하거나 복사합니다. 캡처 없는 람다는 `this`를 사용할 수 없습니다.

## 참고문헌과의 관계

참조 매개변수, 참조 반환 및 안전한 참조 소스에 대한 호출 규칙은 [참조 및 참조 의미 체계](references.md)에서 다룹니다. 참조 캡처는 폐쇄 규칙이며 일반 값 캡처로 대체될 수 없습니다.

전체 예제는 [예제 인덱스](../examples.md)의 Lambda 항목을 참조하세요.

## 초기화 캡처, mutable 및 지역 static

`auto`를 사용할 때는 `() mutable -> void { ... }`처럼 Lambda의 반환형을 명시해야 합니다. 대상에 `function() -> void`와 같은 함수형 문맥이 있으면 Lambda의 반환형 화살표를 생략할 수 있습니다. 기존 문맥 기반 형식 추론 규칙을 따르며, `mutable` 자체가 본문에서 반환형을 추론하지는 않습니다.

지역 정적 상수에서 `static const int value = 42;`와 `const static int value = 42;`는 같은 의미입니다. 일관된 표기를 위해 `static const`를 권장합니다.

```VeroScript
function() -> int make_counter()
{
	return [count = 0]() mutable -> int { return ++count; };
}

int remember_first(int seed)
{
	static int value = seed;
	return ++value;
}

void capture_example()
{
	auto next = make_counter();
	auto alias = next;
	assert(next() == 1 && alias() == 2);
	int total = 0;
	auto advance = [&target = total, step = 2]() -> int { target += step;
		return target; };
	assert(advance() == 2 && total == 2);
	assert(remember_first(10) == 11 && remember_first(99) == 12);
}
```

초기화 캡처의 오른쪽 식은 바깥 범위에서 왼쪽부터 한 번 평가되며 새 이름은 본문에서만 보입니다. `[&name = expression]`에는 보존 가능한 lvalue가 필요하고 const를 유지합니다. 전역 및 타입·지역 static은 직접 참조하며 스냅샷에는 초기화 캡처를 사용합니다.

지역 static은 선언에 처음 도달할 때 초기화되고 후속 호출에서 공유됩니다. 같은 람다 식의 클로저도 함수 static을 공유합니다. 실패 후 정리가 성공하면 재시도할 수 있고 재귀 초기화는 오류입니다. 프로그램 상태, Session, worker는 격리되며 상태를 지우면 다시 시작합니다. static 참조 변수는 지원하지 않습니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/lambdas.md) · [fr-FR](../../fr-FR/language-basics/lambdas.md) · [ja-JP](../../ja-JP/language-basics/lambdas.md) · [ko-KR](lambdas.md) · [zh-CN](../../zh-CN/language-basics/lambdas.md) · [zh-TW](../../zh-TW/language-basics/lambdas.md)
