# 04: 함수와 컬렉션

`main`이 커지기 시작하면 명확한 책임을 갖고 코드를 추출하는 것이 일반적으로 주석을 추가하는 것보다 더 유용합니다. 이 자습서에서는 합계 함수를 작성하고 `array<int>`에 가변 개수의 값을 저장합니다.

## 1단계: 합계 함수 작성

```VeroScript
int sum(array<int> values)
{
	int result = 0;
	for (int value : values)
	{
		result = result + value;
	}
	return result;
}

int main()
{
	array<int> values = array<int>();
	values.push(10);
	values.push(20);
	values.push(30);

	print("sum={}", sum(values));
	return 0;
}
```

함수 선언은 반환 유형, 이름 및 매개변수를 제공합니다. `sum`은 `array<int>`을 수신하고 `int`를 반환합니다.

## 2단계: 컬렉션을 늘리세요.

`array<int>()`은 빈 동적 배열을 생성하고 `push`은 끝에 요소를 추가합니다. 중괄호 초기화를 사용할 수도 있습니다.

```VeroScript
int main()
{
	array<int> values = array<int>{10, 20, 30};

	values.push(40);
	for (int& value : values)
	{
		value = value + 1;
	}

	print("size={}", len(values));
	print("first={}", values[0]);
	return 0;
}
```

일반 범위 루프 `for (int value : values)`은 요소 복사본을 받습니다. `&`을 추가하면 요소에 별칭이 생성되므로 변경 사항이 컨테이너에 다시 기록됩니다.

## 3단계: 기본 매개변수 사용

```VeroScript
void print_values(array<int> values, string label = "values")
{
	print(label);
	for (int value : values)
	{
		print(value);
	}
}

int main()
{
	array<int> values = array<int>{1, 2, 3};
	print_values(values);
	print_values(values, "numbers");
	return 0;
}
```

기본 매개변수를 사용하면 호출자가 공통 구성을 생략할 수 있습니다. 기본값은 매개변수 유형과 호환되어야 합니다.

## 컨테이너 선택

정렬되고 확장 가능한 데이터를 얻으려면 `array<T>`으로 시작하세요. 키 조회를 위해서는 `map<K, V>`을 고려하고 고유성을 위해서는 `set<T>`를 고려하십시오. 액세스 패턴에서 요구하기 전에는 더 복잡한 컨테이너를 추가하지 마세요.

## 연습

1. 빈 배열에 대해 `0.0`을 반환하는 `average` 함수를 작성합니다.
2. 별칭 범위 루프를 사용하여 모든 음수 값을 `0`로 변경하는 함수를 작성합니다.
3. 점수를 `map<string, int>`에 저장하고 이름 하나에 대한 점수를 인쇄합니다.

[`examples/dynamic_array.vs`](../../../../../VeroScript/Resources/Scripts/examples/dynamic_array.vs) 및 [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs)을 참조하세요. [데이터 유형](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/data-types.md) 페이지에는 사용 가능한 컨테이너가 요약되어 있습니다.

다음: [구조체, 클래스 및 인터페이스](05-structs-classes-and-interfaces.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/language/04-functions-and-collections.md) · [fr-FR](../../fr-FR/language/04-functions-and-collections.md) · [ja-JP](../../ja-JP/language/04-functions-and-collections.md) · [ko-KR](04-functions-and-collections.md) · [zh-CN](../../zh-CN/language/04-functions-and-collections.md) · [zh-TW](../../zh-TW/language/04-functions-and-collections.md)
