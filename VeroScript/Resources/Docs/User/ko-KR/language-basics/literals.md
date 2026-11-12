# 리터럴

리터럴은 소스 코드에 직접 작성된 값입니다. VeroScript는 정수, 부동 소수점, 문자, 문자열, 와이드 문자열, 부울 및 `nullptr` 리터럴을 지원합니다.

## 정수 리터럴

접두사가 없는 정수는 10진수입니다. `0x`/`0X`는 16진수, `0b`/`0B`는 2진수를 나타냅니다.

```VeroScript
int decimal = 42;
int hexadecimal = 0x2A;
int binary = 0b101010;
```

접미사는 범위나 부호를 명시적으로 만들 수 있습니다.

```VeroScript
unsigned int unsigned_value = 100u;
long long_value = 100L;
int64 wide_value = 9223372036854775807i64;
uint64 unsigned_wide_value = 18446744073709551615u64;
```

지원되는 정수 접미사에는 `u`, `l`, `ul`, `lu`, `i64` 및 `u64`의 대소문자 구분 형식이 포함됩니다. `l`/`L`은 언어의 64비트 `long`을 선택합니다. `i64`/`u64` 부호 있는/부호 없는 64비트 리터럴을 직접 선택합니다. 접미사가 없는 리터럴과 `u` 리터럴은 32비트 경로를 사용합니다. 리터럴은 대상 유형으로 표현할 수 있어야 합니다.

밑줄은 현재 진법에서 유효한 두 숫자 사이에만 사용할 수 있으며 부동 소수점의 소수부와 지수부에도 적용됩니다. `1_000`, `0xFF_FF`, `0b1010_0101`, `1_2.5_0e1`은 유효합니다. 진법 접두사, 소수점, 지수 표식이나 부호, 형식 접미사 또는 다른 밑줄과 맞닿을 수 없습니다. 앞의 0은 8진수를 뜻하지 않으므로 `010`은 10진수 십입니다.

## 부동 소수점 리터럴

소수점이나 지수가 있는 숫자는 부동 소수점 리터럴로 구문 분석됩니다.

```VeroScript
double pi_approximation = 3.14159;
double scientific = 1.25e2;
double half = .5;
float small = .5f;
double fifty = .5e2;
```

소수점 바로 뒤에 10진 숫자가 오면 정수부를 생략할 수 있습니다. 따라서 `.5`, `.5f` 및 `.5e2`는 유효한 부동 소수점 리터럴이며 각각 `double`의 `0.5`, `float`의 `0.5`, `double`의 `50.0`을 나타냅니다. 단독 `.`은 계속 멤버 액세스 구두점으로 사용되며 `i64`와 같은 정수 접미사는 이 형식에 적용되지 않습니다.

지수는 `e` 또는 `E`을 사용하며 `+` 또는 `-`을 포함할 수 있습니다. `f` 또는 `F` 접미사는 `float` 리터럴을 식별합니다. 접미사가 없는 리터럴은 `double`입니다.

십진 부동소수점 리터럴은 호스트 로캘과 관계없이 `.`을 사용합니다. `f`/`F` 접미사는 binary32로 직접 반올림하며 접미사가 없으면 binary64로 반올림합니다. 오버플로와 허용된 binary64 범위 밖의 값은 컴파일 오류입니다. `double`로 표현되지만 `float`에는 너무 작은 값은 `1e-100f`처럼 0이 될 수 있습니다. 이 리터럴 규칙이 런타임 텍스트 파싱 API의 0 언더플로 허용을 뜻하지는 않습니다.

## 문자 리터럴

문자 리터럴의 정적 형식은 언어의 부호 있는 8비트 정수형 `char`입니다.

```VeroScript
char letter = 'A';
char newline = '\n';
char byte_ff = '\xFF';
```

직접 문자는 ASCII 한 바이트여야 합니다. `\xNN`은 8비트 비트 패턴을 정확히 기록하므로 `\x80`부터 `\xFF`까지는 음수 `char` 값입니다. 빈 문자, 여러 문자, 비 ASCII 직접 문자, Unicode 이스케이프 및 `L'A'` 같은 와이드 문자 형식은 거부됩니다.

## 문자열과 와이드 문자열

일반 문자열에는 큰따옴표를 사용합니다. 넓은 문자열은 여는 따옴표 앞에 `L`을 추가합니다.

```VeroScript
string text = "VeroScript";
wstring wide_text = L"text";
```

문자열 리터럴은 개행 및 따옴표를 포함한 이스케이프 문자를 지원합니다.

```VeroScript
string message = "first line\nsecond line";
string quoted = "say \"hello\"";
```

| 이스케이프 | `string` | `wstring` | `char` |
| --- | --- | --- | --- |
| `\\`, `\"`, `\'` | 백슬래시 또는 따옴표 | 동일 | 동일 |
| `\0`, `\a`, `\b`, `\f`, `\n`, `\r`, `\t`, `\v` | 한 바이트 | UTF-16 코드 단위 하나 | 한 바이트 |
| `\xNN` | 원시 한 바이트 | `U+00NN` | 8비트 패턴 |
| `\uNNNN`, `\UNNNNNNNN` | Unicode 스칼라를 UTF-8로 인코딩 | Unicode 스칼라를 UTF-16으로 인코딩 | 지원하지 않음 |

알 수 없거나 불완전한 이스케이프는 컴파일 오류입니다. Unicode 이스케이프는 서로게이트 값과 `U+10FFFF`를 넘는 값을 거부합니다. `string`은 길이를 가진 바이트 시퀀스입니다. 소스 텍스트와 텍스트 API는 UTF-8을 사용하지만 `\0`과 `\xNN`은 내장 NUL이나 임의 바이트를 만들 수 있으므로 런타임 `string`이 항상 올바른 UTF-8인 것은 아닙니다. `wstring`은 길이를 가진 UTF-16 코드 단위 시퀀스입니다. 명시적 UTF-8/UTF-16 변환은 잘못된 입력을 조용히 대체하지 않고 오류를 보고합니다.

문자열 리터럴은 소스 줄에 걸쳐 있을 수 없습니다. `string`과 `wstring`은 유형이 다르므로 직접 혼합할 수 없습니다. 필요한 경우 명시적 변환을 사용합니다.

배열, 맵, 세트 및 명시적 형식의 중괄호 초기화에는 `{1, 2,}` 또는 `{'A': 1,}`처럼 끝 쉼표 하나를 사용할 수 있습니다. 빈 초기화는 계속 `{}`입니다. 첫 쉼표, 연속 쉼표 또는 함수 인수의 끝 쉼표는 허용하지 않습니다.

## 부울 및 null

부울 리터럴은 `true` 및 `false`입니다.

```VeroScript
bool enabled = true;
bool finished = false;
```

`nullptr`은 null 개체 핸들 또는 null 런타임 값을 나타냅니다. 일반적으로 클래스 객체 및 nullable 런타임 값과 함께 사용됩니다.

```VeroScript
class Node
{
public:
	int value;
};

int main()
{
	Node node = nullptr;
	if (node == nullptr)
	{
		print("empty");
	}
}
```

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/literals.md) · [fr-FR](../../fr-FR/language-basics/literals.md) · [ja-JP](../../ja-JP/language-basics/literals.md) · [ko-KR](literals.md) · [zh-CN](../../zh-CN/language-basics/literals.md) · [zh-TW](../../zh-TW/language-basics/literals.md)
