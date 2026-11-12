# 03: 값, 조건 및 루프

이 튜토리얼은 작은 점수 계산기에 변수, 정적 유형, 분기 및 반복을 결합합니다. 목표는 문제를 상태, 결정, 반복 작업으로 나누는 방법을 배우는 것입니다.

## 1단계: 상태 준비

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	print("total={}", total);
	return 0;
}
```

`total` 및 `count`은 모두 `int`입니다. `i`은 `for` 루프 내부에만 존재합니다. 로컬 범위는 임시 상태가 관련 없는 코드로 누출되는 것을 방지합니다.

## 2단계: 결정 추가

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	if (total >= 15)
	{
		print("pass: {}", total);
	}
	else
	{
		print("retry: {}", total);
	}
	return 0;
}
```

`if`의 조건은 부울 결과를 생성해야 합니다. 비교 결과 `bool`이 생성됩니다. 조건으로 처리되는 정수에 의존하는 대신 명시적 비교를 사용합니다.

## 3단계: 데이터를 배열로 이동

```VeroScript
int main()
{
	int scores[4] = {72, 88, 91, 65};
	int total = 0;

	for (int i = 0; i < len(scores); i = i + 1)
	{
		total = total + scores[i];
	}

	double average = to_double(total) / len(scores);
	print("average={}", average);
	return 0;
}
```

`scores`은 고정 크기 기본 배열입니다. 해당 인덱스는 `0`에서 시작하고 `len(scores)`는 해당 길이를 반환합니다. 런타임 시 요소 수가 변경되어야 하는 경우 `array<int>`을 사용하세요.

## 4단계: 소수 리터럴 사용

`.` 바로 뒤에 10진 숫자가 오면 부동 소수점 리터럴의 정수부를 생략할 수 있습니다. 접미사는 저장되는 유형을 계속 결정합니다. `.5f`는 `float`이고 `.5`와 `.5e2`는 `double` 값입니다.

```VeroScript
int main()
{
	float ratio = .5f;
	double half = .5;
	double fifty = .5e2;

	print("ratio={}, half={}, fifty={}", ratio, half, fifty);
	return 0;
}
```

`0.5f`와 `.5f`는 같은 값과 유형입니다. 점 뒤에는 숫자를 써야 하며 단독 `.`은 계속 멤버 액세스에 사용됩니다.

## 5단계: 스크립트 예외 처리

스크립트가 설명하고 복구할 수 있는 실패에는 `throw`를 사용합니다. 예외 값은 함수 호출을
넘어 전파되어 일치하는 `catch`에서 처리할 수 있습니다. 작업이 성공하든 예외로 끝나든 실행해야
하는 정리 작업에는 `finally`를 사용합니다.

```VeroScript
void validate_score(int score)
{
	if (score < 0)
	{
		throw "score must not be negative";
	}
}

int main()
{
	try
	{
		validate_score(-1);
	}
	catch (const string& message)
	{
		print("handled: {}", message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

이 프로그램은 처리된 메시지를 출력한 다음 `cleanup`을 출력합니다. 예외 형식이 중요하면
형식이 지정된 처리기를 사용하고 `catch (...)`는 마지막 대체 처리기로만 사용하세요. 처리기
안의 식 없는 `throw;`는 원래 값을 다시 던집니다.

전체 예외 예제는 [`examples/exceptions.vs`](../../../../../VeroScript/Resources/Scripts/examples/exceptions.vs)를 참조하세요.

## 6단계: `assert`로 불변 조건 확인

`assert(condition[, message])`는 런타임 검사입니다. 조건은 `bool`이어야 하고 선택적
메시지는 `string`이어야 합니다.

```VeroScript
int main()
{
	int total = 15;
	assert(total >= 0, "total must be non-negative");
	print("ready");
	return 0;
}
```

조건이 참이면 출력 없이 계속합니다. 조건이 거짓이면 런타임 오류를 보고하고 현재 실행을
중지합니다. 이는 스크립트 예외가 아니므로 `catch (...)`로 처리할 수 없습니다.

## 사용한 제어 흐름 개념

- `if`은 조건에 따라 경로를 선택합니다.
- `for`은 명시적인 초기화, 조건 및 증분을 사용하여 작업을 반복합니다.
- `return`는 결과를 사용하여 함수를 종료합니다. 다음 튜토리얼에서는 이를 많이 사용합니다.

- `try`/`catch`는 `throw`가 던진 스크립트 값을 처리하고 `finally`는 정리 작업을 수행합니다.
- `assert`는 부울 불변 조건을 확인하고 실패하면 런타임 오류를 보고합니다.

## 연습

1. 최고점과 최저점을 계산합니다.
2. `60`보다 크거나 같은 점수만 계산합니다.
3. 고정 배열을 `array<int>`로 교체하고 범위 기반 반복을 사용합니다.
4. `0.5`와 같은 소수 리터럴을 `.5`로 바꾸고, `float` 값이 필요한 곳에는 `.5f`를 사용합니다.
5. 음수 점수를 거부하는 함수에 형식이 지정된 `catch`와 `finally` 블록을 추가합니다.
6. 점수 계산의 불변 조건에 메시지가 있는 `assert`를 추가합니다.

전체 배열 예제는 [`examples/arrays.vs`](../../../../../VeroScript/Resources/Scripts/examples/arrays.vs)을 참조하세요. 참조 페이지는 [변수](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/variables.md), [제어 흐름](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/control-flow.md) 및 [배열](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/arrays.md)입니다.

다음: [함수 및 컬렉션](04-functions-and-collections.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/language/03-values-and-control-flow.md) · [fr-FR](../../fr-FR/language/03-values-and-control-flow.md) · [ja-JP](../../ja-JP/language/03-values-and-control-flow.md) · [ko-KR](03-values-and-control-flow.md) · [zh-CN](../../zh-CN/language/03-values-and-control-flow.md) · [zh-TW](../../zh-TW/language/03-values-and-control-flow.md)
