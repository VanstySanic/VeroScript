# 참조 및 참조 의미론

참조는 기존 저장 위치 또는 핸들 슬롯의 별칭을 지정합니다. 독립적인 개체를 생성하지 않으며 별칭을 지정한 개체를 소유하지 않습니다. VeroScript 참조는 매개변수, 로컬 별칭, 범위 반복, 요소 액세스, 참조 반환 및 람다 참조 캡처에 사용됩니다. 이는 범용 스크립트 포인터가 아니며 포인터 산술을 지원하지 않습니다.

## 참조 매개변수

권장되는 철자는 뒤에 오는 `&`을 사용합니다.

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
	print(value);
	return 0;
}
```

`ref T` 및 `const ref T`은 동등한 키워드 스타일 철자법입니다.

```VeroScript
void reset(ref int value)
{
	value = 0;
}

int main()
{
	int value = 1;
	reset(ref value);
	return 0;
}
```

선언 및 호출 마커는 일치해야 합니다. `T&`은 `&expression`을 사용하고 `ref T`는 `ref expression`을 사용합니다. 참조 인수는 변수, 배열 요소, 맵 요소 또는 객체 필드와 같은 저장소를 식별해야 합니다.

## 읽기 전용 참조

읽기 전용 참조에는 `const T&` 또는 `const ref T`을 사용하십시오. 대상을 읽을 수 있지만 별칭을 통해 수정할 수는 없습니다.

```VeroScript
int read_value(const int& value)
{
	return value;
}

int main()
{
	const int value = 42;
	print(read_value(&value));
	return 0;
}
```

변경 가능한 참조는 const 개체나 const 참조 반환에 바인딩할 수 없습니다. 함수가 객체만 검사하면 되는 경우 읽기 전용 참조를 선호하세요.

## 로컬 참조 별칭

지역 변수는 기존 객체에 대한 별칭으로 선언될 수 있습니다.

```VeroScript
int main()
{
	int value = 1;
	int& alias = value;
	const int& view = value;

	alias = 2;
	print(view);
	return 0;
}
```

`alias` 및 `view`은 모두 `value`의 스토리지를 참조합니다. 참조 변수는 독립 개체가 되지 않으며 해당 대상을 복사하지 않습니다.

## 범위 반복의 참조

일반 범위 변수는 요소 값을 받습니다. 구조체와 같은 값 유형의 경우 루프 변수를 수정해도 원래 컨테이너에 다시 쓰지 않습니다.

```VeroScript
for (Point point : points)
{
	point.x = 0;
}
```

`T&`을 사용하여 루프 변수를 원래 요소의 별칭으로 만듭니다.

```VeroScript
for (Point& point : points)
{
	point.x = 0;
}
```

배열, 고정 크기 기본 배열, 데크 및 맵 값은 참조 범위 반복을 지원합니다. 읽기 전용 컬렉션에는 `const T&`을 사용합니다.

맵 반복에 키와 값이 모두 필요한 경우 `pair<K, V>&`을 사용하세요. 값만 필요한 경우 `V&`을 반복합니다.

```VeroScript
for (pair<string, int>& entry : counts)
{
	entry.second += 1;
}
```

## 요소 참조

배열이나 맵 요소는 참조 인수로 직접 전달될 수 있습니다.

```VeroScript
void bump(int& value)
{
	value += 1;
}

bump(&values[0]);
bump(&counts["total"]);
```

요소 참조는 요소를 복사하지 않으므로 이를 통해 변경된 내용은 호출 후에도 원래 컨테이너에 유지됩니다. 중첩된 필드와 요소 경로는 경로의 모든 개체가 유효한 한 참조를 생성할 수도 있습니다.

## 참조 반환

함수는 변경 가능하거나 읽기 전용 참조를 반환할 수 있습니다. 참조 반환 표현식은 할당 가능한 lvalue로 유지되며 증분되거나 다른 참조에 바인딩될 수 있습니다.

```VeroScript
int Counter = 1;
array<int> Values = array<int>();

int& counter_ref()
{
	return Counter;
}

int& first_value()
{
	return Values[0];
}

const int& read_counter()
{
	return Counter;
}

int main()
{
	Values.push(40);
	counter_ref() += 4;
	first_value() += 2;
	print(read_counter());
	return 0;
}
```

참조 반환은 유효한 상태로 유지되는 저장소를 가리켜야 합니다. 전역, 컨테이너 요소, 개체 필드 또는 기존 참조를 반환할 수 있습니다. 스택 저장이 함수로 끝나는 일반 함수 지역 변수를 반환할 수 없습니다. 가변 참조는 const 참조 반환에 바인딩할 수 없습니다.

클래스 핸들 변수는 참조 반환을 통해 리바운드될 수도 있습니다. 참조는 새 객체를 복사하는 대신 핸들 슬롯을 가리킵니다. 이는 클래스 자체의 공유 ID 동작과 다르며 API에 명시적으로 문서화되어야 합니다.

## 참조 및 함수 유형

참조 방향은 함수 유형의 일부입니다. 함수 값과 람다는 `int&` 또는 `const int&`을 사용할 수 있으며 호출에는 여전히 `&` 인수가 필요합니다.

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	function(int&) -> void mutate = add_one;
	int value = 3;
	mutate(&value);
	print(value);
	return 0;
}
```

## 제한 사항 및 평생

- 쓰기 가능한 참조 매개변수에는 기본값을 지정할 수 없습니다. 읽기 전용 참조에는 지원되는 리터럴 기본값을 지정할 수 있으며 호출자가 보관하는 임시 값에 바인딩됩니다. 참조 매개변수는 가변 인수가 될 수 없습니다.
- 참조는 객체를 소유하지 않으며 포인터 산술을 지원하거나 일반 객체의 수명을 자동으로 연장합니다.
- `ref`, `const ref`, `T&` 및 `const T&`은 함수 유형, 오버로드 및 바인딩 검사에 영향을 줍니다.
- 일반 참조는 대상 저장소가 유효한 동안 사용해야 합니다.
- Lambda 참조 캡처는 관리되는 공유 참조 셀을 사용하므로 클로저는 이를 생성한 함수보다 안전하게 오래 지속될 수 있습니다. [람다 및 함수 값](lambdas.md)을 참조하세요.

실행 가능한 참조 예제는 [예제 색인](../examples.md)에 나열되어 있습니다.

컨테이너 요소 참조는 원래 컨테이너와 항목을 빌립니다. 컨테이너 변수를 다시 대입해도 참조 대상은 바뀌지 않습니다. 같은 항목에 대입하거나 끝에 추가하거나 다른 키를 삽입하면 대상이 유지됩니다. 항목이 만료되거나 원래 위치를 다른 요소가 차지하면 잘못된 참조 진단을 반환합니다. 관리되는 참조 반환과 Lambda 캡처의 수명 규칙은 유지됩니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/references.md) · [fr-FR](../../fr-FR/language-basics/references.md) · [ja-JP](../../ja-JP/language-basics/references.md) · [ko-KR](references.md) · [zh-CN](../../zh-CN/language-basics/references.md) · [zh-TW](../../zh-TW/language-basics/references.md)
