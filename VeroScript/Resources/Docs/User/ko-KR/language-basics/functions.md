# 기능

함수는 매개변수 목록과 반환 유형이 있는 명명된 호출 가능 선언입니다. 함수는 모듈 범위에서 정의하거나 구조체, 클래스 또는 인터페이스의 멤버로 정의할 수 있습니다.

## 함수 선언

함수 선언에는 반환 유형, 이름, 매개변수 목록 및 본문이 포함됩니다.

```VeroScript
int add(int left, int right)
{
	return left + right;
}
```

`void`은 함수가 값을 반환하지 않음을 나타냅니다.

```VeroScript
void print_message(string message)
{
	print(message);
}
```

## `main` 진입점

애플리케이션 진입점은 `int` 또는 `void`을 반환할 수 있으며 최대 하나의 `string[]` 매개 변수를 사용할 수 있습니다. `int` 반환 값은 프로세스 종료 코드가 됩니다. `void`는 `0`로 처리됩니다. 항목 매개변수는 참조, 가변 매개변수 또는 기본 매개변수일 수 없습니다.

```VeroScript
int main(string[] args)
{
	if (len(args) == 0)
	{
		return 2;
	}
	print(args[0]);
	return 0;
}
```

입력 모듈만 `main`을 제공할 수 있습니다. 라이브러리 모듈은 애플리케이션 진입점을 정의할 수 없습니다.

`int main`이 정상적으로 반환되거나 명시적인 반환 없이 닫는 중괄호에 도달하면 결과 코드는 반환된 값 또는 `0`입니다. `exit(n)`는 `main` 반환 값보다 우선하며 `exit(0)`는 여전히 스크립트가 명시적으로 종료를 요청했다고 기록합니다. 엔진 API의 경우 0이 아닌 `ExitCode` 자체가 엔진이 실패했다는 의미는 아닙니다. 호스트는 `bSuccess`, `ExitCode` 및 `bExitRequested`을 함께 검사해야 합니다.

함수 본문의 매개변수와 지역 변수는 일반 함수 및 블록 범위 규칙을 따릅니다.

## 매개변수 및 반환 값

인수는 선언 순서에 따라 매개변수와 일치됩니다. 해당 유형은 언어의 변환 규칙을 충족해야 합니다.

```VeroScript
double average(double total, int count)
{
	return total / count;
}
```

`return` 표현식의 유형은 함수의 반환 유형과 호환되어야 합니다. `void` 함수는 표현식 없이 `return`를 사용하여 실행을 조기에 종료할 수 있습니다.

## 기본 인수

매개변수는 기본값을 제공할 수 있습니다. 호출자가 해당 인수를 생략하면 선언된 기본 표현식이 사용됩니다.

```VeroScript
int clamp(int value, int minimum = 0, int maximum = 100)
{
	if (value < minimum)
	{
		return minimum;
	}
	if (value > maximum)
	{
		return maximum;
	}
	return value;
}

int main()
{
	print(clamp(120));
	print(clamp(-1, -10, 10));
}
```

기본 매개변수는 매개변수 목록에서 후행 시퀀스를 형성해야 합니다. 선택 매개변수 뒤에 필수 매개변수를 배치하지 마세요.

## 함수 오버로딩

동일한 이름을 가진 여러 함수는 매개변수 목록으로 구분될 때 하나의 범위에서 선언될 수 있습니다.

```VeroScript
int abs_value(int value)
{
	return value < 0 ? -value : value;
}

double abs_value(double value)
{
	return value < 0.0 ? -value : value;
}
```

함수가 호출되면 컴파일러는 이름, 인수 개수, 인수 유형 및 사용 가능한 변환을 사용하여 오버로드를 선택합니다. 여러 후보가 동일하게 일치하면 과부하 모호함으로 인해 컴파일이 실패합니다.

## 참조 매개변수

참조 매개변수를 사용하면 함수가 호출자가 소유한 객체를 수정할 수 있습니다. 공개 예제에서는 `T&`을 선호합니다. `ref T`은 동등한 키워드 스타일 철자법입니다.

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
}
```

인수는 호출 사이트에서 일치하는 `&` 또는 `ref` 마커를 사용합니다. VeroScript에는 범용 스크립트 포인터 유형이 없으며 참조는 포인터 연산을 지원하지 않습니다.

## 가변 매개변수

함수는 `T... name`을 사용하여 하나의 요소 유형으로 가변 인수 시퀀스를 선언할 수 있습니다.

```VeroScript
void print_all(string... values)
{
	for (string value : values)
	{
		print(value);
	}
}
```

함수 내에서 가변 매개변수는 해당 요소 유형의 배열로 작동합니다. 가변 매개변수는 마지막에 와야 합니다.

## 함수 값과 람다

함수는 값으로 전달될 수 있으며 람다는 익명 함수를 만들 수 있습니다. 함수 유형은 매개변수 목록과 반환 유형을 설명합니다.

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

int main()
{
	function(int) -> int increment = function(int value) -> int => value + 1;
	print(apply(increment, 41));
}
```

`fn`은 `function`의 약어 철자입니다. 함수 값과 람다, 일반 함수 및 멤버 함수에 대한 캡처 규칙은 전용 참조에서 다룹니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/functions.md) · [fr-FR](../../fr-FR/language-basics/functions.md) · [ja-JP](../../ja-JP/language-basics/functions.md) · [ko-KR](functions.md) · [zh-CN](../../zh-CN/language-basics/functions.md) · [zh-TW](../../zh-TW/language-basics/functions.md)
