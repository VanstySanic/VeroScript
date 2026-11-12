# 제어 흐름

제어 흐름 문은 문이 실행되는 순서를 결정합니다. VeroScript는 조건, 루프, `switch`, 점프 및 함수 반환을 지원합니다.

## `if`

`if`은 조건에 따라 블록을 선택합니다. `else if` 및 `else`는 선택 사항입니다.

```VeroScript
if (score >= 60)
{
	print("pass");
}
else if (score >= 0)
{
	print("fail");
}
else
{
	print("invalid score");
}
```

조건은 언어의 부울 규칙으로 평가할 수 있는 표현식이어야 합니다. 중괄호는 분기 경계를 명시적으로 만들기 때문에 단일 문에도 권장됩니다.

## `while`

`while`은 매 반복 전에 해당 조건을 확인합니다.

```VeroScript
int count = 3;
while (count > 0)
{
	print(count);
	count -= 1;
}
```

조건이 처음에 false이면 루프 본문이 실행되지 않습니다.

## `do ... while`

`do`는 조건을 검사하기 전에 본문을 한 번 실행합니다. 끝의 세미콜론은 필수입니다.

```VeroScript
int count = 0;
do
{
	count += 1;
}
while (count < 3);
```

조건은 `bool`이어야 합니다. 본문의 `continue`는 조건 검사로 이동하고, `break`는 루프를 종료합니다.

## `for`

기존 `for` 문에는 초기화 프로그램, 조건 및 반복 표현식이 포함되어 있습니다.

```VeroScript
for (int i = 0; i < 10; ++i)
{
	print(i);
}
```

이니셜라이저에서 선언된 변수의 범위는 `for` 문으로 지정됩니다. 조건과 반복 표현식은 생략될 수 있지만 루프에는 명확한 종료 조건이 있어야 합니다.

초기화 절과 반복 절에는 각각 쉼표로 구분된 표현식 목록을 사용할 수 있으며 왼쪽에서 오른쪽으로 평가됩니다. 이 구문은 `for` 절에서만 지원됩니다. VeroScript는 일반 쉼표 연산자를 제공하지 않습니다. 초기화 절에는 `int index = 0, end = 3` 같은 같은 형식의 선언 그룹도 사용할 수 있으며, 이름은 왼쪽에서 오른쪽으로 보이고 범위는 `for` 문으로 제한됩니다.

## 범위 기반 루프

범위 기반 루프는 `:`을 사용하여 배열 또는 컨테이너를 반복합니다.

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

참조 루프 변수를 사용하여 요소를 제자리에서 수정합니다.

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

맵 컨테이너는 키/값 루프 형식도 지원합니다. 정확한 형식은 컨테이너 참조를 참조하세요.

## `break` 및 `continue`

`break`는 가장 가까운 바깥쪽 루프 또는 `switch`를 종료합니다. `continue`는 가장 가까운 바깥쪽 루프의 다음 반복으로 이동하며, `switch`는 `continue`의 대상이 아닙니다.

```VeroScript
for (int value : values)
{
	if (value < 0)
	{
		continue;
	}
	if (value == 100)
	{
		break;
	}
	print(value);
}
```

## `switch`

`switch`은 표현식을 기반으로 `case`을 선택합니다. 현재 구현에서는 정수, 열거형, 문자열 및 와이드 문자열을 스위치 값으로 허용합니다.

```VeroScript
switch (state)
{
case 0:
	{
		print("off");
		break;
	}
case 1:
	{
		print("on");
		break;
	}
default:
	{
		print("unknown");
		break;
	}
}
```

VeroScript는 표시되지 않은 폴스루를 허용하지 않습니다. 실행이 다음 `case`로 계속되어야 하는 경우 명시적으로 `fallthrough`을 사용합니다.

```VeroScript
switch (value)
{
case 1:
	{
		print("one");
	}
	fallthrough;
case 2:
	{
		print("one or two");
		break;
	}
default:
	{
		break;
	}
}
```

`fallthrough;`는 해당 `case`에 직접 속하는 마지막 문장이어야 합니다. 분기 내용을 중괄호로 묶을 때는 이 문장을 블록 뒤에 두고 중첩 블록, 조건문, 반복문 안에 넣지 않습니다.

