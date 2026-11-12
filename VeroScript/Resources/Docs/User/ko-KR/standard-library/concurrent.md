# 동시성 모듈

모듈 파일: `concurrent.vs`

다음을 사용하여 가져옵니다.

```VeroScript
import "concurrent.vs";
```

## 채널

`channel<T>`은 작업 간 통신에 사용되는 공유 컨테이너입니다. 표준 라이브러리는 모든 전송 가능한 채널 요소 유형에 대한 일반 작업을 공개합니다.

| 기능 | 설명 |
| --- | --- |
| `std::concurrent::channel_send<T>(channel<T> ch, T value)` | 하나의 값을 채널로 보냅니다. |
| `std::concurrent::channel_recv<T>(channel<T> ch)` | 채널에서 하나의 값을 받습니다. |
| `std::concurrent::channel_close<T>(channel<T> ch)` | 채널을 닫습니다. |
| `std::concurrent::make_channel<T>(int64 capacity = 0)` | 무제한 또는 제한된 채널을 만듭니다. |
| `ch.try_send(value)` / `ch.send_for(value, timeout_ms)` | 블로킹 없이 또는 시간 제한으로 보내며, 보낼 수 없으면 `false`를 반환합니다. |
| `ch.try_recv()` / `ch.recv_for(timeout_ms)` | `optional<T>`를 반환하며, 비어 있으면 비어 있음, 시간 초과 또는 닫힘을 뜻합니다. |
| `ch.pending()` / `ch.capacity()` / `ch.is_closed()` | 큐 깊이와 용량은 음수가 아닌 `int64`(용량 `0`은 무제한), 닫힘 상태는 `bool`을 반환합니다. |

더 짧은 `send`, `recv` 및 `close` 형식은 네임스페이스 호출과 확장 구문(`ch.send(value)`, `ch.recv()` 및 `ch.close()`)을 모두 지원합니다. `T`는 채널이 지원하는 전송 가능한 스칼라(부호 있는/없는 정수 너비, `float`, `double`, `bool`, `string` 또는 `wstring`)이거나 다른 `channel<U>`일 수 있습니다. 중첩 채널은 일반 가변 객체를 공유하지 않고 격리된 작업자 사이에 동기화 기능을 안전하게 전달합니다. 열린 채널에서 수신하는 것은 기다릴 수 있습니다. 채널을 닫은 후 유효한 보내기 또는 받기가 필요한 작업을 계속하지 마십시오.

채널 값은 공유 핸들 의미 체계를 사용합니다.

## 격리된 작업

`std::concurrent::spawn`은 격리된 작업을 시작하고 작업 ID를 반환합니다. 작업자 진입점은 채널 유형과 일치해야 합니다.

```VeroScript
import "concurrent.vs";

void worker(channel<int> ch)
{
	int value = ch.recv();
	ch.send(value + 1);
}

int main()
{
	channel<int> ch = channel<int>();
	int task_id = std::concurrent::spawn("worker", ch);
	ch.send(41);
	std::concurrent::join_task(task_id);
	int result = ch.recv();
	print(result);
	return 0;
}
```

`std::concurrent::join_task`은 작업이 완료될 때까지 기다립니다. 작업은 일반 스크립트 로컬을 직접 공유하는 대신 채널을 통해 지원되는 값을 교환해야 합니다.

`std::concurrent::spawn<T>`은 명명된 작업자에 정확히 일치하는 항목 서명이 있는 경우 지원되는 모든 `channel<T>`을 허용합니다.

```text
int std::concurrent::spawn<T>(string name, channel<T> ch);
```

## 입력된 작업 결과

`std::concurrent::spawn_task<T>`은 결과 채널을 통해 하나의 결과를 게시하는 작업자에 대해 `task<T>`을 반환합니다.

```VeroScript
import "concurrent.vs";

void compute(channel<int> output)
{
	output.send(42);
}

int main()
{
	std::task<int> result = std::concurrent::spawn_task<int>("compute");
	int value = result.await();
	print(value);
	return 0;
}
```

문자열 진입점은 정확히 `void compute(channel<T> output)`을 지정하며 반환 전 결과 하나를 전송해야 합니다. 종료 후 누락/복수 결과는 오류이고 아직 차단된 worker는 협력적 취소가 필요합니다.

`task<T>`은 지원되는 모든 전송 가능한 채널 유형에 대해 다음 멤버를 제공합니다.

| 회원 | 설명 |
| --- | --- |
| `valid()` | 작업이 여전히 활성 상태인지 테스트합니다. |
| `done()` | 기다리지 않고 작업 완료 여부를 테스트합니다. |
| `await()` | 작업을 기다리고 하나의 결과를 받은 다음 비활성으로 표시합니다. |
| `await_for(timeout_ms)` | 완료 대기 시간 초과만 빈 optional을 반환하고 작업을 유지합니다. 종료 후 결과 누락/복수 결과는 오류입니다. |
| `cancel()` | 협력적으로 작업을 취소하고 결과 채널을 닫습니다. VM은 종료될 때까지 작업자를 유지합니다. |

협력 취소는 스레드를 강제로 종료하거나 분리하지 않습니다. 작업자는 채널 닫힘을 확인하고 종료해야 하며, 호스트 callback 수명을 보호하기 위해 VM/session 종료는 취소를 무시하는 작업자를 기다립니다.

## callable 작업 진입점

