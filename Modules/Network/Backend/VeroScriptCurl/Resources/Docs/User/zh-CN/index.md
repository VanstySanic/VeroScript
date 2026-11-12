# VeroScriptCurl

`vs-curl`

## 选择并安装后端

Curl 提供 HTTP 客户端、连接池、Cookie、重定向、重试及流式传输。将 `Curl::CreateHttpClientService()` 传入 `Http::FClient`，或设为 `Http::FSseClientOptions::Service`。上下文须提供不拥有套接字的监视与定时器，Boost 提供这样的执行基础；Curl 不会自动选择或安装它。HTTPS 使用 curl 所选 TLS 实现，独立于服务端 TLS。

脚本可为 HTTP 客户端或 SSE 请求显式指定 `curl`，并另选兼容的 Network 上下文。选择与继承遵循 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/index.md#backend-selection)；`curl` 是提供者 ID，不是固定默认值。

## 使用原生 SDK

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

安装所选 SDK 与执行基础要求的精确依赖及原始声明。使用上述 Boost 执行基础时，遵循其[部署指南](../../../../../VeroScriptBoost/Resources/Docs/User/zh-CN/index.md)。这些原生提供者不提供浏览器通信实现。

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/zh-CN/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/zh-CN/index.md)

<a id="system-trust"></a>

内置 macOS curl 构建启用 OpenSSL 与 Apple SecTrust；未显式设置 `CaFile` 时使用原生信任集成，指定 CA 文件则使用该请求提供的信任材料。内置 Windows curl 使用 Schannel，其他内置原生目标使用所配置的 OpenSSL 信任路径。系统 libcurl SDK 遵循该库自身的 TLS／信任配置。这些选择独立于 Boost 服务端／WSS TLS。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