## 예외

VeroScript는 스크립트 예외와 엔진 또는 런타임 오류를 구분합니다. `throw expression;`은
`void`가 아닌 모든 스크립트 값을 던집니다. `try` 문은 함수 안에 있어야 하며 하나 이상의
`catch` 또는 `finally` 절을 포함해야 합니다.

```VeroScript
int main()
{
	try
	{
		throw "failed";
	}
	catch (const string& message)
	{
		print(message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

`catch` 절은 소스 순서대로 검사합니다. 형식이 지정된 처리기는 `catch (Type name)`,
`catch (const Type& name)`, `catch (Type& name)` 또는 `catch (ref Type name)`으로 쓸 수
있습니다. `catch (Type)`처럼 이름을 생략할 수도 있습니다. `catch (...)`는 모든 스크립트
예외를 받고 바인딩을 만들지 않으며 마지막 `catch` 절이어야 합니다. 형식이 지정된 처리기는
던져진 값의 형식과 일치하며, 클래스 또는 인터페이스에는 대입 가능한 파생 객체도 일치합니다.
처리기를 일치시키기 위해 숫자 변환을 수행하지 않습니다.

값으로 바인딩하면 복사본입니다. 참조 바인딩은 원래 값을 관찰하므로 쓰기 가능한 참조를
수정하면 전파되는 값도 바뀝니다. 식이 없는 `throw;`는 `catch` 처리기 안에서만 유효하며,
값 바인딩의 수정된 복사본이 아니라 원래 예외 객체를 다시 던집니다. 던져진 값은 일치하는
처리기를 찾을 때까지 호출 스택을 넘어 전파됩니다.

`finally`는 처리기 뒤에 둘 수 있고 `catch` 없이 사용할 수도 있습니다. 보호된 `try` 또는
`catch`를 정상적으로 끝내거나 `return`, 루프 전환, 스크립트 예외 전파 또는 런타임 오류 전파로
빠져나갈 때 실행됩니다. `return`, `goto`, 바깥 루프를 대상으로 하는 `break`/`continue`처럼
`finally` 밖으로 나가는 비국소 전환은 컴파일러가 거부합니다.

`finally` 안에서 반복문과 중첩 `try`/`catch`를 사용할 수 있습니다. 내부에서 잡은 예외는 대기 중인 바깥 반환값이나 예외를 유지합니다. 잡히지 않은 스크립트 예외가 `finally`를 벗어나면 대기 중인 전파를 대체하여 바깥 처리기로 전파됩니다. 정리가 소멸자를 호출하여 스크립트를 다시 실행하는 경우에도 같은 규칙을 적용합니다.

엔진/런타임 오류는 스크립트 값이 아니므로 `catch (...)`가 삼킬 수 없습니다. 실패한 `assert`도
이 런타임 오류에 해당하지만, 오류가 전파되는 동안에도 `finally`는 실행됩니다.

## `goto` 및 라벨

레이블은 이름과 콜론으로 구성됩니다. `goto`은 해당 레이블로 이동합니다.

```VeroScript
int main()
{
	goto done;

	print("not reached");

	done:
	print("done");
}
```

실제로 이점을 얻을 수 있는 로컬 제어 흐름을 위해 `goto`을 예약하세요. 과도한 점프는 함수를 분석하기 어렵게 만듭니다.

## `return`

`return`은 현재 기능을 종료합니다. `void`이 아닌 함수는 선언된 형식과 호환되는 값을 반환해야 합니다. `void` 함수는 표현식 없이 반환될 수 있습니다.

```VeroScript
int square(int value)
{
	return value * value;
}

void log_done()
{
	print("done");
	return;
}
```

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/control-flow.md) · [fr-FR](../../fr-FR/language-basics/control-flow.md) · [ja-JP](../../ja-JP/language-basics/control-flow.md) · [ko-KR](control-flow.md) · [zh-CN](../../zh-CN/language-basics/control-flow.md) · [zh-TW](../../zh-TW/language-basics/control-flow.md)
