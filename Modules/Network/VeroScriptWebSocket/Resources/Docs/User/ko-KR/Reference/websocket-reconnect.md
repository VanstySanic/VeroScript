# 유한 WebSocket 재연결

ConnectWithRetry 는 명시적 연결 라운드 하나를 만들며 이전 핸들을 교체하거나 업무 메시지를 재전송하지 않습니다. MaxAttempts 기본 1/최대 32 는 첫 시도를 포함합니다. Delay≥0, MaxDelay≥Delay, 기본 상한 지수 백오프입니다. 양수 MaxElapsed 는 최대 1 년이며 queue/proxy/TLS/handshake/대기를 포함합니다. 개별 기한은 남은 시간으로 줄이고 Wait 가 전체 기한을 바꾸지 않습니다.

Resolve/Connect/Read/Write/Closed/TimedOut 은 재시도하며 Protocol 은 bRetryProtocolErrors 일 때만 허용합니다. InvalidArgument/Unsupported/QueueFull/Limit/Allocation/Cancelled 는 재시도하지 않습니다. 비동기 타이머는 HostDriven PollIo 가 필요하지만 스크립트 dispatch 와 독립입니다. 전체 operation 슬롯 하나와 현재 시도 최대 하나, 실제 연결 할당량을 사용합니다.

취소는 현재/대기/향후 시도를 중지하지만 원격이 요청을 받지 않았다고 보장하지 않습니다. 물리 정리는 Shutdown 을 따르고 세대/시도 ID 가 이전의 지연 완료를 차단합니다. 확인/순서/중복 제거는 애플리케이션 책임입니다.

TrySnapshot 은 소유 복사, Snapshot 은 할당량을 유지하는 불변 공유 값입니다. Generation/Attempts/bFinal/Error 와 시도별 Number/bComplete/bConnected/Error 를 제공합니다. 안정/시스템 코드를 보존하며 텍스트는 UTF-8 경계의 4 KiB 와 bMessageTruncated 입니다. 접수 전 기록이 없으면 Operation.Error 를 확인합니다. 닫힌 뒤도 복사할 수 있고 새 I/O/빌린 VM 은 없습니다. 스크립트 policy/limits/backoff/retry 설정과 connect_with_retry, 보고서 generation/is_final/size/at/오류 접근자를 사용합니다. 시도의 number/complete/connected 는 보고서를 해제해도 원래 스냅샷을 보유합니다.

## 예제

```cpp
WebSocket::FReconnectPolicy policy;
policy.MaxAttempts = 3;
policy.Delay = std::chrono::milliseconds(100);
policy.MaxDelay = std::chrono::seconds(2);
policy.MaxElapsed = std::chrono::seconds(15);
auto task = WebSocket::ConnectWithRetry(context, url, policy, options);
auto operation = task.Operation();
```

```VeroScript
websocket::reconnect_policy policy=websocket::make_reconnect_policy();
policy.set_limits(3,15000);
policy.set_backoff(100,2000,true);
websocket::reconnect_task task=websocket::connect_with_retry(context,url,options,policy);
websocket::operation opening=task.operation();
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/websocket-reconnect.md) · [fr-FR](../../fr-FR/Reference/websocket-reconnect.md) · [ja-JP](../../ja-JP/Reference/websocket-reconnect.md) · [ko-KR](websocket-reconnect.md) · [zh-CN](../../zh-CN/Reference/websocket-reconnect.md) · [zh-TW](../../zh-TW/Reference/websocket-reconnect.md)
