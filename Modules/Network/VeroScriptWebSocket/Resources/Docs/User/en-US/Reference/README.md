# WebSocket — Network reference

Shared frontend installation and backend-selection rules are specified in [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/en-US/index.md#backend-selection).

`vs-websocket` pins matching `vs-network`; native ID is websocket, script namespace websocket, CMake target VeroScript::WebSocket, optional VeroScript::WebSocketBindings with the matching Core. Load network before websocket and declare dependencies. Standalone WebSocket does not require Http/curl/Json. Optional C++ Json composition uses COMPONENTS Json.

FConnection supports ws/wss clients/servers, text/binary, optional compression, automatic Pong and bounded queues. Native reads continue without a pending script receive, so Ping/close can progress. Context operation, dispatch, deadline, lifetime and quota rules apply. Script connect/listen and TLS/with_options variants return protocol-specific operations; wrong result type, unfinished or failed result access fails. Options are value snapshots. Keep the shared BoostRuntime with deployed native providers; all native components must match Core/build/platform/toolchain.

- [Completion and program lifetime](completion.md)
- [Diagnostics and localization](diagnostics.md)
- [JSON adapters](json-adapters.md)
- [TLS configuration](tls.md)
- [WebSocket compression](websocket-compression.md)
- [WebSocket CONNECT proxy](websocket-proxy.md)
- [Bounded WebSocket reconnect](websocket-reconnect.md)
- [WebSocket messages and control frames](websocket.md)

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
