# VeroScriptWebSocket

前端安装与后端选择的公共规则统一见 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/index.md#backend-selection)。

正式包 `vs-websocket`，原生入口 ID 和脚本命名空间为 `websocket`；CMake 包为 `VeroScriptWebSocket`，target 为 `VeroScript::WebSocket`。精确依赖同版本 `vs-network`，共享原生运行时只由 Boost 后端包分发。

`Connect` 和服务端 `Accept` 的结果为 `shared_ptr<FConnection>`。
`Send` 成功表示本地传输写入完成，不表示对方业务确认；`Receive` 默认返回完整 `FMessage`。
可选字节分片、串行分片发送、Ping/Pong、压缩和空闲保活见 [WebSocket 专题](websocket.md)。
接收取消不会消费下一条消息；排队发送取消可以移除，已开始发送的取消会关闭连接。
接收队列满时结束连接；关闭信息区分干净关闭和异常断开。
原生 WSS 使用共享运行时的 TLS 传输；未启用 TLS 的构建明确返回 Unsupported。
HTTP 升级转交见 [协议升级](../../../../../../VeroScriptHttp/Resources/Docs/User/zh-CN/Reference/upgrade.md)；浏览器 provider 仍未实现。

## 脚本入口

| 入口 | 主要方法 |
| --- | --- |
| `websocket::connect(context, url, timeout)` | 完成后 `connection()` 取连接 |
| `websocket::listen(context, address, port)` | `accept(timeout)/port/close` |
| `websocket::connection` | `send_text/send_binary/receive/close/closed` |

## SDK 消费

安装后导入所用协议的 CMake 包，配置会自动查找匹配的基础包。context 驱动和操作结果约束见 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-CN/Reference/README.md)。脚本宿主先注册 `network`，再注册本协议并声明依赖。

## 专题

- [json-adapters](json-adapters.md)
- [websocket-compression](websocket-compression.md)
- [websocket-proxy](websocket-proxy.md)
- [websocket-reconnect](websocket-reconnect.md)
- [websocket](websocket.md)

## 注册与结果

`VeroScriptWebSocketModule.h` 提供本模块的 `QueryModule` / `GetImplementationIdentity`，静态库为
`VeroScriptWebSocketBindings`，动态库为 `VeroScriptWebSocketModule`。先注册 network，并声明依赖可见性。
接收完成后用 `data/is_text` 读取消息，数据使用保留 NUL 的 string；任务未完成、失败或结果类型不符时
访问会失败。Core、程序代次和派发约束见基础模块的注册与完成回调文档。

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](README.md) · [zh-TW](../../zh-TW/Reference/README.md)
