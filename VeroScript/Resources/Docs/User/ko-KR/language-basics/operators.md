# 연산자

연산자는 표현식 값을 계산, 비교, 결합 또는 수정합니다. VeroScript는 산술, 비교, 논리, 비트 단위 할당, 인덱싱, 멤버 액세스 및 조건 연산자를 지원합니다.

## 연산자 카테고리

| 카테고리 | 운영자 |
| --- | --- |
| 산술 | `+`, `-`, `*`, `/`, `%` |
| 비교 | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| 논리적 | `!`, `&&`, `||` |
| 비트 단위 | `&`, `|`, `^`, `<<`, `>>` |
| 과제 | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` |
| 증가/감소 | `++`, `--` |
| 조건부 | `condition ? when_true : when_false` |
| 액세스 | `.`, `::`, `[]`, `()` |

## 우선순위와 결합 방향

다음 표는 우선순위가 낮은 것부터 높은 것까지의 순서입니다.

| 단계 | 연산자 | 결합 방향 |
| --- | --- | --- |
| 대입 | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` | 오른쪽 |
| 조건 | `?:` | 오른쪽 |
| 논리 OR | `||` | 왼쪽 |
| 논리 AND | `&&` | 왼쪽 |
| 비트 OR | `|` | 왼쪽 |
| 비트 XOR | `^` | 왼쪽 |
| 비트 AND | `&` | 왼쪽 |
| 비교 | `==`, `!=`, `<`, `>`, `<=`, `>=` | 왼쪽 |
| 시프트 | `<<`, `>>` | 왼쪽 |
| 덧셈 | `+`, `-` | 왼쪽 |
| 곱셈 | `*`, `/`, `%` | 왼쪽 |
| 전위 | `!`, `~`, 단항 `+`/`-`, 전위 `++`/`--`, `new`, `delete`, 변환, `ref` | 오른쪽 |
| 후위/액세스 | 호출 `()`, 인덱싱 `[]`, `.`, `::`, 후위 `++`/`--` | 왼쪽 |

`&&`와 `||`는 왼쪽 피연산자를 먼저 평가하고 단락합니다. `?:`는 조건을 먼저 평가한 뒤 한 분기만 평가합니다. 그 밖에는 부작용 순서가 중요할 때 피연산자나 함수 인수의 상대 평가 순서에 의존하지 말고 별도 문으로 나누십시오. 연산자 오버로드는 우선순위나 결합 방향을 바꾸지 않습니다. 일반 쉼표 연산자는 없으며 쉼표로 구분한 표현식은 기존 형식 `for`의 초기화 절과 반복 절에서만 지원됩니다.

계산 순서를 명시적으로 나타내려면 괄호를 사용하십시오.

```VeroScript
int result = (a + b) * c;
bool valid = (value >= minimum) && (value <= maximum);
```

## 산술 연산자

`+`, `-`, `*`, `/` 및 `%`는 숫자 연산을 수행합니다. `+`는 동일한 텍스트 유형의 값을 연결할 수도 있습니다.

```VeroScript
int total = 10 + 2;
int remainder = total % 3;
string message = "value: " + to_string(total);
```

`string`과 `wstring`은 서로 직접 연결할 수 없습니다.

## 비교 및 논리 연산자

비교 연산자는 부울 결과를 생성합니다. 논리 연산자는 부울 표현식을 결합합니다.

```VeroScript
bool in_range = value >= 0 && value <= 100;
bool invalid = value < 0 || value > 100;
bool enabled = !disabled;
```

## 할당 연산자

`=`은 일반 할당을 수행합니다. 복합 할당은 작업과 할당을 결합합니다.

```VeroScript
value = 10;
value += 2;
value *= 3;
```

과제의 양쪽 모두 언어의 변환 규칙을 충족해야 합니다. 할당은 왼쪽 변수의 정적 유형을 변경하지 않습니다.

## 액세스 연산자

인덱싱 연산자 `[]`은 배열 또는 컨테이너 요소에 액세스합니다. 멤버 액세스 연산자 `.`은 개체 멤버에 액세스합니다.

```VeroScript
int first = values[0];
point.x = 10;
```

네임스페이스 및 유형 멤버는 `::`을 사용할 수도 있습니다.

```VeroScript
Color color = Color::Red;
```

값 멤버에는 `.`을 사용하고 네임스페이스, 유형, enum 멤버 및 정적 멤버에는 `::`을 우선하세요. 앞의 `::`는 전역/루트 범위에서 검색을 시작하며 현재 네임스페이스, 로컬 별칭 및 `using namespace` 대체 검색을 무시합니다.

## 연산자 오버로딩

사용자 정의 유형은 해당 유형이 선택한 작업에 참여하도록 연산자 메서드를 선언할 수 있습니다. 오버로드는 컴파일러의 매개변수 및 반환 유형 규칙을 충족해야 합니다. 해당 유형에 대해 놀라운 의미를 갖는 연산자를 오버로드하지 마십시오.

멤버 연산자는 현재 개체를 암시적 왼쪽 피연산자로 사용합니다. 비멤버 연산자 함수는 피연산자를 명시적으로 선언할 수 있습니다.

```VeroScript
class Vec
{
public:
	int x;

	Vec(int value)
	{
		x = value;
	}

	Vec operator+(Vec other)
	{
		return Vec(x + other.x);
	}

	void operator+=(Vec other)
	{
		x = x + other.x;
	}

	bool operator==(Vec other)
	{
		return x == other.x;
	}

	int operator[](int index)
	{
		return x + index;
	}
};

Vec operator+(int value, Vec vector)
{
	return Vec(value + vector.x);
}

Vec sum = Vec(3) + Vec(4);
Vec shifted = 1 + Vec(2);
int item = sum[0];
```