`concurrent.vs`를 가져온 뒤 확인된 스크립트 함수, 정적 메서드 또는 함수 값(람다 포함)을 `std::concurrent::spawn(callable, 인수...)` 또는 `std::concurrent::spawn_task<T>(callable, 인수...)`에 직접 전달할 수 있습니다. 서로 다른 타입의 인수는 왼쪽부터 한 번 평가됩니다. `std::concurrent::spawn`은 `void` 반환을, `std::concurrent::spawn_task<T>`는 `T`와 정확히 일치하는 전송 가능한 반환값을 요구합니다. 기존 문자열 진입점도 계속 사용할 수 있습니다.

```VeroScript
import "concurrent.vs";

int twice(int value)
{
	return value * 2;
}

int main()
{
	std::task<int> work = std::concurrent::spawn_task<int>(twice, 21);
	int factor = 3;
	std::task<int> captured = std::concurrent::spawn_task<int>(
		[factor](int value) -> int => value * factor, 7);
	print(work.await());
	print(captured.await());
	return 0;
}
```

인수와 값 캡처에는 전송 가능한 스칼라, 문자열 또는 채널 핸들만 사용할 수 있습니다. 참조 캡처, `this` 캡처, 인스턴스 메서드, 일반 가변 객체, 배열, `ref` 매개변수 및 참조 반환값은 worker 경계를 넘을 수 없습니다. worker는 독립된 VM과 입력 스냅샷을 사용하며 `await()`는 현재 스레드를 계속 차단합니다. 취소는 협력적이며 worker는 `std::concurrent::cancel_requested()`를 확인할 수 있습니다. C++, Python, C# AOT는 현재 callable 진입점을 컴파일 시 명확히 거부합니다. [동시성 예제](../../../../Scripts/examples/concurrent_demo.vs)를 참조하세요.

## 동기화 기능

`task_group<T>`은 같은 유형의 작업을 묶고 `add`, `await_all`, `cancel_all`을
제공합니다. 대응하는 free helper는 `std::concurrent::when_all`과
`std::concurrent::cancel_all`입니다.

`mutex`와 `counting_semaphore`는 동기화된 channel handle의 별칭이며 중첩 channel을
통해 전달할 수 있습니다. `std::concurrent::make_mutex`, `std::concurrent::mutex_lock`, `std::concurrent::mutex_try_lock`,
`std::concurrent::mutex_lock_for`, `std::concurrent::mutex_unlock`, `std::concurrent::make_semaphore`, `std::concurrent::semaphore_acquire`,
`std::concurrent::semaphore_try_acquire`, `std::concurrent::semaphore_acquire_for`, `std::concurrent::semaphore_release`를 사용합니다.
이중 unlock과 semaphore 최대값 초과는 runtime error입니다.

`std::concurrent::make_synchronized<T>(initial)`은 용량 1의 channel cell을 만듭니다.
`std::concurrent::synchronized_load`, `std::concurrent::synchronized_store`, `std::concurrent::synchronized_exchange`,
`std::concurrent::synchronized_fetch_add`는 접근을 직렬화하지만 hardware `atomic<T>`의 lock-free
보장을 의미하지 않습니다.

## 작업 취소 요청

`std::concurrent::cancel_task(int id)`는 유효한 작업 ID에 협력적 취소를 요청하며 ID를 소비하지 않습니다. 이후 `std::concurrent::join_task(id)`로 기다릴 수 있습니다. 잘못된 ID는 런타임 오류입니다. `std::concurrent::cancel_requested()`는 요청된 작업자 안에서 `true`, 작업자 밖에서 `false`입니다. 작업자는 시간 제한 대기 사이에 이를 확인해야 합니다. 차단된 작업을 중단하거나 스레드를 강제로 종료하지 않습니다. `task<T>.cancel()`은 결과 채널도 닫습니다.

`pending(ch)`, `capacity(ch)` 및 `task_group<T>.size()`는 음수가 아닌 `int64`를 반환합니다. `std::concurrent::make_channel<T>`의 용량은 `int64`이며 `0`은 무제한 채널, 양수는 제한된 채널을 지정합니다. 음수 또는 표현할 수 없는 용량은 오류이며 실제 할당에는 호스트 제한이 적용됩니다. 작업 ID는 계속 `int` 타입입니다.

- [표준 라이브러리 기반](foundation.md)

<a id="concurrency-aot-boundary"></a>

독립 AOT 경계: task<T>, task_group, when_all 및 mutex/semaphore/synchronized는 문자열 spawn_task를 포함하여 Core의 능력 식별, 소유자 및 소비 상태 검사가 필요합니다. C++/Python/C# 독립 AOT는 출력 전에 이 작업을 명확히 거부합니다. Core VM/Mixed/JIT로 실행하세요. C++/Python의 기존 일반 channel, 문자열 spawn 및 join 경로는 유지됩니다.

소유 Core VM이 끝나면 소유 워커의 Core 채널 대기를 중단한 뒤 워커 종료를 기다립니다. 직접 인수로 전달된 채널을 닫는 기존 동작은 유지되며 이 알림 자체는 다른 채널을 닫지 않습니다. 명시적 취소는 계속 협력 방식이며 분리된 워커는 독립적이고 차단되는 호스트 콜백은 호스트의 협력이 필요합니다.

<!-- docs-navigation -->

---

[사용자 문서](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ko-KR/index.md)

언어: [en-US](../../en-US/standard-library/concurrent.md) · [fr-FR](../../fr-FR/standard-library/concurrent.md) · [ja-JP](../../ja-JP/standard-library/concurrent.md) · [ko-KR](concurrent.md) · [zh-CN](../../zh-CN/standard-library/concurrent.md) · [zh-TW](../../zh-TW/standard-library/concurrent.md)
