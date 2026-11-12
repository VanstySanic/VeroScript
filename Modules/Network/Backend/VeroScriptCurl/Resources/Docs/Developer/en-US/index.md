# VeroScriptCurl

Curl owns the concrete HTTP client in `Private/HttpClient.cpp`, curl dependency configuration and original curl notices. Its provider implements the Http contract; it does not own Boost transport or WebSocket services.

Shared context/operation ownership, registry composition, SDK boundaries and build options are maintained in [Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/en-US/index.md).

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [User](../../User/en-US/index.md)

<a id="provider-registration"></a>

The `curl` provider registers `http-client` / `veroscript.http.client` / `0.1.1` with an `Http::Backend::IClientService*` factory payload. Factories are stateless; registration starts no I/O and rejects unknown configuration. Dynamic discovery uses `veroscript_extension_query`; static registration uses `veroscript_curl_query` / `Curl::QueryModule`. Curl consumes opaque non-owning socket-readiness/timer hooks and has no Boost header or library dependency. Its HTTPS backend follows curl’s own TLS configuration. Keep bundled curl patches, static dependency deployment and Unix system-library requirements with this module; qualify missing/incompatible services, leases and relocated SDK/package consumers against the actual build.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