연산자 오버로드는 이항 및 단항 연산, 복합 할당, 인덱싱 및 함수 호출 구문을 다룰 수 있습니다. 유형은 객체가 명시적으로 부울 값으로 변환될 수 있도록 `bool operator bool()`과 같은 변환 연산자를 선언할 수도 있습니다.

```VeroScript
class Token
{
public:
	int value = 42;
	bool operator bool()
	{
		return value != 0;
	}
};

int main()
{
	Token token = Token();
	bool available = token;
	print(available);
	return 0;
}
```


내장 부울 연산자인 `&&`와 `||`는 항상 단락 평가를 유지하며 사용자 정의 오버로드로 대체되지 않습니다. 사용자 정의 연산자 후보는 적어도 한 연산자가 오버로드 가능한 사용자 정의 형식 또는 호스트 형식일 때만 고려되므로, 두 개의 기본 형식을 변환해야 하는 연산자는 선택되지 않습니다.

일부 비교 형식은 역방향을 지원하기 위해 `operator<`을 사용하는 것과 같이 선언된 기본 연산자에서 미러링된 동작을 얻을 수 있습니다. 유형의 의미를 직접 표현하는 선언 연산자를 선호하고 오버로드된 연산자를 익숙한 의미와 일관되게 유지하세요.

## 연산자 오버로드의 참조

연산자 인수는 C++처럼 참조 매개변수에 암시적으로 바인딩됩니다. `a + b`, `a[i]`, `f(x)`에는 `&`나 `ref`가 필요하지 않습니다. 일반 함수 및 메서드 호출에는 기존의 명시적 참조 인수 표식이 필요합니다. `T&`는 별칭을 정규화한 뒤 같은 타입인 쓰기 가능한 저장 위치를 요구하며 상수, 임시 값, 숫자 변환을 거부합니다. `const T&`는 읽기 전용 저장 위치와 변환으로 생성한 임시 값을 허용합니다.

나머지 변환이 같으면 쓰기 가능한 좌측 값은 `const T&`보다 `T&`를 우선합니다. 값 전달과 참조 전달의 변환을 구별할 수 없으면 모호한 호출로 거부됩니다.

`T&`를 반환하는 연산자 식은 대입 대상이나 지역 참조 별칭의 원본이 될 수 있습니다. `const T&` 결과는 읽기 전용입니다. 수신 객체의 const와 접근 권한도 검사합니다. 참조 반환의 수명 검사는 유지되며 `return this`는 현재 객체의 핸들을 참조합니다.

전위 ++/-- 멤버 오버로드에는 명시적 매개변수가 없고, 후위 버전에는 값으로 전달하는 `int` 태그가 있습니다. 자유 함수 버전은 피연산자를 받고 후위 버전은 태그도 받습니다. 선언한 반환값이 식의 결과입니다. `void` 복합 대입을 객체 값으로 사용할 수 없으며 비 void 결과도 대상에 자동으로 다시 대입하지 않습니다.

`operator()`는 기본 인수와 이 언어의 타입이 지정된 가변 인수를 지원합니다. 다른 연산자에는 둘 다 사용할 수 없습니다. `operator=`는 하나의 매개변수를 갖는 멤버여야 합니다. 변수 선언의 초기화 동작은 유지됩니다.

`static`으로 선언할 수 있는 연산자는 `operator()`와 `operator[]`뿐입니다. 상속된 연산자는 선언한 타입의 제네릭 인수를 사용합니다. 정적 필드와 네임스페이스 전역 변수는 참조 매개변수에 바인딩할 수 있지만, 임시 구조체의 필드는 쓰기 가능한 참조에 바인딩할 수 없습니다.

자유 함수 `operator[]`/`operator()`, 비교 반전 및 합성 복합 대입은 VeroScript 확장입니다. 클래스 핸들과 구조체 값의 의미는 유지됩니다. C++ 포인터, 우측 값 참조 및 이동 의미를 완전히 지원하는 것은 아닙니다.

이 절의 새 기능은 VM, mixed 및 JIT 실행 백엔드를 대상으로 합니다. 소스 AOT 내보내기는 별도의 지원 범위를 갖습니다. 새 참조 연산자 조합, 대입 오버로드, 후위 태그 및 호출 연산자의 기본/가변 인수는 아직 지원하지 않으며 명시적으로 거부됩니다.

```VeroScript
class Cell
{
public:
	int value = 0;
	int& operator[](int index)
	{
		return value;
	}
	const int& operator[](int index) const { return value; }
	void operator()(int& target, const int& delta = 1) const { target += delta; }
	Cell& operator=(const int& next)
	{
		value = next;
		return this;
	}
	Cell& operator++()
	{
		value++;
		return this;
	}
	Cell operator++(int tag)
	{
		Cell old = Cell();
		old.value = value;
		value++;
		return old;
	}
};

void main()
{
	Cell cell = Cell();
	cell[0] = 3;
	cell[0] += 2;
	int number = 4;
	cell(number);             // number == 5
	cell(number, 2);          // number == 7
	print((cell++).value);    // 5
	print((++cell).value);    // 7
	(cell = 9)[0] = 10;
	const Cell view = cell;
	print(view[0]);           // 10
}
```

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/operators.md) · [fr-FR](../../fr-FR/language-basics/operators.md) · [ja-JP](../../ja-JP/language-basics/operators.md) · [ko-KR](operators.md) · [zh-CN](../../zh-CN/language-basics/operators.md) · [zh-TW](../../zh-TW/language-basics/operators.md)
