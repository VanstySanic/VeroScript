# SSE 클라이언트

FSseClient 는 유한 HTTP 스트림을 사용합니다. Poll, 접근자와 Close 는 하나의 논리 호출자가 사용하며 같은 객체에서 동시에 호출하지 않습니다. 연결별 작업 스레드를 만들거나 I/O 스레드에서 스크립트를 실행하지 않습니다. Connect 는 본문 없는 GET 만 허용하고 upload, factory, length, multipart 는 거부합니다. Accept: text/event-stream 과 Last-Event-ID 는 클라이언트가 관리하며 CA, proxy 및 명시한 요청 제한은 유지합니다.

기본 전체 기한은 무제한, 헤더/본문의 유휴 기한은 60 초입니다. 일반 HTTP 의 30 초를 자동 적용하지 않으며 명시적으로 교체한 Request 는 그 요청의 기한을 유지합니다. 기본 총 본문은 무제한, 링은 256 KiB, 행은 64 KiB, 이벤트는 1 MiB, 한 Poll 은 64 KiB 입니다. context 가 링/파서 저장소를 계산하고 이벤트 복사본은 호출자가 관리합니다. 빌린 FSseWork 는 시작과 각 블록 전 비용 0 으로 취소를 확인한 후 실제 소비한 최대 8 KiB 블록만 계산합니다. 접미부를 중복 계산하지 않습니다. false 는 Cancelled, 예외는 Callback 이며 스크립트 취소/예산 소진은 VM 실패로 전파합니다.

Poll 상태는 0 Pending, 1 EventReady, 2 Reconnecting, 3 Closed, 4 Failed 입니다. EventReady 만 Out 을 바꿉니다. HTTP 200 과 올바른 미디어 형식이 필요하고 204 는 재연결을 중지합니다. 잘못된 상태/형식도 ResponseHead 를 보존하며 EOF 에서 미완료 이벤트를 만들어 내지 않습니다.

재연결은 기본 무제한, 초기 3 초, 최대 60 초입니다. 네트워크 오류/시간 초과는 재시도하지만 프로토콜/설정/파싱 한도/할당 실패는 재시도하지 않습니다. MaxReconnects 는 전체 수명에 적용하고 0 이면 비활성화합니다. 소진 후 정상 EOF 는 Closed, 네트워크 실패는 Failed 입니다. 서버 retry 는 유지하되 최대 지연으로 제한합니다. ID 는 완전한 빈 줄 구분자에서 확정하며 data 없는 ID 갱신도 포함하고, 다음 이벤트에 새 ID 가 없으면 상속합니다. Close 는 멱등이며 새 요청은 Poll 중에만 시작합니다. 업무 메시지를 자동 재전송하지 않습니다.

스크립트 옵션은 limits/reconnect/idle_timeout(-1 은 해제)/request 입니다. connect_sse 및 connect_sse_with_options 로 만들고 poll/close/event_*/last_event_id/response_status/reconnect_count/reconnect_remaining/error_code/error 를 사용합니다. event_*는 직전 poll 이 1 일 때만 읽습니다. 지속적인 서버 SSE 는 reply_stream 을 쓰며 버퍼 응답 reply_response 와 구분합니다.

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/sse-client.md) · [fr-FR](../../fr-FR/Reference/sse-client.md) · [ja-JP](../../ja-JP/Reference/sse-client.md) · [ko-KR](sse-client.md) · [zh-CN](../../zh-CN/Reference/sse-client.md) · [zh-TW](../../zh-TW/Reference/sse-client.md)
