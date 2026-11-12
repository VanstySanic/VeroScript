# WebSocket CONNECT 代理

原生ws/wss明確HTTPCONNECT，不需Http/curl。僅http://host:port及空路徑或/，不能query/fragment，其他schemeInvalidArgument，不讀/改環境proxy。URL/auth各最多4096byte，auth為無控制字元的完整欄位；proxyDNS/TCP/CONNECT/目標TLS/WS共原期限/取消，SNI/CA/身分驗目標，auth只送proxy。

2xx成功、最多8次1xx；成功直接tunnel不按Content-Length/Transfer-Encoding讀正文，保預讀，頭64 KiB，非2xx/截斷/無效/超限皆關閉。預留320 KiB至實際連線釋放，不是完整TLS/OS上限。僅client，Listen/AcceptUpgrade拒絕；腳本set_proxy/proxy_url/clear_proxy清兩值、提交快照。browserproxy行為獨立，proxy.vs須echo及proxy/target/auth/可選CA，業務protocol由應用處理。

## 範例

```cpp
WebSocket::FOptions options;
options.ProxyUrl = "http://127.0.0.1:8080";
options.ProxyAuthorization = "Basic ..."; // Full proxy Authorization value; empty if not required.
options.Operation.Timeout = std::chrono::seconds(10);
auto operation = WebSocket::FConnection::Connect(context, "wss://example.com/socket", options);
```

```VeroScript
websocket::options options=websocket::make_options();
options.set_proxy("http://127.0.0.1:8080", "");
options.set_timeout(10000);
websocket::operation opening=websocket::connect_with_options(context,"ws://example.com/socket",options);
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/websocket-proxy.md) · [fr-FR](../../fr-FR/Reference/websocket-proxy.md) · [ja-JP](../../ja-JP/Reference/websocket-proxy.md) · [ko-KR](../../ko-KR/Reference/websocket-proxy.md) · [zh-CN](../../zh-CN/Reference/websocket-proxy.md) · [zh-TW](websocket-proxy.md)
