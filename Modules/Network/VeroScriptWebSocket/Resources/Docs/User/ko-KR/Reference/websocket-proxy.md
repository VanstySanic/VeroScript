# WebSocket CONNECT 프록시

네이티브 ws/wss 는 명시적 HTTP CONNECT 를 지원하며 Http/curl 이 필요 없습니다. http://host:port 와 빈 경로 또는 /만 허용하고 query/fragment 는 거부합니다. 다른 scheme 은 InvalidArgument 이며 환경 proxy 를 읽거나 바꾸지 않습니다. URL/인증은 각각 4096 바이트 이하, 인증은 제어 문자 없는 전체 필드 값입니다.

proxy DNS/TCP/CONNECT/목표 TLS/WS handshake 가 원래 기한과 취소를 공유합니다. SNI/CA/신원은 목표 호스트를 검증하고 인증 필드는 proxy 에만 보냅니다. 2xx 는 성공, 1xx 는 최대 8 회이며 성공 뒤 Content-Length/Transfer-Encoding 과 무관하게 터널로 전환하고 선독 바이트를 보존합니다. 헤더 최대 64 KiB, 비 2xx/잘림/무효/초과는 연결을 닫습니다. 실제 연결 해제까지 320 KiB 를 예약하며 전체 TLS/OS 메모리 한도는 아닙니다.

클라이언트 전용으로 Listen/AcceptUpgrade 는 거부합니다. set_proxy/proxy_url/clear_proxy 는 두 값을 함께 지우며 제출 시 옵션을 복사합니다. 브라우저 proxy 동작은 별개입니다. proxy.vs 는 echo 서비스 및 proxy URL/목표 URL/인증/선택 CA 를 받으며 업무 프로토콜은 호출자가 처리합니다.

## 예제

```cpp
WebSocket::FOptions options;
options.ProxyUrl = "http://127.0.0.1:8080";
options.ProxyAuthorization = "Basic ..."; // Full proxy Authorization value; empty if not required.
options.Operation.Timeout = std::chrono::seconds(10);
auto operation = WebSocket::FConnection::Connect(context, "wss://example.com/socket", options);
```

```VeroScript
websocket::options options=websocket::make_options();
options.set_proxy("http://127.0.0.1:8080", "");
options.set_timeout(10000);
websocket::operation opening=websocket::connect_with_options(context,"ws://example.com/socket",options);
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/websocket-proxy.md) · [fr-FR](../../fr-FR/Reference/websocket-proxy.md) · [ja-JP](../../ja-JP/Reference/websocket-proxy.md) · [ko-KR](websocket-proxy.md) · [zh-CN](../../zh-CN/Reference/websocket-proxy.md) · [zh-TW](../../zh-TW/Reference/websocket-proxy.md)
