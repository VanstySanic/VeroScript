# WebSocket HTTP CONNECT 代理

原生 ws 与 wss 客户端通过显式 HTTP CONNECT 代理建立隧道。WebSocket 模块仍只依赖 Network，
不需要链接 Http 或 curl。代理协议目前接受 `http://host:port`，路径只能为空或 `/`，不能带查询或 fragment。
其他代理协议返回 InvalidArgument；不会读取或修改进程代理环境变量。

## C++

```cpp
WebSocket::FOptions options;
options.ProxyUrl = "http://127.0.0.1:8080";
options.ProxyAuthorization = "Basic ..."; // Full proxy Authorization value; empty if not required.
options.Operation.Timeout = std::chrono::seconds(10);
auto operation = WebSocket::FConnection::Connect(context, "wss://example.com/socket", options);
```

代理 URL 与认证字段各最多 4096 字节，认证值不能包含控制字符。
连接先解析代理地址、建立 TCP，再协商 CONNECT；wss 在隧道内执行目标主机的 TLS 握手，
最后进行 WebSocket 握手。SNI、CA 与目标身份检查继续使用目标主机，代理认证字段只发往代理。
所有阶段共享原 operation 的总期限与取消路径，握手时不会重新开始这项总期限。

CONNECT 的成功响应为 2xx；1xx 信息响应最多接受八次。
成功后直接进入隧道，不按 Content-Length 或 Transfer-Encoding 等响应字段读取正文，
包括两者同时存在的情况，遵循 [RFC 9110 §9.3.6](https://www.rfc-editor.org/rfc/rfc9110.html#section-9.3.6)。
响应头限制为 64 KiB，普通隧道的预读协议字节保持原样；非 2xx、截断、无效头或超限会失败并关闭连接。

代理连接为握手存储保守预留 320 KiB context 保留额度，随连接释放。
这项额度涵盖模块管理的握手缓冲；完整 TLS/操作系统/解析器分配与进程 RSS 继续适用网络模块的额度范围说明。
代理选项只适用于客户端，Listen 与 AcceptUpgrade 不接受它们。

## 脚本

```vs
websocket::options options=websocket::make_options();
options.set_proxy("http://127.0.0.1:8080", "");
options.set_timeout(10000);
websocket::operation opening=websocket::connect_with_options(context,"ws://example.com/socket",options);
```

`proxy_url()` 查询配置；`clear_proxy()` 同时清空代理地址与认证。
选项按值复制，提交后继续修改原选项不会改变正在进行的连接。
完整示例为 [proxy.vs](../../../../Examples/proxy.vs)，
参数是代理 URL、ws/wss URL、代理认证字段值，以及可选的 CA PEM 文本。
示例假定目标是 echo 服务；真实业务应用自行处理接收消息。

浏览器使用自身的 WebSocket 接口，不能据此推断支持原生代理选项。

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/websocket-proxy.md) · [fr-FR](../../fr-FR/Reference/websocket-proxy.md) · [ja-JP](../../ja-JP/Reference/websocket-proxy.md) · [ko-KR](../../ko-KR/Reference/websocket-proxy.md) · [zh-CN](websocket-proxy.md) · [zh-TW](../../zh-TW/Reference/websocket-proxy.md)
