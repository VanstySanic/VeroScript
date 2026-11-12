# 06: 참조, 참조 반환 및 람다 캡처

이 튜토리얼에서는 실행 가능한 여러 작은 프로그램을 사용하여 기존 값에 대한 별칭과 외부 상태를 공유하는 클로저 간의 차이점을 설명합니다. 참조는 기존 개체나 요소를 수정하는 데 유용합니다. 람다 캡처는 클로저가 둘러싸는 바인딩을 저장하는 방법을 결정합니다.

## 1단계: 로컬 참조 별칭 만들기

```VeroScript
int main()
{
	int score = 40;
	int& alias = score;
	alias += 2;

	print("score={}, alias={}", score, alias);
	return 0;
}
```

`alias`은 두 번째 정수가 아닙니다. `score`에 대한 참조 별칭이므로 `alias`를 통해 작성하면 `score`이 변경됩니다. 참조는 초기화 후에 다른 정수로 리바운드될 수 없습니다. 독립된 값이 필요할 때는 일반 변수를 사용하세요.

## 2단계: 함수에 대한 참조 전달

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	int value = 40;
	add_one(&value);
	print(value);
	return 0;
}
```

`int&`은 쓰기 가능한 참조 매개변수를 선언합니다. 호출에서는 `&value`을 사용하여 앨리어싱 작업을 명시적으로 만듭니다. 읽기 전용 액세스의 경우 `const int&`를 사용하십시오. 함수가 해당 참조를 통해 객체를 변경하는 것을 방지하면서 복사를 방지합니다.

## 3단계: 쓰기 가능한 참조 반환

참조는 함수 반환 값일 수도 있지만 반환된 개체는 호출이 완료된 후에도 여전히 존재해야 합니다.

```VeroScript
int counter = 1;

int& counter_ref()
{
	return counter;
}

int main()
{
	counter_ref() += 4;
	int& alias = counter_ref();
	alias += 1;
	print(counter);
	return 0;
}
```

이는 전역 `counter`을 반환하므로 호출자는 결과를 통해 이를 계속 수정할 수 있습니다. 일반 지역 변수에 대한 참조를 반환하지 마세요. 해당 지역은 함수가 반환될 때 해당 범위를 벗어났습니다. 컨테이너 요소와 객체 멤버는 소유자가 유효한 동안 참조로 반환될 수도 있습니다.

## 4단계: 참조 반복을 통해 컨테이너 수정

```VeroScript
int main()
{
	array<int> values = array<int>();
	values.push(1);
	values.push(2);

	for (int& value : values)
	{
		value += 10;
	}

	print(values[0]);
	print(values[1]);
	return 0;
}
```

`for (int& value : values)`은 `value`을 각 배열 요소의 별칭으로 지정하므로 루프의 할당이 배열에 다시 기록됩니다. `&`가 없으면 루프 변수는 값을 수신하고 해당 변경 사항은 다시 기록되지 않습니다. 인덱스로 요소를 전달하는 경우 동일한 명시적 형식(예: `add_one(&values[0])`)을 사용합니다.

## 5단계: 람다가 외부 상태를 캡처하도록 합니다.

```VeroScript
function() -> int make_counter()
{
	int running = 40;
	return [&]() -> int
	{
		running++;
		return running;
	};
}

int main()
{
	function() -> int next = make_counter();
	print(next());
	print(next());
	return 0;
}
```

`[&]`은 사용된 둘러싸는 로컬을 참조로 캡처합니다. VeroScript는 캡처된 로컬 바인딩을 안전한 공유 저장소로 승격하므로 `make_counter`이 반환된 후에도 `next`는 여전히 `running`에 안전하게 액세스하고 수정할 수 있습니다.

하나의 변수에만 공유 변형이 필요한 경우 `[&running]`을 작성하세요. 혼합 정책의 경우 `[=, &running]`을 사용하면 다른 변수는 값으로 캡처되고 `running`만 참조로 캡처됩니다.

## 6단계: 멤버 함수 수신자 캡처

수신자 캡처는 해당 형식 자체의 의미 체계를 따릅니다.

```VeroScript
class Counter
{
public:
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

struct Snapshot
{
	int value;

	function() -> int getter()
	{
		return function() -> int => value;
	}
};

int main()
{
	Counter counter = Counter();
	counter.value = 1;
	function() -> int live = counter.getter();
	counter.value = 2;

	Snapshot snapshot = Snapshot();
	snapshot.value = 3;
	function() -> int copied = snapshot.getter();
	snapshot.value = 4;

	print(live());
	print(copied());
	return 0;
}
```

클래스 클로저는 객체를 유지하므로 `2`를 출력하고, 구조체 클로저는 값 스냅샷을
유지하므로 `3`을 출력합니다. 캡처가 없는 `[]` 람다는 `this`에 액세스할 수 없습니다.

## 둘 중 선택하기

- 독립된 복사본이 필요한 경우 일반 값 또는 값 캡처를 사용합니다.
- 함수가 호출자가 소유한 값을 수정해야 하는 경우 쓰기 가능한 참조 매개변수를 사용합니다.
- 루프가 컨테이너 요소를 제자리에서 업데이트해야 하는 경우 참조 범위 변수를 사용합니다.
- 반환된 클로저가 변경 가능한 외부 상태를 계속 공유해야 하는 경우 참조 캡처를 사용하세요.
- 큰 개체를 복사하지 않고 읽기 전용 액세스를 위해 `const T&`을 고려하는 동시에 참조된 개체가 충분히 오랫동안 활성 상태를 유지하도록 합니다.

## 연습

1. `swap_values(int& left, int& right)`을 작성하고 `swap_values(&a, &b)`로 호출합니다.
2. `array<int>`에 대한 참조 범위 루프를 사용하여 모든 요소에 `2`을 곱합니다.
3. 값이 캡처된 `step`을 `make_counter`에 추가하고 `[=, &running]` 정책을 구현합니다.
4. 일반 로컬에 대한 참조를 반환하고 `--check` 진단을 읽고 전역 또는 여전히 유효한 멤버를 반환하도록 변경합니다.
5. 클로저를 만든 후 `Counter`의 필드를 변경하고 `Snapshot`의 결과와 비교합니다.

전체 규칙은 [참조 및 참조 의미 체계](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/references.md) 및 [람다 및 함수 값](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/lambdas.md)을 참조하세요. 실행 가능한 예는 [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs), [`examples/reference_returns.vs`](../../../../../VeroScript/Resources/Scripts/examples/reference_returns.vs) 및 [`examples/lambda.vs`](../../../../../VeroScript/Resources/Scripts/examples/lambda.vs)입니다.

다음: [모듈, 가져오기 및 전처리기](07-modules-and-preprocessor.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/language/06-references-and-captures.md) · [fr-FR](../../fr-FR/language/06-references-and-captures.md) · [ja-JP](../../ja-JP/language/06-references-and-captures.md) · [ko-KR](06-references-and-captures.md) · [zh-CN](../../zh-CN/language/06-references-and-captures.md) · [zh-TW](../../zh-TW/language/06-references-and-captures.md)
