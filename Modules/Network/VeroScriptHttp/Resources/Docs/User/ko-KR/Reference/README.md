# Http — 네트워크 참조

공통 프런트엔드 설치와 백엔드 선택 규칙은 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ko-KR/index.md#backend-selection)에 정의합니다.

vs-http 0.1.1 은 같은 vs-network 에 의존하고 WS/JSON 은 필수가 아닙니다. ID/namespace http, 대상 VeroScript::Http, Bindings 는 일치 Core 의 VeroScript::HttpBindings 입니다. network 를 먼저 등록하고 의존 가시성을 선언하며 공개 curl/Boost 형식은 없습니다. 버퍼/스트림 client/server, routing, multipart, SSE 와 파일을 create_client/create_request/listen/create_response 및 형식 결과로 사용합니다. NUL 보존, 옵션은 제출 시 값 스냅샷이며 기한/파견/수명/할당량은 Network 입니다. 4xx/5xx 만으로 전송 실패는 아닙니다. 본문 setter 는 충돌 모드를 지우고 form 만 +/공백 변환, multipart 는 반복/바이너리를 유지합니다. 버퍼 응답은 response_builder/reply_response, add_header/set_header 는 추가/교체입니다. SSE reader/encoder 가 재연결을 추가하지 않으며 C++ COMPONENTS Json 은 선택입니다.

- [HTTP 클라이언트 정책](client-policy.md)
- [완료 알림과 프로그램 수명](completion.md)
- [진단과 현지화](diagnostics.md)
- [파일과 원자적 출력](files.md)
- [HTTP 응답 체인과 통계](http-trace.md)
- [JSON 어댑터](json-adapters.md)
- [multipart 전송과 파싱](multipart.md)
- [HTTP 라우팅](routing.md)
- [SSE 클라이언트](sse-client.md)
- [유한 바이트 스트림](streaming.md)
- [TLS 구성](tls.md)
- [HTTP 에서 WebSocket 으로 전환](upgrade.md)

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
