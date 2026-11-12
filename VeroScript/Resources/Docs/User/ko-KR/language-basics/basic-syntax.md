# 기본 구문

이 페이지에서는 명령문, 블록, 식별자 및 키워드를 포함하여 VeroScript 소스 파일의 기본 구조를 설명합니다.

## 소스 파일

VeroScript 소스 파일은 `.vs` 확장자를 사용합니다. 소스 파일에는 전역 변수, 함수, 구조체, 클래스, 인터페이스, 열거형, 네임스페이스 및 모듈 선언이 포함될 수 있습니다.

애플리케이션 스크립트는 일반적으로 `main` 진입점을 제공합니다. 라이브러리 모듈은 입력 함수를 정의하지 않고도 유형과 함수를 내보낼 수 있습니다. `main`은 `int` 또는 `void`을 반환할 수 있습니다. 프로세스 종료 코드가 명시적이어야 하는 경우 `int`를 사용하는 것이 좋습니다.

```VeroScript
int global_value = 1;

int add(int left, int right)
{
	return left + right;
}

int main()
{
	print(add(global_value, 2));
	return 0;
}
```

## 추천 소스 스타일

문서의 VeroScript 스니펫은 들여쓰기 수준당 Tab 문자, 한 줄에 여는 중괄호, 한 줄에 하나의 일반 문, `class`, `struct`, `interface` 및 `enum` 선언 뒤에 후행 세미콜론을 사용합니다. 언어는 후행 세미콜론 없이 일부 간결한 형식을 허용하지만 문서 코드에서는 완전한 형식을 선호해야 합니다. 독립 실행형 예제 파일은 examples README에 설명된 포매터 스타일을 따릅니다.

## 명령문 및 세미콜론

일반적인 명령문은 세미콜론으로 끝납니다.

```VeroScript
int value = 1;
value = value + 1;
print(value);
```

함수 선언, 유형 선언 및 제어 흐름 문은 복합 문에 중괄호를 사용합니다. 공백과 줄 바꿈은 일반적으로 문의 의미를 변경하지 않습니다.

```VeroScript
if (value > 0)
{
	print("positive");
}
```

## 식별자

식별자는 변수, 함수, 유형, 필드, 네임스페이스 및 모듈 멤버를 명명합니다. 식별자에는 ASCII 문자, 숫자, 밑줄이 포함될 수 있지만 숫자로 시작할 수는 없습니다.

```VeroScript
int item_count = 3;
string display_name = "VeroScript";
```

식별자는 대소문자를 구분합니다. `item_count`, `ItemCount` 및 `itemCount`는 다른 이름입니다.

## 키워드

키워드는 언어로 정의된 의미를 가지며 일반적인 식별자로 사용할 수 없습니다. 키워드 세트에는 다음이 포함됩니다.

```text
void bool char short int long float double string wstring
int8 uint8 int16 uint16 int32 uint32 int64 uint64
unsigned auto channel array map multimap set multiset
queue priority_queue stack ordered_map ordered_multimap deque ordered_multiset
fn function
if else while do for switch case default return break continue fallthrough goto
try catch finally throw struct class interface enum
import export using typedef namespace friend
new delete this const ref static explicit
public protected private virtual override final
where operator template in
true false nullptr
```

키워드 세트는 언어 문법의 일부입니다. 새 키워드로 인해 다른 유효한 식별자를 사용할 수 없게 될 수 있으므로 라이브러리 코드에서는 키워드를 공개 이름으로 사용하지 않아야 합니다.

`do`는 이제 `do ... while`을 위한 예약어입니다. 이전 소스에서 `do`를 식별자로 사용했다면 이름을 바꿔야 합니다.

## 같은 형식의 선언 그룹과 원시 문자열

최상위, 네임스페이스 및 로컬 선언에서는 해당 선언 위치에서 유효한 완전한 선행 형식과 `const` / `ref` 한정자가 같을 때 여러 변수를 도입할 수 있습니다. 초기화 식은 왼쪽에서 오른쪽으로 평가되고 이름도 같은 순서로 보이게 됩니다.

```VeroScript
const int first = 1, second = first + 1;
for (int index = 0, end = 3; index < end; index++)
{
	print(index);
}
```

각 항목은 자체 초기화 식과 일반 선언 검사를 유지합니다. 특히 `auto first = 1, second = 2;`는 의도적으로 거부됩니다. 이름마다 배열 접미사를 써서 서로 다른 모양을 선언할 수도 없습니다. 공유 배열 길이는 형식 접두사에 작성하십시오.

UTF-8 원시 문자열은 고정 형식 `r"..."`를 사용합니다. 역슬래시와 줄 바꿈을 이스케이프 처리 없이 보존합니다.

```VeroScript
string path = r"C:\tools\new";
string text = r"first line
second line";
```

다음에 오는 첫 `"`가 리터럴을 닫으므로 원시 텍스트에는 큰따옴표를 넣을 수 없습니다. 와이드 원시 문자열, 사용자 지정 구분자, 문자열 보간은 없습니다.

## 의도적인 구문 경계

VeroScript는 단독 빈 문 `;`을 거부합니다. 의도적으로 빈 본문에는 `{}`를 사용하십시오. 일반 쉼표 표현식, `f(value: 1)` 같은 명명된 인수, 추론된 `auto` 반환형, 구조적 바인딩, `sizeof`나 `alignof` 같은 C++ 레이아웃 연산자도 지원하지 않습니다. 선언 그룹은 같은 형식의 선언에만 사용하고, 명시적 반환형과 언어 또는 컨테이너 API를 사용하십시오.

## 파서 안전 한계

식, 단항 식 및 전처리기 `#if` 식의 중첩은 최대 256단계입니다. statement 중첩은
32단계이고 expression chain은 256개 링크로 제한됩니다. 한계를 넘으면
`expression nested too deeply`, `statement nested too deeply`, `#if expression
nested too deeply`, `expression chain is too long`과 같은 diagnostic이 발생합니다.
자동 생성 코드가 지나치게 깊어지면 식이나 control-flow block을 나누십시오.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/basic-syntax.md) · [fr-FR](../../fr-FR/language-basics/basic-syntax.md) · [ja-JP](../../ja-JP/language-basics/basic-syntax.md) · [ko-KR](basic-syntax.md) · [zh-CN](../../zh-CN/language-basics/basic-syntax.md) · [zh-TW](../../zh-TW/language-basics/basic-syntax.md)
