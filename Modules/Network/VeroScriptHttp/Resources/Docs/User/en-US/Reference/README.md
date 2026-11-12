# Http — Network reference

Shared frontend installation and backend-selection rules are specified in [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/en-US/index.md#backend-selection).

`vs-http` depends on the matching `vs-network`, not WebSocket or JSON. Native registration ID is http, script namespace http, CMake target VeroScript::Http; optional Bindings supplies VeroScript::HttpBindings with the matching Core. Load network first, then http and declare visible dependencies. Native public headers expose no curl/Boost types.

The API supplies buffered/streaming clients and servers, routes, multipart, SSE and files. Script entry points include create_client, create_request, listen, create_response and typed operation result access. Strings preserve binary NUL. Options are value copies and are snapshotted on submission. Operation deadlines, creator-thread completion dispatch, retained-memory accounting and cancellation follow Network. HTTP status 4xx/5xx is not by itself a transport error.

Forms and ordinary body setters replace conflicting body modes. URL-encoded forms explicitly use form percent coding, including plus/space; multipart preserves repeated fields and binary bytes. Buffered server replies use response_builder and exchange.reply_response; add_header/set_header preserve their documented duplicate/replacement semantics. SSE reader/encoder operate on event data without adding a reconnect loop; use the SSE client topic for reconnection. Optional C++ JSON composition requires COMPONENTS Json and does not force JSON into the base package.

- [HTTP client policy](client-policy.md)
- [Completion and program lifetime](completion.md)
- [Diagnostics and localization](diagnostics.md)
- [Files and atomic output](files.md)
- [HTTP response chains and statistics](http-trace.md)
- [JSON adapters](json-adapters.md)
- [Multipart upload and parsing](multipart.md)
- [HTTP routing](routing.md)
- [SSE client](sse-client.md)
- [Bounded byte streams](streaming.md)
- [TLS configuration](tls.md)
- [HTTP to WebSocket upgrade](upgrade.md)

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
