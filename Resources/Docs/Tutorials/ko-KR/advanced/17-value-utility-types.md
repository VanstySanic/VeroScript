# 17: 선택적 값, weak 참조 및 공유 구조체 핸들

이 튜토리얼에서는 VeroScript의 컴파일러 제공 유틸리티 유형을 사용하여 작은 상태 공유 예제를 만듭니다. `optional<T>`, `weak<T>`, `handle<T>` 및 `pair<T, U>`를 언제 사용해야 하는지와 값 및 소유권 규칙의 차이를 배웁니다.

`shared<T>`는 `handle<T>`의 권장 표기입니다. 두 이름은 같은 형식이며 컨테이너, 제네릭 함수 및 `weak<T>` 안에서도 함께 사용할 수 있습니다. `std::shared<T>`와 `std::handle<T>`도 같은 형식을 나타냅니다. 명시적 `shared<T>()`는 null이 아닌 래퍼를 생성하고 `nullptr` 대입은 핸들을 비웁니다. 구조체는 생성 시 래퍼에 복사되며 이후 핸들 복사는 같은 객체를 공유합니다. `weak<shared<T>>`는 래퍼를 관찰하며 `lock()`은 같은 객체의 강한 핸들 또는 만료 후 `nullptr`를 반환합니다. `.value` 접근 전에 잠금 결과를 확인하세요. `shared`는 예약된 내장 형식 별칭이며 구문 키워드가 아닙니다. 기존 소프트 제네릭 규칙에 따라 같은 이름과 같은 형식 매개 변수 수의 사용자 `handle<T>`가 있으면 두 표기는 모두 해당 사용자 형식을 가리킵니다.

## 목표

이 튜토리얼을 마치면 다음을 할 수 있어야 합니다.

- 센티넬 값을 사용하지 않고 없을 수도 있는 결과를 표현합니다.
- 객체를 살아 있게 유지하지 않고 클래스를 관찰합니다.
- 클래스 핸들을 통해 구조체 페이로드를 공유합니다.
- 맵 항목 스냅샷과 쓰기 가능한 항목 뷰를 구분합니다.

전체 참조는 [값 유틸리티 유형](../../../../../VeroScript/Resources/Docs/User/ko-KR/language-basics/value-utility-types.md)입니다. 각 예제를 실행하기 전에 `--check`로 확인하세요.

## 1단계: 선택적 결과 표현

빈 결과에는 `optional<T>()`를 사용하고 값을 포함하는 결과에는 `optional<T>(value)`를 사용합니다. `get()`을 호출하기 전에 `has_value()`를 확인하세요.

```VeroScript
optional<int> find_score(bool available)
{
	if (available)
	{
		return optional<int>(95);
	}
	return optional<int>();
}

int main()
{
	optional<int> missing = find_score(false);
	print(missing.value_or(0));

	optional<int> found = find_score(true);
	if (found.has_value())
	{
		print(found.get());
	}

	return 0;
}
```

기본값이면 충분할 때는 `value_or`가 유용합니다. 명시적으로 상태를 확인한 후에는 `get()`을 사용할 수 있습니다. 빈 optional에서 호출하면 `assert`를 통해 실패합니다. `try_cast<T>`도 실패할 수 있는 변환에 같은 결과 패턴을 사용합니다.

## 2단계: 소유하지 않고 클래스 관찰

클래스 변수는 강한 핸들입니다. `weak<T>` 값은 객체를 관찰하지만 객체를 계속 살아 있게 하지는 않습니다.

```VeroScript
class Session
{
public:
	int id = 0;
};

int main()
{
	Session owner = Session();
	owner.id = 42;

	weak<Session> watcher = weak<Session>(owner);
	if (!watcher.expired())
	{
		Session locked = watcher.lock();
		print(locked.id);
	}

	owner = nullptr;
	print(watcher.expired());
	return 0;
}
```

