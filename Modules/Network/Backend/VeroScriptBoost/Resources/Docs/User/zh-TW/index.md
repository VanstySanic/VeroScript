# VeroScriptBoost

`vs-boost`

## 選擇並安裝後端

Boost 提供 DNS、TCP、UDP，以及所安裝 SDK 包含的 HTTP 伺服器與 WebSocket 功能。將 `Boost::CreateNetworkService()` 傳入 `Network::FContext`，透過 `Http::FServerOptions::Service` 傳入 `CreateHttpServerService()`，透過 `WebSocket::FOptions::Service` 傳入 `CreateWebSocketService()`。伺服器與 WebSocket 服務需要相容的情境。

指令碼可為已安裝的 Network、HTTP 伺服器或 WebSocket 服務明確指定 `boost`。選擇與繼承遵循 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-TW/index.md#backend-selection)；`boost` 是提供者 ID，不是固定預設值。

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

安裝所選套件要求的精確相依項及原始授權。共用 `VeroScriptBoostRuntime` 歸 Boost 所有；Windows 主程式須將相容 DLL 放在執行檔旁或原生模組部署位置。Linux/macOS 使用安裝後的相對載入路徑；iOS 在同一主程式映像中靜態註冊。這些原生後端不提供瀏覽器通訊實作。

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-TW/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/zh-TW/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/zh-TW/index.md)

<a id="system-trust"></a>

用戶端啟用 `bUseSystemTrust` 時，Boost 除 OpenSSL 預設路徑外，亦將 Windows ROOT 存放區或 macOS 系統信任錨加入 OpenSSL 上下文。macOS 路徑匯入錨憑證，驗證仍由 OpenSSL 傳輸層執行。可用 `TrustPem` 提供明確的附加根；僅使用所供根時關閉系統信任。信任庫可用性與相依部署須在實際目標系統檢查。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
