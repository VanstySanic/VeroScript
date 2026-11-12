# 과부하 해결

여러 함수, 메서드 또는 연산자가 하나의 범위에서 이름을 공유하는 경우 컴파일러는 호출 컨텍스트에서 호출 가능한 후보 하나를 선택합니다.

## 후보 함수

과부하 해결은 먼저 이름과 호출 형식으로 후보를 수집한 후 다음을 확인합니다.

- 인수 개수가 일치하는지 여부
- 인수 유형이 매개변수 변환 규칙을 만족하는지 여부
- `ref`, `const ref` 또는 `&` 마커가 선언과 일치하는지 여부
- 제네릭 형식 인수를 추론할 수 있고 모든 `where` 제약 조건이 유지되는지 여부
- 기본 인수가 생략된 후행 위치를 채울 수 있는지 여부입니다.

반환 유형은 일반적으로 동일한 이름의 오버로드를 구별하는 데 충분하지 않습니다.

```VeroScript
int parse(int value)
{
	return value;
}

string parse(string value)
{
	return value;
}
```

인수가 제공되면 컴파일러는 해당 유형과 사용 가능한 변환을 사용하여 가장 일치하는 후보를 선택합니다. 일치하는 후보가 없거나 여러 후보가 동일하게 일치하는 경우 컴파일에서는 진단을 보고합니다.

후보를 일치시킬 때 호환 가능한 숫자 변환과 단일 값을 받는 비-`explicit` 변환 생성자 같은 암시적 변환을 고려합니다. `explicit` 생성자는 일반 할당이나 인수 변환의 후보가 아니므로 직접 생성하거나 `cast<T>`로 요청해야 합니다. 컴파일러는 여러 사용자 정의 변환을 연결하지 않습니다.

## 참조 매개변수

참조 표시는 매개변수 일치의 일부이며 호출 사이트에 작성되어야 합니다.

```VeroScript
void update(int& value)
{
	value = value + 1;
}

void update(const int& value)
{
	print(value);
}

int number = 1;
update(&number);
```

참조 표시를 생략하거나 쓰기 가능한 읽기 전용 참조를 사용하지 마십시오. 명시적 마커는 참조 오버로드를 명확하게 만듭니다. `ref T` 및 `ref expression`은 여전히 지원되는 호환성 철자법입니다.

## 일반 오버로드

제네릭 후보는 해당 유형 인수를 결정할 수 있고 제약 조건이 유지되며 해당 매개변수가 인수를 허용할 수 있는 경우에만 실행 가능합니다.

```VeroScript
T identity<T>(T value)
{
	return value;
}

int number = identity(42);
string text = identity<string>("text");
```

추론이 모호할 경우 명시적인 유형 인수를 제공하거나 매개변수를 조정하십시오.

## 연산자 오버로드

연산자 오버로드는 동일한 후보 일치 규칙을 따릅니다. 멤버 연산자는 암시적 현재 개체를 포함하는 반면, 비멤버 연산자는 명시적 피연산자를 사용합니다. 해당 매개변수와 반환 유형은 연산자의 구현 규칙을 충족해야 합니다. 선언은 [연산자](../language-basics/operators.md)를 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-reference/overload-resolution.md) · [fr-FR](../../fr-FR/language-reference/overload-resolution.md) · [ja-JP](../../ja-JP/language-reference/overload-resolution.md) · [ko-KR](overload-resolution.md) · [zh-CN](../../zh-CN/language-reference/overload-resolution.md) · [zh-TW](../../zh-TW/language-reference/overload-resolution.md)
