# WebSocket 訊息與控制框架

`Send(FMessage)`須完整message/bFinal=true，SendFragment首片決定text/binary、末片bFinal；未完時另一完整業務message報State，Ping/close仍可。取消排隊片不改狀態，活動write取消關連線。UTF8跨全訊息驗證，可跨片拆字，總MaxMessageBytes，拒絕片不消耗已完成狀態。預設Receive完整，fragment模式回bFirst/bFinal有界byte，不保frame/UTF8邊界，兩者true才完整；不能只把末片當JSON。結果不變且保有時持續計額度。

Ping最多125byte走序列write、autoPong。TryTakeControl無阻塞、無事件保out，須排空控制佇列；超限關閉Limit，已完訊息可先讀，後續Limit，既取消仍Cancelled。IdleTimeout預設60秒、keepAlive=true，nullopt停用，與operationtotal不同。

subprotocol按client提供/server偏好順序，大小寫敏感非空HTTPtoken、不重複、128項/16 KiB；server選首交集，require預設false、true須配置及成功協商。client拒絕重複/多選/未提供值，結果不變，保預讀、有偏好時預留64 KiB，close停timer。腳本options、send_*_fragment/ping/try_control，receive的is_text/is_first/is_final/data，control的has_value/is_ping/payload（無事件空值/false），add/clear_subprotocols/require/subprotocol提供操作。

## 預設值

| API | 值 |
| --- | --- |
| `FOptions::MaxMessageBytes` | `16 MiB` |
| `MaxQueuedBytes / MaxQueuedMessages` | `32 MiB / 128` |
| `Close(Code)` | `1000` |

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/websocket.md) · [fr-FR](../../fr-FR/Reference/websocket.md) · [ja-JP](../../ja-JP/Reference/websocket.md) · [ko-KR](../../ko-KR/Reference/websocket.md) · [zh-CN](../../zh-CN/Reference/websocket.md) · [zh-TW](websocket.md)
