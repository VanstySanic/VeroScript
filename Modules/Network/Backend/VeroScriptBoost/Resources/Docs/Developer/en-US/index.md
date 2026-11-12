# VeroScriptBoost

Boost owns Asio/Beast, OpenSSL integration and `Private/Runtime` (`VeroScriptBoostRuntime`). Keep these concrete implementations and third-party source inventories in this module; the frontend SDK remains independent of those types.

Shared context/operation ownership, registry composition, SDK boundaries and build options are maintained in [Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/en-US/index.md).

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [User](../../User/en-US/index.md)

<a id="provider-registration"></a>

The `boost` provider registers `network` (`veroscript.network`), plus `http-server` (`veroscript.http.server`) and `websocket-client` / `websocket-server` (`veroscript.websocket`) when those protocol targets are enabled. These contracts use version `0.1.1`. Factory payloads are respectively `Network::Backend::IService*`, `Http::Backend::IServerService*` and `WebSocket::Backend::IService*`. Factories are stateless, registration starts no I/O, and unknown configuration is rejected. Dynamic discovery uses `veroscript_extension_query`; static registration uses `veroscript_boost_query` / `Boost::QueryModule`. Only Boost owns `VeroScriptBoostRuntime`; frontends and Curl must not install a second copy. Qualify enabled/disabled protocol metadata, leases and relocated consumers against matching foundation/Core identities.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
