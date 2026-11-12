# VeroScriptCurl

`vs-curl`

## 選擇並安裝後端

Curl 提供 HTTP 用戶端、連線池、Cookie、重新導向、重試及串流。將 `Curl::CreateHttpClientService()` 傳入 `Http::FClient`，或設為 `Http::FSseClientOptions::Service`。情境須提供不擁有通訊端的監視與計時器，Boost 提供這類執行基礎；Curl 不會自動選擇或安裝它。HTTPS 使用 curl 選定的 TLS 實作，獨立於伺服器 TLS。

指令碼可為 HTTP 用戶端或 SSE 請求明確指定 `curl`，並另選相容的 Network 上下文。選擇與繼承遵循 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-TW/index.md#backend-selection)；`curl` 是提供者 ID，不是固定預設值。

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

安裝所選 SDK 與執行基礎要求的精確相依項目及原始聲明。使用上述 Boost 執行基礎時，遵循其[部署指南](../../../../../VeroScriptBoost/Resources/Docs/User/zh-TW/index.md)。這些原生提供者不提供瀏覽器通訊實作。

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-TW/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/zh-TW/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/zh-TW/index.md)

<a id="system-trust"></a>

內建 macOS curl 建置啟用 OpenSSL 與 Apple SecTrust；未明確設定 `CaFile` 時使用原生信任整合，指定 CA 檔案則使用該請求提供的信任資料。內建 Windows curl 使用 Schannel，其他內建原生目標使用所設定的 OpenSSL 信任路徑。系統 libcurl SDK 遵循該函式庫自身的 TLS／信任設定。這些選擇獨立於 Boost 伺服端／WSS TLS。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
