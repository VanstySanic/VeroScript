# 변수

변수는 런타임에 값을 저장하는 유형이 있는 명명된 선언입니다. 변수는 모듈 범위, 함수 또는 블록 내부, 구조체 또는 클래스 멤버로 선언될 수 있습니다.

## 선언

변수 선언에는 타입, 이름 및 초기값을 지정합니다.

```VeroScript
int count = 0;
int limit = 10;
string name = "VeroScript";
```

지역 변수와 모듈 수준 변수는 초기화해야 하며 `int count;`는 유효하지 않습니다. 명시적인 값, 생성자 호출 또는 중괄호 초기화를 사용하세요. 구조체와 클래스의 필드는 초기값을 생략할 수 있으며, 이 경우 타입의 필드 기본값 규칙이 적용됩니다.

## 초기화

VeroScript는 생성자 호출 및 중괄호 초기화를 지원합니다.

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
```

사용자 정의 유형은 생성자를 호출하여 초기화할 수 있습니다.

```VeroScript
struct Point
{
	int x;
	int y;
};

int main()
{
	Point point = Point();
	point.x = 10;
	point.y = 20;
}
```

## 할당

선언 후 할당 표현식을 사용하여 변수를 수정할 수 있습니다.

```VeroScript
int value = 1;
value = 2;
value += 3;
```

할당의 양쪽은 언어의 변환 규칙을 충족해야 합니다. 할당은 변수의 정적 유형을 변경하지 않습니다.

## 지역 및 전역 변수

함수나 블록에 선언된 변수는 지역 변수입니다. 모듈 범위에서 선언된 변수는 전역 변수이며 동일한 모듈의 함수로 액세스할 수 있습니다.

```VeroScript
int global_count = 0;

void increment()
{
	global_count += 1;
}
```

모듈 전반에 걸쳐 전역에 대한 액세스에는 가져오기 및 내보내기 규칙이 적용됩니다. 범위는 이름이 유효한 위치를 설명합니다. 가시성은 모듈 경계 및 액세스 지정자에 의해서도 영향을 받습니다.

## `auto` 변수

`auto`은 이니셜라이저에서 유형을 유추합니다.

```VeroScript
auto answer = 42;
auto text = "hello";
```

`auto` 변수를 초기화해야 합니다. 일단 추론되면 정적으로 유형이 지정된 변수로 유지되며 나중에 다른 유형을 할당할 수 없습니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/variables.md) · [fr-FR](../../fr-FR/language-basics/variables.md) · [ja-JP](../../ja-JP/language-basics/variables.md) · [ko-KR](variables.md) · [zh-CN](../../zh-CN/language-basics/variables.md) · [zh-TW](../../zh-TW/language-basics/variables.md)
