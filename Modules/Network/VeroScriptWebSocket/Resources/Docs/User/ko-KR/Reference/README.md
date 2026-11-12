# WebSocket — 네트워크 참조

공통 프런트엔드 설치와 백엔드 선택 규칙은 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ko-KR/index.md#backend-selection)에 정의합니다.

`vs-websocket` 0.1.1은 일치하는 `vs-network`에 의존합니다. 네이티브 ID와 스크립트 네임스페이스는 `websocket`, CMake 대상은 `VeroScript::WebSocket`입니다. 선택적인 `VeroScript::WebSocketBindings`에는 일치하는 Core가 필요합니다. Network를 먼저 등록하고 공개 의존성을 선언하세요. Http, curl, Json은 필수가 아니며 JSON 연동에는 `COMPONENTS Json`을 사용합니다.

`FConnection`은 ws/wss 클라이언트와 서버, 텍스트/바이너리 메시지, 선택적 압축, 자동 Pong 및 유한 큐를 제공합니다. 스크립트 수신 대기가 없어도 네이티브 읽기가 계속되어 Ping과 종료를 처리합니다. 기한, 콜백 전달, 할당량 및 수명은 Network 계약을 따릅니다. 잘못된 형식, 미완료 또는 실패한 결과에 대한 접근은 거부되며 옵션은 값 스냅샷입니다. 공급자와 함께 BoostRuntime을 배포하고 모든 네이티브 구성 요소의 Core, 빌드, 플랫폼 및 도구 체인을 일치시키세요.

- [completion](completion.md)
- [diagnostics](diagnostics.md)
- [json-adapters](json-adapters.md)
- [tls](tls.md)
- [websocket-compression](websocket-compression.md)
- [websocket-proxy](websocket-proxy.md)
- [websocket-reconnect](websocket-reconnect.md)
- [websocket](websocket.md)

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