`lock()`은 객체가 살아 있으면 강한 핸들을, 그렇지 않으면 null 핸들을 반환합니다. 멤버에 접근하기 전에 항상 `expired()`를 확인하거나 `lock()`의 결과를 확인하세요. weak 값을 복사하면 별도의 관찰이 생성되며 한 복사본을 재설정해도 다른 복사본은 재설정되지 않습니다.

## 3단계: 구조체 페이로드 공유

구조체 할당은 일반적으로 값을 복사합니다. 프로그램의 여러 부분에서 하나의 구조체 페이로드를 수정해야 한다면 `handle<T>`에 넣으세요.

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void move_right(handle<Point> point)
{
	point.value.x += 1;
}

int main()
{
	shared<Point> first = shared<Point>(Point());
	handle<Point> second = first;

	first.value.x = 10;
	move_right(second);
	print(first.value.x);
	print(second.value.x);

	return 0;
}
```

`handle<T>`를 다른 값에 할당하면 클래스 핸들이 복사되므로 두 변수는 같은 `value`를 봅니다. `handle<T>(source)`는 대신 페이로드를 새 공유 객체에 복사합니다. `nullptr`를 할당하면 핸들이 지워집니다. 전체 예제는 [examples/handle_struct.vs](../../../../../VeroScript/Resources/Scripts/examples/handle_struct.vs)입니다.

## 4단계: `pair` 뷰 선택

맵 API는 `pair<K, V>`를 사용합니다. `entries()`의 결과는 값 스냅샷 배열이므로 스냅샷을 변경해도 맵은 변하지 않습니다. 맵을 `pair<K, V>&`로 직접 순회하면 쓰기 가능한 항목 뷰를 얻습니다.

```VeroScript
int main()
{
	ordered_map<string, int> scores = ordered_map<string, int>();
	scores["alice"] = 95;
	scores["bob"] = 88;

	for (pair<string, int> snapshot : scores.entries())
	{
		print("{} -> {}", snapshot.first, snapshot.second);
	}

	for (pair<string, int>& entry : scores)
	{
		entry.second += 1;
	}

	for (auto& [key, value] : scores)
	{
		print("{} -> {}", key, value);
	}

	return 0;
}
```

독립된 항목 값의 모음이 필요하면 `m.entries()`를 사용하세요. 항목에 다시 쓰려면 맵을 직접 순회하세요. 전체 예제는 [examples/pair_map.vs](../../../../../VeroScript/Resources/Scripts/examples/pair_map.vs)입니다.

## 연습 문제

1. `find_score`를 수정하여 정수 대신 `optional<string>` 메시지를 반환합니다.
2. 두 번째 `weak<Session>` 관찰자를 추가하고 한 관찰자를 재설정해도 다른 관찰자가 재설정되지 않는지 확인합니다.
3. `handle<Point>` 예제에 `y` 방향 이동 함수를 추가하고 두 번째 핸들을 통해 호출합니다.
4. `map<string, Point>`를 추가하고 `pair<string, Point>&`를 통해 중첩 필드를 업데이트합니다.

## 기억할 내용

- `optional<T>`는 비어 있거나 값이 있는 값을 표현하는 값 유형입니다.
- `weak<T>`는 클래스의 수명을 연장하지 않고 관찰합니다.
- `handle<T>`는 페이로드를 담은 public 필드 `value`를 가진 클래스 객체를 공유합니다.
- `entries()`의 `pair<K, V>`는 값 스냅샷이고, 맵을 직접 순회한 pair 참조는 다시 씁니다.

다음: [std 표준 네임스페이스 사용](14-standard-namespace.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/advanced/17-value-utility-types.md) · [fr-FR](../../fr-FR/advanced/17-value-utility-types.md) · [ja-JP](../../ja-JP/advanced/17-value-utility-types.md) · [ko-KR](17-value-utility-types.md) · [zh-CN](../../zh-CN/advanced/17-value-utility-types.md) · [zh-TW](../../zh-TW/advanced/17-value-utility-types.md)
