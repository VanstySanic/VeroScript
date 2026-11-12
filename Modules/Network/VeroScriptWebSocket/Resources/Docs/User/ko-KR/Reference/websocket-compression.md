# WebSocket 압축

CompressionSupported 는 빌드 능력, bCompression 은 요청(기본 false), CompressionInfo 는 실제 불변 결과입니다. 상대가 거부하면 일반 메시지를 사용하며 결과는 닫힌 뒤도 읽습니다. 창 기본값은 양방향 15, 허용 9–15, takeover 플래그 false, Level=8(0–9), MemoryLevel=4(1–9)입니다. client/server 는 프로토콜 방향이며 미협상은 disabled/창 0 입니다.

8 비트 창을 9 로 바꾸지 않습니다. 서버는 불가 제안을 건너뛰고 클라이언트는 불가 선택을 거부합니다. 잘못된 로컬 설정은 InvalidArgument, 미지/중복/누락/미지원 협상은 코덱 생성 전 Protocol 입니다. 도착 순서의 첫 지원 제안을 선택하며 다중 필드/목록/인용 숫자를 처리합니다.

한도는 해독된 애플리케이션 바이트이므로 압축으로 우회할 수 없습니다. 초과 Limit, DEFLATE/프레임 오류 Protocol, 정상 Closed, 읽기 Read, 유휴 TimedOut 입니다. 완성 큐를 비운 뒤도 최초 종결 원인을 유지합니다. 코덱 전 1 MiB 를 예약하고 협상 실패 시 반환, 성공 시 실제 코덱 수명까지 유지하며 정확한 물리 메모리 피크는 아닙니다.

압축을 빌드에서 끄면 활성화가 Unsupported 이며 일반 ws/wss 는 계속 사용할 수 있습니다. 설치 descriptor 의 websocketCompression 을 확인합니다. compression_options 의 창/수준/메모리/takeover getter/setter, set_compression_options 복사 및 connection.compression 결과를 사용합니다. 텍스트/바이너리/빈 메시지/조각/제어 의미는 변하지 않습니다.

## 예제

```VeroScript
websocket::compression_options compression = websocket::make_compression_options();
compression.set_server_window_bits(12);
compression.set_client_no_context_takeover(true);
websocket::options options = websocket::make_options();
options.set_compression(true);
options.set_compression_options(compression);
// Query after a successful connection.
websocket::compression_info actual = connection.compression();
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/websocket-compression.md) · [fr-FR](../../fr-FR/Reference/websocket-compression.md) · [ja-JP](../../ja-JP/Reference/websocket-compression.md) · [ko-KR](websocket-compression.md) · [zh-CN](../../zh-CN/Reference/websocket-compression.md) · [zh-TW](../../zh-TW/Reference/websocket-compression.md)
