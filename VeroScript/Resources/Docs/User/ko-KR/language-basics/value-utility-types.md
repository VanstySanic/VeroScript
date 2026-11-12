# 값 유틸리티 유형

VeroScript는 값의 부재, 클래스 객체에 대한 비소유 관찰, 공유 구조체 페이로드 및 맵 항목을 다루기 위한 몇 가지 컴파일러 제공 제네릭 유형을 제공합니다. 이러한 유형은 가져오기 없이 사용할 수 있습니다. [std 네임스페이스](standard-namespace.md)에 설명된 `std::` 표기는 동일한 유형을 가리키는 호환성 별칭입니다.

## `optional<T>`

`optional<T>`은 값이 없거나 `T` 유형의 값 하나를 보유하는 값 유형입니다. 상태는 명시적이며 기본 생성된 optional은 비어 있습니다.

### 생성과 메서드

| 형식 | 의미 |
| --- | --- |
| `optional<T>()` | 빈 optional을 생성합니다. |
| `optional<T>(value)` | `value`를 포함하는 engaged optional을 생성합니다. |
| `has_value() const` | 값이 있는지 반환합니다. |
| `get() const` | 저장된 값을 반환합니다. 비어 있을 때 호출하면 `assert`를 통해 실패합니다. |
| `value_or(fallback) const` | 값이 있으면 그 값을, 비어 있으면 `fallback`을 반환합니다. |
| `reset()` | optional을 비웁니다. |

```VeroScript
optional<int> score = optional<int>();
print(score.has_value());
print(score.value_or(0));

score = optional<int>(95);
if (score.has_value())
{
	print(score.get());
}

score.reset();
```

optional은 `T`의 값 의미 체계를 따릅니다. 구조체 페이로드에서는 engaged optional을 복사할 때 구조체가 복사됩니다. 클래스 페이로드에서는 클래스 핸들이 복사되므로 공유 ID가 유지됩니다. `reset()`은 저장된 클래스 핸들이 있으면 함께 해제합니다.

실패할 수 있는 변환에서 `try_cast<T>`는 `optional<T>`을 반환합니다. `get()`을 호출하기 전에 `has_value()`를 확인하세요. 자세한 규칙은 [형식 변환](../language-reference/type-conversions.md)을 참조하세요.

## `weak<T>`

`weak<T>`은 클래스 객체를 소유하지 않고 관찰하는 유형입니다. `T`는 스크립트 클래스, 인터페이스 또는 지원되는 호스트 클래스 유형이어야 하며 기본 유형과 구조체는 유효한 대상이 아닙니다. weak 값은 대상의 수명을 연장하지 않습니다.

### weak 참조 메서드

| 형식 | 의미 |
| --- | --- |
| `weak<T>()` | 빈 weak 참조를 생성합니다. |
| `weak<T>(target)` | `target`의 클래스 핸들을 관찰합니다. |
| `lock() const` | 대상이 살아 있으면 강한 `T` 핸들을, 그렇지 않으면 null 핸들을 반환합니다. |
| `expired() const` | 대상을 더 이상 잠글 수 없는지 반환합니다. |
| `reset()` | 이 weak 참조를 지웁니다. |

```VeroScript
class Session
{
public:
	int id = 0;
};

Session owner = Session();
weak<Session> observer = weak<Session>(owner);

Session locked = observer.lock();
if (locked != nullptr)
{
	print(locked.id);
}
```

weak 값을 복사하면 같은 대상을 관찰하는 별도의 weak 관찰이 생성됩니다. 한 복사본을 재설정해도 다른 복사본은 재설정되지 않습니다. 강한 클래스 핸들이 모두 사라지면 `expired()`가 true가 되고 `lock()`은 null 핸들을 반환합니다. 멤버에 접근하기 전에 항상 결과를 확인하세요.

## `shared<T>` / `handle<T>`

`handle<T>`은 하나의 형식 매개 변수와 `value`라는 public 필드를 가진 컴파일러 제공 제네릭 클래스입니다. 구조체 값을 공유 클래스 ID 뒤에 배치해야 할 때 유용합니다. 기본 생성자는 핸들을 만들고 값 유형인 경우 `value`를 기본 생성합니다. `T`가 클래스이면 페이로드는 클래스 핸들 의미 체계를 따르며 기본값은 null 핸들입니다. `handle<T>(value)`는 페이로드를 새 공유 객체에 복사합니다.

`shared<T>`는 `handle<T>`의 권장 표기입니다. 두 이름은 같은 형식이며 컨테이너, 제네릭 함수 및 `weak<T>` 안에서도 함께 사용할 수 있습니다. `std::shared<T>`와 `std::handle<T>`도 같은 형식을 나타냅니다. 명시적 `shared<T>()`는 null이 아닌 래퍼를 생성하고 `nullptr` 대입은 핸들을 비웁니다. 구조체는 생성 시 래퍼에 복사되며 이후 핸들 복사는 같은 객체를 공유합니다. `weak<shared<T>>`는 래퍼를 관찰하며 `lock()`은 같은 객체의 강한 핸들 또는 만료 후 `nullptr`를 반환합니다. `.value` 접근 전에 잠금 결과를 확인하세요. `shared`는 예약된 내장 형식 별칭이며 구문 키워드가 아닙니다. 기존 소프트 제네릭 규칙에 따라 같은 이름과 같은 형식 매개 변수 수의 사용자 `handle<T>`가 있으면 두 표기는 모두 해당 사용자 형식을 가리킵니다.

