# VeroScriptBoost

`vs-boost`

## 选择并安装后端

Boost 提供 DNS、TCP、UDP，以及所安装 SDK 包含的 HTTP 服务端、WebSocket 能力。将 `Boost::CreateNetworkService()` 传入 `Network::FContext`；通过 `Http::FServerOptions::Service` 传入 `CreateHttpServerService()`，通过 `WebSocket::FOptions::Service` 传入 `CreateWebSocketService()`。服务端及 WebSocket 服务需要兼容的上下文。

脚本可为已安装的 Network、HTTP 服务端或 WebSocket 服务显式指定 `boost`。选择与继承遵循 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/index.md#backend-selection)；`boost` 是提供者 ID，不是固定默认值。

## 使用原生 SDK

```cpp
#include <VeroScriptBoost.h>
#include <VeroScriptNetwork.h>

VeroScript::Network::FContext context(VeroScript::Boost::CreateNetworkService());
```

```cmake
find_package(VeroScriptBoost 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Boost)
```

安装所选包要求的精确依赖及原始许可。共享 `VeroScriptBoostRuntime` 归 Boost 所有；Windows 宿主须将匹配 DLL 放在可执行文件旁或原生模块部署位置。Linux/macOS 使用安装后的相对加载路径；iOS 在同一宿主镜像中静态注册。这些原生后端不提供浏览器通信实现。

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/zh-CN/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/zh-CN/index.md)

<a id="system-trust"></a>

客户端启用 `bUseSystemTrust` 时，Boost 除 OpenSSL 默认路径外，还将 Windows ROOT 存储或 macOS 系统信任锚加入 OpenSSL 上下文。macOS 路径导入锚证书，验证仍由 OpenSSL 传输层执行。可用 `TrustPem` 提供明确的附加根；仅使用所供根时关闭系统信任。信任库可用性与依赖部署须在实际目标系统检查。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
