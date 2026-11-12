# WebSocket 消息、分片与控制帧

FConnection 支持 ws/wss 双端、文本/二进制消息、可选 permessage-deflate、自动 Pong、
有界发送与接收队列。原生 I/O 持续读取，脚本无需调用 receive 才能处理 Ping 或关闭帧。

## 消息与分片

- Send(FMessage) 发送完整消息，bFinal 必须为 true。
- SendFragment(FMessage) 连续组成一条消息，最后一次设 bFinal = true。
  第一片决定文本/二进制类型，后续片必须一致。未结束消息期间，完整业务消息会返回 State；
  Ping 和关闭仍可以处理。排队片段被取消不会改变消息状态，活动写入取消会关闭连接。
- 文本 UTF-8 在整条消息上验证，可以在两个分片之间拆开一个字符。所有分片的总长度仍受
  MaxMessageBytes 限制。失败的片段不会消耗已经完成的分片状态。
- 默认 Receive 返回完整消息。FOptions::bReceiveFragments 返回有界字节块，
  FMessage::bFirst / bFinal 标记消息开始和结束。字节块边界不承诺对应网络帧或 UTF-8 字符边界。
  单个块只有同时满足这两个标志时才是完整消息；不能把最后一块单独当作完整 JSON 正文。

接收的 FMessage 结果使用不可变共享快照。终态 operation 或其结果仍被持有时，
载荷的内存额度继续保留。

## Ping、Pong、空闲和压缩

Ping(payload) 最多接受 125 字节；与业务写入共用串行队列。自动 Pong 由后端处理。
TryTakeControl 非阻塞取出观察到的 Ping/Pong，未收到时返回 false 且保持输出。
控制队列有数量上限；调用方应及时读取，溢出会关闭连接并保留 `Limit` 原因。
尚未取得消息的待处理接收，以及排空已入队消息后的后续接收，报告 `Limit`。
已经进入取消终态的接收保持 `Cancelled`。

IdleTimeout 默认 60 秒，bKeepAlivePings 默认 true。关闭空闲期限需传 nullopt；
启用时由后端在空闲期间发送保活 Ping，并在期限内未收到数据时关闭连接。
它不替代单次操作的总期限。

bCompression 默认 false；打开后参与 permessage-deflate 协商。发送消息与接收消息的大小限制
均按应用载荷核算，接收采用解压后的大小。未协商成功时继续普通消息模式。
参数、实际协商结果、构建开关与额度见 [WebSocket 压缩](websocket-compression.md)。

## 脚本入口

    websocket::options options = websocket::make_options();
    options.set_receive_fragments(true);
    options.set_compression(true);
    options.set_limits(1048576, 2097152, 128);
    options.set_timeout(10000);
    options.set_idle_timeout(60000, true);
    websocket::operation task = websocket::connect_with_options(context, "ws://127.0.0.1:8080/", options);

同一选项类型支持 set_tls(Network::tls_options) 与 listen_with_options。选项按值复制，
后续修改不改变已经提交的连接。现有 connect、listen 与 TLS 便利入口继续可用。

连接提供 send_text_fragment(data, final, timeout_ms)、send_binary_fragment、
ping(data, timeout_ms) 和 try_control()。receive 返回的 operation 可以查询 is_text、is_first、is_final 和 data。
try_control 的结果提供 has_value、is_ping、payload；无事件时 payload 为空。

## 子协议

FOptions::Subprotocols 是客户端提供顺序或服务端偏好顺序。协议名区分大小写，必须是非空 HTTP token，
不允许重复；最多 128 项，序列化后的协议列表最多 16 KiB。服务端选择自己偏好顺序中的第一个交集。
bRequireSubprotocol 默认为 false，允许没有交集时不选择协议；设为 true 则必须配置协议并协商成功。
客户端拒绝重复选择字段、多个选择值，以及任何未曾提供的选择。Subprotocol() 返回只读协商结果。

脚本选项提供 add_subprotocol(name)、clear_subprotocols()、set_require_subprotocol(bool)，
连接通过 subprotocol() 查询结果。选项复制后修改不影响已经提交的连接。
服务端保留握手后预读的全部帧字节；有协议偏好时，为握手缓冲保守预留 64 KiB context 额度。
关闭连接同时取消协议计时器，保留已关闭句柄不会让 context 等待空闲期限后才退出。

[HTTP 升级交接](../../../../../../VeroScriptHttp/Resources/Docs/User/zh-CN/Reference/upgrade.md) 与 [CONNECT 代理](websocket-proxy.md) 已提供独立接口；[有界重连](websocket-reconnect.md) 提供显式的新一轮连接策略。

## 默认值

| API | 值 |
| --- | --- |
| `FOptions::MaxMessageBytes` | `16 MiB` |
| `MaxQueuedBytes / MaxQueuedMessages` | `32 MiB / 128` |
| `Close(Code)` | `1000` |

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/websocket.md) · [fr-FR](../../fr-FR/Reference/websocket.md) · [ja-JP](../../ja-JP/Reference/websocket.md) · [ko-KR](../../ko-KR/Reference/websocket.md) · [zh-CN](websocket.md) · [zh-TW](../../zh-TW/Reference/websocket.md)
