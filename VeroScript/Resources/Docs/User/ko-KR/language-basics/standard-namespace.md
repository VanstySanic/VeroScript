# `std` 표준 네임스페이스

VeroScript는 내장 컨테이너, 유틸리티 값 유형, 리플렉션 유형 및 공용 런타임 함수에 대한 언어 예약 `std` 네임스페이스를 제공합니다. 새 코드는 정규화된 `std::` 철자를 사용해야 합니다. 모듈 유틸리티는 책임 기반 하위 네임스페이스를 사용합니다. `std::contains` 및 `std::read_file`과 같은 더 이상 사용되지 않는 기본 형식은 별칭이 아닙니다.

## 해결 규칙

`std::name` 및 `std.name`이 모두 허용됩니다. 공개 코드는 `std::name`를 사용해야 합니다. 컴파일러는 이러한 이름을 기존 언어 또는 표준 라이브러리 선언으로 확인하므로 별칭은 두 번째 컨테이너 ABI를 생성하거나 런타임 디스패치를 추가하지 않습니다. 형식 지정 함수는 공용 컴파일러에서 제공하는 진입점입니다. 다른 소스 표준 라이브러리 함수는 여전히 일반적인 가져오기 규칙을 따릅니다.

```VeroScript
import "math.vs";

int main()
{
	std::array<int> values = {2, 3, 5};
	std::optional<int> selected = std::optional<int>(values[0]);

	std::print("selected={}, root={}", selected.get(), std::sqrt(81.0));
	return 0;
}
```

언어 기본 형식과 런타임 직접 함수는 정규화되지 않은 형식도 유지합니다. 모듈 소유 API는 의도적으로 네임스페이스 전용이며, 모듈을 가져온 뒤 `std::algorithm::sort`처럼 호출합니다. 이는 이전 `std` 이름을 위한 호환성 계층이 아닙니다.

`std`은 예약되어 있습니다. 스크립트는 `std` 또는 해당 하위 네임스페이스 중 하나에 선언을 추가할 수 없습니다. 네임스페이스 별칭은 모듈 가시성을 변경하지 않습니다. 소스 표준 라이브러리 함수에 대한 별칭은 여전히 이를 제공하는 모듈을 가져와야 합니다.

## 유형 별칭

다음 표에는 공개 유형 별칭이 나열되어 있습니다. 오른쪽은 기존 언어 또는 반영 유형 ID입니다.

| `std::` 철자법 | 표준 이름 |
| --- | --- |
| `string`, `wstring` | `string`, `wstring` |
| `array`, `map`, `multimap` | 같은 이름의 컨테이너 |
| `ordered_map`, `ordered_multimap` | 같은 이름의 순서 지도 |
| `set`, `multiset`, `ordered_multiset` | 같은 이름의 세트 |
| `queue`, `priority_queue`, `stack`, `deque` | 동일한 이름의 시퀀스 컨테이너 |
| `channel`, `task`, `concurrent::task_group`, `concurrent::mutex`, `concurrent::counting_semaphore` | 채널, 동시 작업, 뮤텍스 및 세마포어 유형 |
| `pair` | `pair` |
| `optional`, `weak` | 같은 이름의 선택적 유형과 약한 유형 |
| `readonly` | 읽기 전용 값 래퍼 |
| `shared`, `handle` | `handle` |
| `delegate`, `multicast_delegate` | `delegate`, `multicast_delegate` |
| `type`, `field_info`, `method_info` | 반사 정보 유형 |
| `module_info`, `function_info`, `global_info` | 모듈, 기능 및 전역 정보 유형 |
| `enum_member_info` | Enum-member 정보 유형 |

별칭은 소스 철자만 변경합니다. 일반 인수는 여전히 원래 유형 규칙을 사용하여 확인되므로 `std::map<string, int>`은 2개 매개변수 맵으로 유지됩니다.

## 변환 표기

`std::readonly<T>`는 `readonly<T>`의 표준 네임스페이스 표기입니다. 변환 내장 함수는 전역으로 유지되므로 `std::` 접두사 없이 `cast<T>`, `try_cast<T>`, `const_cast<T>` 및 괄호 형식 `(T)expression`을 사용합니다. `std::cast<T>` 및 `std::try_cast<T>`와 같은 형식은 문서화된 별칭이 아닙니다. 동작과 실패 규칙은 [형식 변환](../language-reference/type-conversions.md)을 참조하세요.

```VeroScript
std::readonly<int> published = std::readonly<int>(41);
double precise = cast<double>(published.get());
optional<int> narrowed = try_cast<int>(precise);
```

## 함수 별칭

### 직접 런타임 기능

이러한 이름은 런타임에서 직접 제공되는 내장 기능 또는 함수를 나타냅니다.

| 카테고리 | 기능 |
| --- | --- |
| 출력 및 형식화 | `print`, `eprint`, `printf`, `format`, `formatf`, `assert`, `std::console::{write, error, flush, clear, is_interactive}` |
| 길이 및 변환 | `len`, `to_string`, `to_wstring`, `to_int`, `to_unsigned`, `to_int64`, `to_uint64`, `to_double`, `to_float` |
| 숫자 | `abs`, `min`, `max` |
| 런타임 유틸리티 | `range`, `exit` |

