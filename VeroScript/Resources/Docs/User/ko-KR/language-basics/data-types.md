# 데이터 유형

유형은 값이 표현되는 방식, 값에 적용할 수 있는 작업 및 허용되는 변환을 결정합니다. VeroScript 유형은 내장형, 컨테이너형 및 사용자 정의 유형으로 그룹화됩니다.

## 내장 유형

| 카테고리 | 유형 |
| --- | --- |
| 값 없음 | `void` |
| 부울 | `bool` |
| 부호 있는 정수 | `char`, `short`, `int`, `long` |
| 부호 없는 정수 | `unsigned char`, `unsigned short`, `unsigned int`, `unsigned long` |
| 고정 너비 정수 | `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64` |
| 부동 소수점 | `float`(바이너리32), `double`(바이너리64) |
| 텍스트 | `string`, `wstring` |

`void`은 함수 반환 유형으로만 사용할 수 있습니다. 변수에는 사용할 수 없습니다. 고정 너비 정수 및 `float`/`double`는 문서 전용 별칭이 아닌 고유한 언어 유형입니다. 이는 명시적인 너비 또는 부동 소수점 정밀도가 필요한 인터페이스 및 데이터 구조에 유용합니다.

고정 너비 정수 범위는 `int8`의 경우 `-128` ~ `127`, `uint8`의 경우 `0` ~ `255`, `int16`의 경우 `-32768` ~ `32767`, `uint16`의 경우 `0` ~ `65535`입니다. `int32`, `uint32`, `int64` 및 `uint64`는 해당하는 부호 있는 및 부호 없는 32비트 및 64비트 범위를 사용합니다. 대상 범위를 벗어나는 리터럴은 오류입니다. 잘림에 의존하지 마십시오.

```VeroScript
int8 signed_byte = -128;
uint8 byte = 255u;
float single = 2.05f;
double precise = 2.05;
```

`f`/`F` 접미사가 있는 부동 소수점 리터럴은 `float`로 저장됩니다. 접미사가 없는 부동 소수점 리터럴은 `double`으로 저장됩니다. 따라서 반올림 동작이 다를 수 있습니다.

## 컨테이너 유형

컨테이너 유형은 유형 인수를 사용하여 해당 요소 또는 키/값 유형을 설명합니다.

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
set<string> names = set<string>();
queue<int> pending = queue<int>();
```

VeroScript는 다음과 같은 컨테이너 카테고리를 제공합니다.

- 시퀀스 컨테이너: `array`, `deque`.
- 연관 컨테이너: `map`, `multimap`, `ordered_map`, `ordered_multimap`.
- 컨테이너 설정: `set`, `multiset`, `ordered_multiset`.
- 어댑터: `queue`, `priority_queue`, `stack`.
- 동시 컨테이너: `channel`.

컨테이너 작업, 요소 의미 체계 및 동시성 규칙은 컨테이너 및 동시성 참조에 속합니다.

## 네이티브 배열

고정 크기 기본 배열은 요소 유형 뒤에 크기를 배치합니다.

```VeroScript
int numbers[3] = {10, 20, 30};
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
```

`T[N]`은 유형에 길이를 포함하는 반면 `array<T>`은 동적 배열 컨테이너입니다. 둘 다 인덱싱할 수 있지만 유형이 다릅니다.

## 사용자 정의 유형

사용자는 `struct`, `class`, `interface` 및 `enum`을 사용하여 새 유형을 정의할 수 있습니다.

```VeroScript
struct Point
{
	int x;
	int y;
};

enum class Color
{
	Red,
	Green,
	Blue
};
```

`struct`에는 값 의미 체계가 있고 `class`에는 공유 ID가 있습니다. 필드, 메서드 및 객체 의미에 대해서는 [구조체](structs.md) 및 [클래스](classes.md)를 참조하세요. 해당 유형 형식은 [인터페이스](interfaces.md) 및 [열거형](enums.md)을 참조하세요. 반면 제네릭은 별도로 다룹니다.

## `auto`

`auto`은 초기화 프로그램에서 변수 유형을 추론합니다.

```VeroScript
auto count = 3;
auto values = array<int>{1, 2, 3};
```

`auto` 선언에는 이니셜라이저가 있어야 합니다. 추론은 선언에서 발생합니다. 변수는 이후에도 여전히 명확한 정적 유형을 갖습니다.

## 유형 변환

호환되는 숫자 유형은 언어 규칙에 따라 변환될 수 있습니다. `string` 및 `wstring`은 암시적으로 혼합될 수 없습니다. 텍스트 유형 간에 이동할 때는 적절한 명시적 변환 기능을 사용하세요.

변환 허용 여부는 소스 유형, 대상 유형 및 표현식 컨텍스트에 따라 다릅니다. 호스트 바인딩과 일반 인터페이스는 가능한 경우 암시적 변환에 의존하는 대신 명시적 형식을 사용해야 합니다.

## 읽기 전용 래퍼

`readonly<T>`는 `T`를 값으로 저장하는 표준 래퍼입니다. 래퍼 자체는 복사하거나 컨테이너에 넣을 수 있고, `const ref T get() const`를 통해 읽기 전용으로 노출됩니다.

```VeroScript
readonly<int> count = readonly<int>(41);
const ref int view = count.get();
print(view);
```

암시적 기본 생성자는 없습니다. struct는 값을 복사해 저장하고 class는 클래스 핸들을 저장하므로 공유 ID가 유지됩니다. 의도적인 `const_cast<T>` 탈출에 대해서는 [형식 변환](../language-reference/type-conversions.md)을 참조하세요.

내장 형식 이름인 `optional`, `readonly`, `weak`, `shared`, `type`, `field_info`, `method_info`, `module_info`, `function_info`, `global_info`, `enum_member_info`은 예약되어 있으며 스크립트에서 다시 선언할 수 없습니다.

## 컴파일러 제공 유틸리티 유형

VeroScript는 `optional<T>`, `weak<T>`, `pair<T, U>` 및 `shared<T>` / `handle<T>`도 제공합니다. 생성자, 상태, 소유권 동작 및 맵 항목 의미 체계는 [값 유틸리티 유형](value-utility-types.md)에 설명되어 있습니다.

`pair<T, U>`와 `handle<T>`는 소프트 제네릭 내장 유형입니다. 같은 이름과 같은 제네릭 인수 개수의 사용자 선언이 있으면 컴파일러 제공 형식보다 사용자 선언이 우선합니다. 이는 위에서 설명한 엄격하게 예약된 이름과 다릅니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/data-types.md) · [fr-FR](../../fr-FR/language-basics/data-types.md) · [ja-JP](../../ja-JP/language-basics/data-types.md) · [ko-KR](data-types.md) · [zh-CN](../../zh-CN/language-basics/data-types.md) · [zh-TW](../../zh-TW/language-basics/data-types.md)
