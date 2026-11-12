# 컨테이너

VeroScript 컨테이너 유형은 일반 인수를 사용하여 해당 요소 또는 키/값 유형을 설명합니다. 고정 크기 기본 배열을 제외하고 컨테이너는 일반적으로 공유 핸들로 표시됩니다. 컨테이너를 다른 변수에 할당하면 두 변수가 모두 동일한 컨테이너 개체를 참조하게 될 수 있습니다.

## 공통 규칙

컨테이너 유형은 유형 인수에 꺾쇠 괄호를 사용하고 `len`은 해당 요소 또는 항목 수를 반환합니다.

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();

print(len(values));
print(len(counts));
```

삽입된 요소는 선언된 요소 유형과 일치해야 합니다. 구조체와 같은 값 유형은 삽입 시 복사됩니다. 클래스 및 기타 핸들 유형은 핸들 의미 체계를 유지합니다.

대부분의 컨테이너는 범위 반복을 지원합니다. 값 루프 변수는 요소 복사본을 받습니다. `T&` 루프 변수는 컨테이너의 요소를 수정할 수 있습니다. 맵은 값, 키, 키/값 입력 반복도 지원합니다.

## `map` 및 `ordered_map`

`map<K, V>`은 키를 값과 연결하고 인덱싱을 사용하여 값에 액세스합니다.

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;
scores["bob"] = 88;

print(scores["alice"]);
print(scores.has("bob"));
```

일반적인 작업은 다음과 같습니다.

| 운영 | 설명 |
| --- | --- |
| `m[key]` | 키 값 읽기 또는 쓰기 |
| `m.has(key)` | 키가 존재하는지 테스트 |
| `m.remove(key)` | 키를 제거하고 제거 여부를 반환 |
| `m.keys()` | 키를 `array<K>`로 반환 |
| `m.values()` | 값을 `array<V>`으로 반환 |
| `m.entries()` | 키/값 항목을 배열로 반환 |
| `m.clear()` | 지도 지우기 |
| `len(m)` | 키 수를 반환 |

`map`은 순서가 지정되지 않았습니다. `keys()`에서 반환된 주문이 지정되지 않았습니다. `ordered_map`는 고유 키의 첫 번째 삽입 순서를 유지합니다. 기존 키를 업데이트해도 이동되지는 않습니다.

## `multimap` 및 `ordered_multimap`

`multimap<K, V>`을 사용하면 하나의 키가 여러 값을 가질 수 있습니다. 값을 추가하려면 `add`을 사용하세요. 인덱싱은 키와 연결된 `array<V>`를 반환합니다.

```VeroScript
multimap<string, int> grouped = multimap<string, int>();
grouped.add("alpha", 1).add("alpha", 2);

print(grouped["alpha"][0]);
print(len(grouped["alpha"]));
```

`remove(key)`은 해당 키 아래에 저장된 모든 값을 제거합니다. `ordered_multimap`은 추가로 고유 키의 첫 번째 삽입 순서를 유지합니다.

## `set` 및 `multiset`

`set<T>`은 고유한 요소를 저장합니다. 기존 요소를 추가해도 두 번째 요소가 생성되지 않습니다.

```VeroScript
set<string> names = set<string>();
names.add("alice");
names.add("alice");
print(names.has("alice"));
```

`remove(value)`은 요소를 제거하고 `clear()`은 세트를 비웁니다. `multiset<T>`는 반복되는 값을 허용하고 `count(value)`을 제공합니다.

```VeroScript
multiset<int> numbers = {2, 1, 2, 3, 2};
print(numbers.count(2));
numbers.remove(2);
```

`multiset`의 경우 `remove(value)`은 하나의 항목을 제거합니다. `ordered_multiset`는 고유 값의 첫 번째 삽입 순서를 유지합니다.

## 큐, 스택, 우선순위 큐 및 데크

| 유형 | 의미론 | 일반적인 작업 |
| --- | --- | --- |
| `queue<T>` | 선입선출 | `push`, `pop`, `front` |
| `stack<T>` | 후입선출 | `push`, `pop`, `top` |
| `priority_queue<T>` | 우선순위에 따라 제거합니다. 현재 구현에서는 먼저 최대값을 반환합니다 | `push`, `pop`, `top` |
| `deque<T>` | 양쪽 끝의 삽입 및 제거와 정수 인덱싱 | `push_front`, `push_back`, `pop_front`, `pop_back` |

예를 들어 `queue<T>`은 뒤쪽에 추가하고 앞쪽에서 제거하는 반면, `stack<T>`은 위쪽에서 추가하고 제거합니다. 현재 요소 수를 얻으려면 `len`를 사용하십시오.

## 지도 반복

맵은 값이나 키/값 항목을 기준으로 반복될 수 있습니다.

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (int value : scores)
{
	print(value);
}

for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}
```

`scores.keys()`을 반복하고 `scores[key]`을 사용하여 각 값에 액세스할 수도 있습니다. 순서가 지정되지 않은 맵의 순서는 프로그램 논리로 사용되어서는 안 됩니다.

### 스냅샷과 항목 뷰

`m.entries()`는 값 스냅샷인 `array<pair<K, V>>`를 반환합니다. `m` 자체를 순회하여 얻은 `pair<K, V>&`는 맵 항목에 대한 쓰기 가능한 뷰입니다. `m.entries()`의 요소에 대한 참조를 맵 내부의 참조처럼 취급하지 마세요. 직접 항목 순회 또는 `m[key]`를 사용하여 맵을 수정하세요.

## `channel`

`channel<T>`은 동시 작업 간 통신에 사용되는 공유 핸들입니다. `concurrent.vs` 표준 라이브러리는 `channel_send`, `channel_recv` 및 `channel_close` 함수를 제공합니다.

```VeroScript
import "concurrent.vs";

channel<int> values = channel<int>();
std::concurrent::channel_send(values, 42);
int result = std::concurrent::channel_recv(values);
std::concurrent::channel_close(values);
```

채널은 일반적으로 `spawn`, `join` 및 작업 기능과 함께 사용됩니다. 차단 동작, 전송 가능한 유형 및 작업 수명은 일반 컨테이너 참조가 아닌 동시성 런타임 참조에 속합니다.

컨테이너 길이와 `multiset.count(value)` 등의 요소 수는 음수가 아닌 `int64`를 반환합니다. Deque 인덱스는 `int64`를 사용하고 map 키는 선언된 키 타입을 유지합니다. 길이 변환과 범위는 [내장 함수](../standard-library/builtins.md), 채널 개수와 용량은 [동시성](../standard-library/concurrent.md)을 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/containers.md) · [fr-FR](../../fr-FR/language-basics/containers.md) · [ja-JP](../../ja-JP/language-basics/containers.md) · [ko-KR](containers.md) · [zh-CN](../../zh-CN/language-basics/containers.md) · [zh-TW](../../zh-TW/language-basics/containers.md)
