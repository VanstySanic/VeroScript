# VeroScriptCurl

`vs-curl`

## Select and install a backend

Curl provides HTTP clients, connection pooling, cookies, redirects, retries and streaming. Pass `Curl::CreateHttpClientService()` to `Http::FClient`, or to `Http::FSseClientOptions::Service`. A context must supply compatible non-owning socket-watch and timer services; Boost supplies that executor. Curl does not select or install it automatically. HTTPS uses the selected curl TLS implementation independently of server TLS.

In scripts, select `curl` explicitly for HTTP-client or SSE requests and select a compatible Network context separately. Selection and inheritance follow [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/en-US/index.md#backend-selection); `curl` is a provider ID, not a fixed default.

## Native SDK use

```cpp
#include <VeroScriptBoost.h>
#include <VeroScriptCurl.h>
#include <VeroScriptHttp.h>

VeroScript::Network::FContext context(VeroScript::Boost::CreateNetworkService());
VeroScript::Http::FClient client(context, VeroScript::Curl::CreateHttpClientService());
```

```cmake
find_package(VeroScriptBoost 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptCurl 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Boost VeroScript::Curl)
```

Install the exact dependencies and original notices required by your selected SDK and executor. When using the Boost executor shown above, follow its [deployment guide](../../../../../VeroScriptBoost/Resources/Docs/User/en-US/index.md). These native providers do not supply a browser transport.

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/en-US/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/en-US/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/en-US/index.md)

<a id="system-trust"></a>

The bundled macOS curl build enables OpenSSL with Apple SecTrust. With no explicit `CaFile`, it uses the native trust integration; an explicit CA file selects that request’s supplied trust material. Windows bundled curl uses Schannel, while other bundled native targets use their configured OpenSSL trust paths. A system-libcurl SDK follows that library’s TLS/trust configuration. These choices are independent of Boost server/WSS TLS.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
