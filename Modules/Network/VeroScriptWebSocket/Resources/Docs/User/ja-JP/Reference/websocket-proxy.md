# WebSocket CONNECT プロキシ

nativews/wss の明示 HTTPCONNECT、Http/curl 不要。http://host:port と空または/path のみ、query/fragment 不可、他 schemeInvalidArgument、環境 proxy 読取変更なし。URL/auth 各 4096byte 以下、auth は control なし完全 field 値。proxyDNS/TCP/CONNECT/targetTLS/WS が同期限/cancel、SNI/CA/identity は target、auth は proxy だけです。

2xx 成功、1xx 最大 8、成功後 Content-Length/Transfer-Encoding に依存せず tunnel へ、preread 保持。header64 KiB、非 2xx/切断/invalid/limit は close。320 KiB 予約を actualconnection 解放まで保持、全 TLS/OScap ではない。client 限定で Listen/AcceptUpgrade 拒否。scriptset_proxy/proxy_url/clear_proxy（両値消去）、optionscopy。browserproxy 機能は別、proxy.vs は echo と proxy/target/auth/optionalCA 引数、アプリの protocol は caller が処理します。

## 例

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

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/websocket-proxy.md) · [fr-FR](../../fr-FR/Reference/websocket-proxy.md) · [ja-JP](websocket-proxy.md) · [ko-KR](../../ko-KR/Reference/websocket-proxy.md) · [zh-CN](../../zh-CN/Reference/websocket-proxy.md) · [zh-TW](../../zh-TW/Reference/websocket-proxy.md)
