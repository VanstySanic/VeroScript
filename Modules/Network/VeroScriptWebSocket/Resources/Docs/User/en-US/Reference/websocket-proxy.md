# WebSocket CONNECT proxy

Native ws/wss clients support explicit HTTP CONNECT without Http/curl. Only http://host:port with empty or / path, no query/fragment is accepted; unsupported schemes are InvalidArgument. Process proxy environment is neither read nor changed. Proxy URL and authorization are each ≤4096 bytes, authorization has no controls and is the complete field value.

Resolve proxy, TCP-connect, CONNECT, then target TLS for wss and WebSocket handshake, sharing one original deadline/cancel path. Target hostname drives SNI/CA/identity; proxy authorization goes only to proxy. CONNECT accepts 2xx and at most eight 1xx heads. On success enter the tunnel regardless of Content-Length/Transfer-Encoding, preserving preread protocol bytes. Headers ≤64 KiB; non-2xx/truncation/invalid/excess closes the connection. Reserve 320 KiB context quota for handshake until actual connection release, excluding full TLS/OS memory. Proxy options are client-only; Listen/AcceptUpgrade reject them.

Script options.set_proxy(url,authorization), proxy_url and clear_proxy (clears both) are copied at submission. Browser-native proxy behavior is not this native option. The proxy.vs example requires an echo peer and accepts proxy URL, ws/wss URL, authorization and optional CA PEM; ordinary applications must handle their own message protocol.

## Examples

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

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](websocket-proxy.md) · [fr-FR](../../fr-FR/Reference/websocket-proxy.md) · [ja-JP](../../ja-JP/Reference/websocket-proxy.md) · [ko-KR](../../ko-KR/Reference/websocket-proxy.md) · [zh-CN](../../zh-CN/Reference/websocket-proxy.md) · [zh-TW](../../zh-TW/Reference/websocket-proxy.md)
