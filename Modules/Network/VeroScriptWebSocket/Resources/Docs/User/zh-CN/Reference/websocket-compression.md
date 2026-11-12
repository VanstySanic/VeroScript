# WebSocket 压缩

原生 provider 支持 RFC 7692 的 permessage-deflate，默认不启用连接压缩。
`CompressionSupported(service)` 查询构建能力，`FOptions::bCompression` 表示参与协商，
`FConnection::CompressionInfo()` 返回握手实际选择的不可变值；三者含义不同。
对端可以拒绝压缩，连接继续使用普通消息。快照在连接和 context 关闭后仍可读取。

## 配置

`FOptions::Compression` 为 `FCompressionOptions`：

| 字段 | 默认值 | 范围或含义 |
| --- | --- | --- |
| `ClientMaxWindowBits` | 15 | 原生配置 9–15，client→server 方向的窗口提议/选择 |
| `ServerMaxWindowBits` | 15 | 原生配置 9–15，server→client 方向的窗口约束 |
| `bClientNoContextTakeover` | false | 提议或选择客户端每条消息重建压缩上下文 |
| `bServerNoContextTakeover` | false | 要求或选择服务端每条消息重建压缩上下文 |
| `Level` | 8 | 本端压缩等级，0–9 |
| `MemoryLevel` | 4 | 本端压缩内存等级，1–9 |

client/server 前缀遵循协议方向，不是 local/remote。客户端的 client 参数可能只是提议，
不能把配置副本当作最终结果。实际窗口和上下文接管标志以 `FCompressionInfo` 为准；
未协商时 `bEnabled=false`，窗口值为零。压缩协商不改变文本 UTF-8、二进制、空消息、分片和控制帧语义。

原生压缩器不能生成 8 位窗口。服务端拒绝要求自身使用 8 位窗口的提议，
可继续选择后续可支持的提议；客户端拒绝要求自身压缩器使用 8 位窗口的结果。
不把 8 静默替换成 9。传入原生配置的非法窗口/等级返回 InvalidArgument。

握手逐项校验扩展名、完整参数语法、重复项、取值、提议约束和本端能力，
在创建解压/压缩状态前拒绝不成立的选择。未知、重复、缺值和不支持的协商结果为 Protocol。
服务端从字段到达顺序中选择首个可支持提议；支持逗号列表、多个请求扩展字段和带引号的数字。

## 大小与资源

`MaxMessageBytes`、消息队列和发送额度按应用载荷核算，接收使用解压后的字节数。
重复文本压缩得很小也不能绕过接收上限。超限返回 Limit；无效 DEFLATE 或帧协议返回 Protocol；
正常关闭返回 Closed，底层读取失败返回 Read，空闲期限返回 TimedOut。
连接保留第一个终止原因，队列中已经收齐的消息仍可依次读取；随后 receive 返回该原因。

每条参与协商的连接在分配编解码状态前，保守预留 1 MiB 的 context 保留字节额度，
覆盖锁定后端两方向的编解码状态在全部支持窗口/内存等级下的存储。协商未启用时立即释放该预留；
启用后预留随实际连接状态保留，不能因逻辑关闭后仍持有连接对象就提前当作已释放。
此预留不等价于后端所有管理对象的精确物理峰值。

## 构建与脚本

`VEROSCRIPT_WEBSOCKET_COMPRESSION=OFF` 从 native stream 模板中关闭编解码能力，
`CompressionSupported(service)` 返回 false，开启压缩的操作返回 Unsupported，普通 ws/wss 仍可使用。
发行依赖清单 `veroscript-module.json` 的 `backendServices` 记录已构建的服务；压缩能力通过 `compression_supported()` 查询，
构建选项与私有补丁参与模块实现身份。

```cpp
websocket::compression_options compression = websocket::make_compression_options();
compression.set_server_window_bits(12);
compression.set_client_no_context_takeover(true);
websocket::options options = websocket::make_options();
options.set_compression(true);
options.set_compression_options(compression);
// Query after a successful connection.
websocket::compression_info actual = connection.compression();
```

compression_options 提供 client_window_bits、server_window_bits、level、memory_level、
client_no_context_takeover、server_no_context_takeover 的 getter 和 `set_` setter。
options 保存参数副本，可通过 `compression_options()` 读回。`websocket::compression_supported()`
查询能力；compression_info 提供 enabled、两个 window_bits 和两个 no_context_takeover。

完整本地示例：[compression.vs](../../../../Examples/compression.vs)。

协议与后端说明：[RFC 7692](https://www.rfc-editor.org/rfc/rfc7692.html)、
[Beast 参数](https://www.boost.org/doc/libs/latest/libs/beast/doc/html/beast/ref/boost__beast__websocket__permessage_deflate.html)。

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/websocket-compression.md) · [fr-FR](../../fr-FR/Reference/websocket-compression.md) · [ja-JP](../../ja-JP/Reference/websocket-compression.md) · [ko-KR](../../ko-KR/Reference/websocket-compression.md) · [zh-CN](websocket-compression.md) · [zh-TW](../../zh-TW/Reference/websocket-compression.md)
