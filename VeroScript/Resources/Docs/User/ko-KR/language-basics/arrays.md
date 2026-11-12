# 배열

VeroScript에는 동적 배열 `array<T>`과 고정 크기 기본 배열 `T[N]`이라는 두 가지 배열 형식이 있습니다. 둘 다 인덱싱과 범위 반복을 지원하지만 런타임 의미 체계가 다른 유형입니다.

## 동적 배열

`array<T>`은 요소 유형 `T`로 매개변수화된 동적 컨테이너입니다.

```VeroScript
array<int> values = array<int>();
values.push(10);
values.push(20);

print(values[0]);
print(len(values));
```

중괄호 초기화도 지원됩니다.

```VeroScript
array<int> values = array<int>{1, 2, 3};
```

일반적인 작업은 다음과 같습니다.

| 운영 | 설명 |
| --- | --- |
| `values[index]` | 인덱스의 요소 읽기 또는 수정 |
| `values.push(value)` | 요소를 추가하고 배열을 반환 |
| `values.remove(value)` | 첫 번째 동일한 요소를 제거하고 제거되었는지 여부를 반환 |
| `values.remove_at(index)` | 인덱스에서 요소 제거 |
| `values.clear()` | 배열을 지우고 배열을 반환 |
| `values.reserve(count)` | 용량 예약 및 어레이 반환 |
| `values.capacity()` | 현재 용량 반환 |
| `len(values)` | 요소 수를 반환 |

`array<T>`에 삽입된 모든 요소는 `T`과 일치해야 합니다. 구조체와 기본 배열은 삽입 시 값 의미 체계에 따라 복사됩니다. 클래스 및 기타 핸들 유형은 핸들 의미 체계를 유지합니다.

동적 배열은 공유 컨테이너 핸들을 통해 저장됩니다. 하나의 `array<T>` 변수를 다른 변수에 할당하면 두 변수가 모두 동일한 동적 배열을 참조하게 됩니다. 독립 배열을 구성하고 독립 복사본이 필요할 때 해당 요소를 복사합니다.

## 고정 크기 네이티브 배열

기본 배열은 요소 유형 뒤에 길이를 입력합니다.

```VeroScript
int numbers[3] = {10, 20, 30};
```

길이는 유형의 일부입니다. `int[3]`, `int[4]`, `array<int>`는 서로 다른 유형입니다. 기본 배열은 구조체 필드일 수 있으며 다차원일 수 있습니다.

```VeroScript
struct GridRow
{
	int cells[3] = {1, 2, 3};
};

int matrix[2][3] = {
	{1, 2, 3},
	{4, 5, 6}
};
```

다차원 기본 배열은 중첩된 기본 배열로 구성됩니다. `len(matrix)`은 첫 번째 차원의 길이를 반환하고, `len(matrix[0])`은 두 번째 차원의 길이를 반환합니다.

네이티브 배열 형식은 최대 64개 차원을 가질 수 있습니다. 각 차원은 최대 65,536개 요소를 포함할 수 있고, 모든 차원을 평탄화한 요소 수의 곱은 최대 1,048,576개입니다. 어느 한도라도 초과하는 소스는 컴파일 중 거부됩니다.

## 인덱싱

배열 인덱스는 `0`에서 시작합니다.

```VeroScript
array<string> names = array<string>{"Ada", "Linus"};
string first = names[0];
names[1] = "Grace";
```

인덱싱 표현식의 결과에는 요소 유형이 있습니다. 인덱스는 현재 배열의 유효한 범위 내의 정수여야 합니다. 범위를 벗어난 액세스는 런타임 오류를 생성합니다.

## 범위 반복

배열을 반복하려면 콜론과 함께 `for`을 사용하십시오. 일반 루프 변수는 요소 값을 받습니다.

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

참조 루프 변수를 사용하여 원래 요소를 수정합니다.

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

구조체 배열의 경우 값 기준 루프 변수를 수정해도 배열에 다시 쓰지 않습니다. 원본 요소를 수정하려면 `T&`을 사용하십시오. 동적 배열과 기본 배열 모두 참조 반복을 지원합니다.

## 배열 형식 선택

길이가 고정되어 있고 데이터 레이아웃의 일부인 경우 `T[N]`을 사용합니다. 런타임 시 길이가 변경되거나 `push` 및 `remove_at`과 같은 작업이 필요한 경우 `array<T>`을 사용합니다. 함수 간에 변경 가능한 컨테이너를 공유해야 하는 경우 `array<T>`를 사용하고 고정 레이아웃 값이 필요한 경우 기본 배열을 사용합니다.

`len(values)`와 `values.capacity()`는 음수가 아닌 `int64`를 반환합니다. 선형 인덱스, `remove_at(index)` 및 `reserve(count)`는 `int64`를 받습니다. 인덱스는 `0 <= index < len(values)`여야 하고 예약 용량은 음수가 아니며 호스트에서 표현 가능해야 합니다. 할당 제한은 계속 적용됩니다. 범위 검사 변환은 [내장 함수](../standard-library/builtins.md)를 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/arrays.md) · [fr-FR](../../fr-FR/language-basics/arrays.md) · [ja-JP](../../ja-JP/language-basics/arrays.md) · [ko-KR](arrays.md) · [zh-CN](../../zh-CN/language-basics/arrays.md) · [zh-TW](../../zh-TW/language-basics/arrays.md)