페이로드 인수가 하나이면 `auto h = shared(value)`에서 `T`를 추론합니다. 인수 없는 생성에는 명시적인 대상 타입 또는 타입 인수가 필요합니다.

소스 출력에는 백엔드별 제한이 있습니다. C++ 출력은 공유 래퍼와 약한 참조를 지원합니다. C# 출력은 공유 래퍼를 지원하지만 구조체 또는 열린 제네릭 페이로드 생성에는 `shared<Point>(Point())`처럼 값을 명시적으로 전달해야 하며, 현재 `weak<T>`는 지원하지 않습니다. 독립 Python 소스 출력은 공유 래퍼와 약한 참조를 지원하지 않지만 Python 런타임 바인딩은 코어를 통해 둘 다 지원합니다. 전체 수명 예제는 VM, Mixed, JIT 백엔드에서 실행할 수 있습니다.

### 생성과 공유

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void bump_x(handle<Point> point)
{
	point.value.x += 1;
}

shared<Point> first = shared<Point>(Point());
handle<Point> second = first;
first.value.x = 10;
bump_x(second);
print(first.value.x);
```

`first`를 `second`에 할당하면 클래스 핸들이 복사되므로 두 변수는 같은 `value`를 관찰하고 수정합니다. `nullptr`를 할당하면 핸들이 비워집니다. `T`가 구조체이면 `handle<T>(source)`는 새 페이로드 복사본을 만들므로 이후 `source`를 변경해도 그 복사본은 바뀌지 않습니다.

`handle<T>`은 공유 래퍼이며 일반적인 클래스의 대체물이 아닙니다. `h.value`의 구조체를 인터페이스로 변환하면 값 복사본이 박싱됩니다. 인터페이스가 공유되는 변경 가능한 ID를 노출해야 한다면 해당 인터페이스를 구현하는 클래스를 사용하세요.

완전한 실행 가능 예제는 [examples/handle_struct.vs](../../../../Scripts/examples/handle_struct.vs)입니다.

## `pair<T, U>`

`pair<T, U>`는 다음 두 public 필드를 가진 컴파일러 제공 제네릭 구조체입니다.

| 필드 | 유형 |
| --- | --- |
| `first` | `T` |
| `second` | `U` |

맵 API는 키/값 항목을 `pair`로 표현합니다. `m.entries()`는 값으로 된 `pair<K, V>` 스냅샷 배열을 반환합니다.

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (pair<string, int> entry : scores.entries())
{
	print("{} -> {}", entry.first, entry.second);
}
```

맵 자체를 순회하면 쓰기 가능한 항목 뷰를 얻을 수 있습니다. `pair<K, V>&`는 맵에 다시 쓰고 `pair<K, V>`는 복사본입니다.

```VeroScript
for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}

for (auto& [key, value] : scores)
{
	value += 1;
}
```

`entries()`가 반환한 `pair`를 원래 맵 항목에 대한 참조로 취급하지 마세요. 전체 범위 순회 규칙은 [참조](references.md)와 [컨테이너](containers.md)를 참조하세요. 실행 가능한 맵 예제는 [examples/pair_map.vs](../../../../Scripts/examples/pair_map.vs)입니다.

## 이름과 호환성

`optional`, `readonly`, `weak` 및 리플렉션 정보 유형은 엄격하게 예약된 내장 유형 이름입니다. 스크립트에서 다시 선언할 수 없습니다.

`pair<T, U>`와 `handle<T>`는 소프트 제네릭 내장 유형입니다. 컴파일러는 같은 이름과 같은 제네릭 인수 개수의 사용자 선언이 없을 때만 두 개의 형식 매개 변수를 가진 `pair`와 하나를 가진 `handle`을 제공합니다. 제네릭이 아닌 `pair` 또는 `handle`, 혹은 다른 개수의 선언은 해당 소프트 제네릭 형식과 공존할 수 있습니다.

표준 네임스페이스 별칭은 `std::optional`, `std::weak`, `std::shared`, `std::handle`, `std::pair`입니다. 이러한 별칭은 별도의 유형을 만들지 않습니다.

## 관련 참조

- [데이터 유형](data-types.md): 내장 유형, 컨테이너 및 읽기 전용 래퍼.
- [제네릭](generics.md): 사용자 정의 제네릭 유형 및 소프트 내장 유형.
- [형식 변환](../language-reference/type-conversions.md): `cast`, `try_cast`, `const_cast` 및 변환 결과.
- [컨테이너](containers.md): 맵 작업 및 범위 순회.

스크립트 객체의 소멸이 시작되면 약한 참조 수명도 끝납니다. 소멸 중과 소멸 후 lock()은 null, expired()는 true를 반환합니다. 소멸자에서 this를 외부에 저장해도 객체가 되살아나지 않으며 이후 필드 및 메서드 접근은 거부됩니다. 호스트 객체는 기존 호스트 수명 계약을 따릅니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/value-utility-types.md) · [fr-FR](../../fr-FR/language-basics/value-utility-types.md) · [ja-JP](../../ja-JP/language-basics/value-utility-types.md) · [ko-KR](value-utility-types.md) · [zh-CN](../../zh-CN/language-basics/value-utility-types.md) · [zh-TW](../../zh-TW/language-basics/value-utility-types.md)
