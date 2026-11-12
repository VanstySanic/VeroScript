# Network

Install the frontend packages for the APIs you use and the selected native backends. `vs-boost` supplies `network`, and its enabled HTTP-server/WebSocket services; `vs-curl` supplies `http-client`. Frontend installation alone supplies no transport. Missing or incompatible services report an error.

<a id="backend-selection"></a>

In scripts, `Network::create_context_with_backend("boost", false)` selects the executor explicitly. HTTP client/server, SSE and WebSocket options use `set_backend` to override the environment default. An empty request inherits `services.<service>.backend`; if it is absent, `auto` tries enabled providers in backend-ID order. Explicit `auto` overrides a configured default; `none` or an explicitly empty configuration value disables the service. Missing required services fail clearly; runtime failures do not replay operations on another backend. `boost` and `curl` are provider IDs, not fixed defaults. C++ consumers supply typed service instances; these script-selection tokens do not replace the instance-matching contract of C++ options.

`vs-network` · `VeroScript::Network`

- [Completion and program lifetime](Reference/completion.md)
- [Diagnostics and localization](Reference/diagnostics.md)
- [Files and atomic output](Reference/files.md)
- [Network reference](Reference/README.md)
- [Bounded byte streams](Reference/streaming.md)
- [TLS configuration](Reference/tls.md)

[Boost](../../../../../Backend/VeroScriptBoost/Resources/Docs/User/en-US/index.md) · [Curl](../../../../../Backend/VeroScriptCurl/Resources/Docs/User/en-US/index.md)

<!-- docs-navigation -->

---

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
