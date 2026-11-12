# VeroScriptBoost

`vs-boost`

## Select and install a backend

Boost provides DNS, TCP and UDP, plus HTTP-server and WebSocket capabilities included in the installed SDK. Pass `Boost::CreateNetworkService()` to `Network::FContext`. Pass `CreateHttpServerService()` through `Http::FServerOptions::Service`, and `CreateWebSocketService()` through `WebSocket::FOptions::Service`. Server/WebSocket services require a compatible context.

In scripts, select `boost` explicitly for its installed Network, HTTP-server or WebSocket service. Selection and inheritance follow [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/en-US/index.md#backend-selection); `boost` is a provider ID, not a fixed default.

## Native SDK use

```cpp
#include <VeroScriptBoost.h>
#include <VeroScriptNetwork.h>

VeroScript::Network::FContext context(VeroScript::Boost::CreateNetworkService());
```

```cmake
find_package(VeroScriptBoost 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Boost)
```

Install every exact dependency required by the selected packages, including their original notices. Boost owns the shared `VeroScriptBoostRuntime`; Windows hosts need its matching DLL beside the executable or in the native-module layout. Use the installed relative loader paths on Linux/macOS. iOS uses static registration in one host image. These native backends do not supply a browser transport.

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/en-US/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/en-US/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/en-US/index.md)

<a id="system-trust"></a>

With `bUseSystemTrust` enabled for a client, Boost adds system trust anchors to its OpenSSL context: the Windows ROOT store or macOS system anchors, alongside OpenSSL default paths. macOS imports anchor certificates; verification still follows the OpenSSL transport. Supply `TrustPem` for explicit additional roots, or disable system trust when only your supplied roots should be used. Trust-store availability and target deployment must be checked on the actual system.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
