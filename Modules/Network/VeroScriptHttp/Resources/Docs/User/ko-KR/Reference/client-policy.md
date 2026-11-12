# HTTP 클라이언트 정책

`FClient(Context, Service, FClientOptions)`는 연결 풀과 Cookie 정책을 고정합니다. Cookie와 자동 재시도는 기본적으로 꺼져 있습니다. `MaxConnections=0`은 컨텍스트 한도를 사용하며 호스트별 한도, 유휴 연결 수와 보관 시간도 적용됩니다. `MaxPendingRequests`에는 연결 대기와 재시도 대기가 포함되고 원래 기한을 공유합니다. 초과하면 `QueueFull`입니다. `bInheritProxyEnvironment=true`가 아니면 환경 프록시를 무시하며, 비어 있지 않은 명시적 `Proxy`가 우선합니다. 프로세스 환경은 변경하지 않습니다.

`Send`는 전체 응답을 보관하므로 `MaxBodyBytes`가 context 버퍼 예산과 플랫폼의 `Network::FBytes::max_size()` 이하여야 합니다. 초과하면 요청 제출 시 `InvalidArgument`가 발생합니다. `SendStream`은 전체 본문에 64비트 한도를 유지하고 대기 메모리는 `BufferBytes`로 별도 제한합니다.

Cookie 는 client 별 격리, MaxCookieBytes 를 보수적으로 예약하고 초과 Limit 와 store 삭제이며 물리 heap cap 은 아닙니다. Cookies/ClearCookies/ImportCookies 는 Netscape 기록 검증과 스냅샷/개수입니다. 영속화 명시, cwd 파일 없음, 복구 불가 backend 할당 실패는 일부 업데이트를 되돌리지 않을 수 있습니다.

기본 1 회/최대 32 회, 여러 번은 bAllowReplay, unsafe 메서드는 bAllowUnsafeMethods 도 필요합니다. 일회 소스는 replay 불가입니다. UploadFactory 는 매 시도/redirect rewind 마다 새 소스/길이를 I/O 에서 즉시 제공하고 script/차단 불가, null/소비/예외/길이 불일치는 실패입니다. 상태/오류/상한 지수 지연/Retry-After 는 같은 기한이며 cancel 은 timer 를 없앱니다. curl 오류는 Resolve/Connect/Read/Write/TimedOut/Allocation, 기타 Protocol, SystemCode 유지와 기존 소스 오류 보존입니다. head/body 공개 뒤 stream retry 는 불가합니다.

Attempts/FAttemptHistory 는 불변 진단/error/bFinal/bTruncated 이며 final 은 물리 해제가 아닙니다. 동시 재사용 불가, final 뒤 재사용은 새 기록으로 옛 스냅샷은 유지합니다. history/result 는 Owner/할당량 보존, 잘린 진단이 원래 오류를 바꾸지 않습니다. MaxTotalBodyBytes 는 해독/redirect 본문, Header 는 전체 헤더, Upload 는 MIME framing 없는 소스입니다. 버퍼는 제출/replay 선차감, 스트림은 수락 시 계산, 단일 상한도 적용합니다. 예약은 wire 통계가 아닙니다. set_redirect_limit(0), client/retry options, set_retry_options, total_*_limit 및 Cookie/history 작업으로 제어합니다.

## 예제

```VeroScript
http::client_options options = http::make_client_options();
options.set_cookies(true);
options.set_max_cookie_bytes(8192);
http::client client = http::create_client_with_options(context, options);

http::retry_options retry = http::make_retry_options();
retry.set_max_attempts(3);
retry.set_allow_replay(true);
retry.set_delay_ms(100);
request.set_retry_options(retry);
```

## 기본값

| API | 값 |
| --- | --- |
| `MaxConnectionsPerHost / MaxIdleConnections / MaxPendingRequests` | `8 / 64 / 128` |
| `MaxConnectionIdleTime / MaxCookieBytes` | `120 s / 1 MiB` |
| `FRetryOptions::Delay / MaxDelay` | `100 ms / 30000 ms` |
| `bHonorRetryAfter / bRetryTransportErrors` | `true / true` |
| `Statuses` | `408, 429, 500, 502, 503, 504` |

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/client-policy.md) · [fr-FR](../../fr-FR/Reference/client-policy.md) · [ja-JP](../../ja-JP/Reference/client-policy.md) · [ko-KR](client-policy.md) · [zh-CN](../../zh-CN/Reference/client-policy.md) · [zh-TW](../../zh-TW/Reference/client-policy.md)
