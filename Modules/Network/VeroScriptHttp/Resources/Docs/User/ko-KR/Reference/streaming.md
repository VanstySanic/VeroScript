# 유한 바이트 스트림

SendStream 은 Operation/Body/TryHead 를 반환하며 pending 은 false/오류 없음, head 는 불변이고 response.Body 중복 없이 끝에 BodyBytes/trailers 입니다. 버퍼는 ≥16 KiB 및 context 이하, 총 본문은 별도입니다. 완료 대기 전 전송 중 비웁니다. curl 블록 pause/replay 중복 없이 방향 독립, 다른 연결은 진행합니다. reader 취소/해제는 요청 취소이며 기존 바이트를 읽고 종결 오류를 얻습니다. 전체 TLS/curl/HTTP2/OS 메모리 cap 은 아닙니다.

Upload 는 단일 reader, 길이 선택/backend framing, MaxUploadBytes=64 MiB 입니다. 알려진 짧은 입력은 오류, WouldBlock 은 write/Finish 대기입니다. 일회 소스 재사용/버퍼/multipart/HEAD/자동 redirect 불가, native factory 로 명시 replay 가능합니다. Content-Length/Transfer-Encoding 은 backend 소유입니다. script set_upload(length=-1)/upload_limit/send_stream/operation/body/head_ready/head/body_bytes, VM 예산과 HostDriven poll/yield 를 유지합니다.

ReplyStream 은 헤더/고정 trailer 를 고정하고 BodySource 와 비어 있지 않은 Body 는 배타적입니다. 상태 0 Uncommitted/1 HeadersSent/2 BodyStreaming/3 Finished/4 Aborted, BodyBytes 는 완전 기록 블록입니다. 완료는 로컬 수락이며 원격 처리/보낸 바이트 rollback 이 아닙니다. HTTP1.1 미지 길이/trailer 는 chunked, 아니면 길이, 1.0 미지 길이는 close/trailer 불가입니다. HEAD 는 헤더, 204/304 는 본문 없음/소스 취소입니다. 길이 Protocol/상한 Limit, framing 재구성, trailer 에 framing/connection/routing 불가입니다. precommit 인수 오류는 exchange 미소비, 블록≤16 KiB 와 파이프 별도 계산, 실패/취소/기한은 close 입니다. ResponseTimeout 은 idle, options.Timeout 은 total 이며 SSE 는 total 만 해제할 수 있습니다. script reply_stream 길이/total -1, 같은 context, operation/state/body_bytes, trailer 는 제출 전입니다.

bStreamRequests 기본 false, true 는 헤더 뒤 Accept/빈 Request.Body/단일 BodyStream/Operation, 최종 body_bytes/trailers 와 불변 초기 head, 100-continue 후 읽기입니다. 기본 버퍼 64 KiB/총량 MaxStreamBodyBytes/블록≤16 KiB/용량입니다. 소비하거나 취소하고 본문과 응답 모두 종료 후 다음 요청, 이전 exchange 재사용은 불가입니다. 입출력 독립, 옵션 stream/buffer/body/header/pending/time/TLS 는 값 복사, 버퍼 모드의 stream 접근은 오류입니다.

Shutdown 은 신규 연결/요청/Accept 를 멈추고 미전달/idle 을 닫으며 전달한 exchange 는 grace 내 완료, 다음 요청 없음입니다. 만료 강제 bForced, ClosedConnections 는 시작 시 active 수로 물리 해제가 아닙니다. grace0–365 일과 total 기한, 취소/기한/Close 는 원래 오류, 동시 shutdown State, 이미 닫힘은 성공/0 입니다. 핸들을 유지하며 파괴는 Close 입니다. script shutdown/forced/connections 와 대소문자 무시 header/trailer 는 형식 결과 규칙을 따릅니다.

## 예제

```VeroScript
Network::byte_pipe output = Network::create_byte_pipe(context, 65536);
http::response_builder response = http::create_response(200, "");
response.set_header("Content-Type", "text/event-stream");
http::response_stream_operation sending = exchange.reply_stream(response, output.reader(), -1, -1);
Network::writable_stream writer = output.writer();
writer.write(http::encode_sse("hello", "", ""));
// On WouldBlock yield; retry the unaccepted suffix on a later tick.
writer.finish();
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](../../fr-FR/Reference/streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
