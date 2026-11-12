# 09: 동시성과 이벤트

공유 상태에서는 동시성이 어려워집니다. 이 튜토리얼에서는 일반 지역 변수를 공유하는 대신 채널을 통해 값을 보낸 다음 멀티캐스트 대리자를 사용하여 여러 핸들러에 알립니다.

## 1단계: 작업이 값을 처리하도록 합니다.

```VeroScript
import "concurrent.vs";

void worker(channel<int> input)
{
	int value = input.recv();
	input.send(value + 1);
}

int twice(int value)
{
	return value * 2;
}

int main()
{
	channel<int> pipe = channel<int>();
	int task_id = std::concurrent::spawn(worker, pipe);

	pipe.send(41);
	std::concurrent::join_task(task_id);

	int result = pipe.recv();
	assert(result == 42);
	task<int> doubled = std::concurrent::spawn_task<int>(twice, 21);
	assert(doubled.await() == 42);
	print(result);
	return 0;
}
```

작업은 `channel<int>`을 통해 값을 받고 보냅니다. `std::concurrent::spawn`은 해석된 스크립트 함수, 정적 메서드 또는 함수 값을 받아 작업 ID를 반환하고, `join_task`는 완료될 때까지 기다립니다. `spawn_task<T>`도 같은 callable 형식을 받아 `task<T>.await()`로 형식이 지정된 결과를 반환합니다. 인수와 람다 캡처는 스칼라, 문자열, 채널 핸들과 같은 전송 가능한 값이어야 하며 각 작업은 격리된 VM에서 실행됩니다. `send`, `recv` 및 `close`는 VeroScript의 균일한 확장 호출 구문을 통해 노출되는 일반적인 표시 자유 함수입니다. 일반 지역 변수는 작업 통신 채널을 대체할 수 없습니다. callable 작업 지원 여부는 백엔드에 따라 다르며 제한 사항은 동시성 참조 문서를 확인하세요.

취소는 협력 방식입니다. 프로그램의 다른 부분에서 `std::concurrent::cancel_task(id)`를 호출한 뒤 실행 중인 작업은 `std::concurrent::cancel_requested()`를 확인할 수 있습니다. 요청은 차단된 작업을 중단하거나 작업을 강제로 종료하지 않습니다. `task<T>.cancel()`은 해당 작업의 결과 채널도 닫습니다.

## 2단계: 결과 방송

```VeroScript
import "delegate.vs";

int main()
{
	multicast_delegate<int> on_result = multicast_delegate<int>();
	on_result.add(function(int value) -> void => print("log={}", value));
	on_result.add(function(int value) -> void => print("double={}", value * 2));

	on_result.broadcast(21);
	print("listeners={}", on_result.count());
	return 0;
}
```

멀티캐스트 대리자는 여러 `void` 처리기를 저장합니다. `broadcast`은 현재 등록 순서로 이를 호출합니다. 이는 하나의 결과가 여러 개의 독립적인 응답을 트리거해야 하는 경우 유용합니다.

## 메커니즘 선택

- 함수 호출: 호출자는 함수를 알고 있으며 즉각적인 결과가 필요합니다.
- `delegate`: 교체 가능한 핸들러 1개.
- `multicast_delegate`: 여러 처리기에 대한 알림입니다.
- `channel<T>`: 작업 간에 값이 교환됩니다.
- `spawn_task<T>`: callable을 실행하고 기다릴 수 있는 형식 지정 결과를 생성하는 작업입니다.
- `make_synchronized<T>`, mutex 및 semaphore: 전송 가능한 공유 상태를 위한 명시적 동기화 핸들입니다.

대기순서에 주의하세요. 수신자가 발신자를 기다리는 동안 발신자가 수신자를 기다리는 것이 가장 일반적인 교착 상태 패턴입니다. `spawn` 및 `join_task`을 추가하기 전에 메시지 흐름을 디자인하십시오.

## 연습

1. `worker`을 변경하여 두 개의 정수를 받고 그 합계를 반환합니다.
2. 결과가 `40`보다 큰 경우에만 인쇄하는 핸들러를 추가합니다.
3. `channel<string>`를 통해 상태 문자열을 보내고 기본 기능에서 채널을 닫습니다.

실행 가능한 예: [`examples/concurrent_demo.vs`](../../../../../VeroScript/Resources/Scripts/examples/concurrent_demo.vs) 및 [`examples/delegates.vs`](../../../../../VeroScript/Resources/Scripts/examples/delegates.vs). 전체 규칙은 [동시성](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/concurrent.md) 및 [이벤트](../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/events.md)를 참조하세요.

다음: [패키지 빌드 및 애플리케이션 배포](../toolchain/10-packages-and-distribution.md).

<!-- docs-navigation -->

---

[튜토리얼](../index.md)

언어: [en-US](../../en-US/standard-library/09-concurrency-and-events.md) · [fr-FR](../../fr-FR/standard-library/09-concurrency-and-events.md) · [ja-JP](../../ja-JP/standard-library/09-concurrency-and-events.md) · [ko-KR](09-concurrency-and-events.md) · [zh-CN](../../zh-CN/standard-library/09-concurrency-and-events.md) · [zh-TW](../../zh-TW/standard-library/09-concurrency-and-events.md)