시간, 콘솔, 스레드 및 환경 함수는 책임 기반 이름인 `std::chrono::now`,
`std::chrono::steady_now`, `std::this_thread::sleep`, `std::environment::getenv` 및
`std::environment::setenv`만 사용합니다. 해당하는 일반 `std` 별칭은 유지되지 않습니다.

```VeroScript
int main()
{
	std::print("elapsed={}", std::chrono::now() > 0.0);
	string mode = std::environment::getenv("MODE");
	std::this_thread::sleep(1);
	return 0;
}
```

### 모듈 기능

다음 이름은 내보낸 표준 라이브러리 함수의 표준 네임스페이스 철자입니다. 여전히 일반적인 `import` 규칙을 따릅니다.

| 모듈 | 기능 |
| --- | --- |
| `io.vs` | `std::io::{read_file, write_file, append_file, read_line, pause, read_token, read_string, read_int, read_double, read_bool}` |
| `io.vs` | `std::filesystem::{delete_file, exists, is_file, is_directory, remove_dir, list_dir, mkdir, cwd, chdir}` |
| `string.vs` | `std::strings::{substring, find, last_find, char_code, contains, starts_with, ends_with, trim_left, trim_right, trim, split, join, to_lower, to_upper, replace, repeat, remove_prefix, remove_suffix}` |
| `path.vs` | `std::filesystem::{basename, dirname, join_path, extension, change_extension}` |
| `concurrent.vs` | `std::concurrent::{channel_send, channel_recv, channel_close, send, recv, close, make_channel, try_send, send_for, try_recv, recv_for, is_closed, pending, capacity, spawn, spawn_task, join_task, cancel_task, cancel_requested, when_all, cancel_all, make_mutex, mutex_lock, mutex_try_lock, mutex_lock_for, mutex_unlock, make_semaphore, semaphore_acquire, semaphore_try_acquire, semaphore_acquire_for, semaphore_release, make_synchronized, synchronized_load, synchronized_store, synchronized_exchange, synchronized_fetch_add}` |
| `math.vs` | 단순 숫자 이름: `std::{sqrt, sin, cos, tan, floor, ceil, round, trunc, pow, exp, log, log2, asin, acos, atan2, isfinite, isnan, pi, e, clamp, clamp_int}` |
| `algorithm.vs` | `std::algorithm::{sort, reverse, find_index, count, lower_bound, binary_search, fill, copy, contains, equal, is_sorted, min_index, max_index, rotate, shuffle}` |
| `numeric.vs` | `std::numeric::{accumulate, sum, average}` |
| `random.vs` | `std::random::{make_engine, seed, next_uint, next_bounded_uint, next_int, next_double, next_double_range, next_bool}` |

예를 들어 `std::sqrt`은 `math.vs`을 가져오기 때문에 이전 샘플에서 사용할 수 있습니다. `std::sqrt`만 작성하면 표준 라이브러리 모듈이 로드되지 않습니다. `std::format` 및 `std::formatf`는 컴파일러에서 제공하는 서식 지정 진입점이며 해결하기 위해 `string.vs`가 필요하지 않습니다.

## 통일된 내선 호출

가시적 자유 함수는 첫 번째 인수를 수신자로 사용하여 호출할 수 있습니다.

```VeroScript
int grow(ref Counter self, int amount)
{
	/* ... */
}

counter.grow(2); // grow(ref counter, 2)
```

실제 스크립트, 가상, 호스트 및 내장 멤버가 항상 승리합니다. 실제 멤버가 일치하지 않는 경우에만 컴파일러는 동등한 자유 함수 호출을 시도합니다. 일반 추론, 오버로드 해결, 가져오기, 패키지 가시성, 기본값, 변환 및 `ref`/`const ref` 규칙은 일반 호출과 공유됩니다. 참조 수신기는 암시적으로 바인딩됩니다. 두 가지 형식이 모두 존재하는 경우 수신기를 복사하는 것보다 호환 가능한 참조-수신기 오버로드가 선호됩니다. 이 낮추기는 컴파일 타임에만 적용됩니다.

`concurrent.vs`은 채널에 대해 다음 메커니즘을 사용합니다.

```VeroScript
import "concurrent.vs";

channel<int> messages = channel<int>();
messages.send(42);
int value = messages.recv();
messages.close();
```

## 경계

- 스크립트는 `namespace std`을 선언하거나 `std` 이름 트리 아래에 사용자 유형 또는 함수를 등록할 수 없습니다.
- `std::` 별칭은 모듈 가져오기가 아닙니다. 모듈은 `import`, `export` 및 패키지 가시성 규칙에 의해 계속 제어됩니다.
- `__`로 시작하는 이름에 의존하지 마십시오. 문서화된 `std::` 철자법 및 표준 라이브러리 페이지는 안정적인 공용 인터페이스입니다.

전체 별칭 테이블은 런타임 표준 라이브러리 레지스트리에 의해 유지됩니다. 일반 네임스페이스 규칙은 [네임스페이스](namespaces.md)를, 모듈 서명은 [표준 라이브러리 참조](../standard-library/index.md)를 참조하세요.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/language-basics/standard-namespace.md) · [fr-FR](../../fr-FR/language-basics/standard-namespace.md) · [ja-JP](../../ja-JP/language-basics/standard-namespace.md) · [ko-KR](standard-namespace.md) · [zh-CN](../../zh-CN/language-basics/standard-namespace.md) · [zh-TW](../../zh-TW/language-basics/standard-namespace.md)
