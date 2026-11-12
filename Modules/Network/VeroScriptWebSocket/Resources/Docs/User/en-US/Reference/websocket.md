# WebSocket messages and control frames

Send(FMessage) is a full message and requires bFinal=true. SendFragment starts/continues one message, first fragment fixes text/binary and the final marks completion. While fragmented, another complete business message fails State; Ping/close remain possible. Cancelling a queued fragment does not alter message state; cancelling an active write closes the connection. UTF-8 validation spans the whole text message and may cross fragment boundaries; all fragments share MaxMessageBytes. A rejected fragment does not consume completed fragment state.

Receive returns whole messages by default. bReceiveFragments returns bounded byte chunks with bFirst/bFinal, not guaranteed frame/UTF-8 boundaries. A chunk is a complete message only if BOTH flags are true; do not parse its final chunk alone as JSON. Immutable result snapshots retain payload quota while operation/results live.

Ping allows ≤125 bytes through the serial write queue; the backend handles automatic Pong. TryTakeControl is nonblocking, preserves output if no event, and the bounded queue must be drained. Overflow closes with Limit; queued complete messages remain readable, later receives report Limit, while already cancelled receives remain Cancelled. IdleTimeout defaults 60 s, bKeepAlivePings true; nullopt disables idle timeout. Keepalive does not replace per-operation total deadlines.

Subprotocols are ordered client offers/server preferences, case-sensitive nonempty HTTP tokens without duplicates, ≤128 entries and ≤16 KiB serialized. Server chooses its first preferred intersection; bRequireSubprotocol defaults false, true requires a configured negotiated protocol. Clients reject duplicate/multiple/unoffered selections. Subprotocol() is immutable. Server preread bytes survive handshake, with conservative 64 KiB handshake reservation when protocol preferences are used. Close cancels timers even if handles remain retained.

Script make_options supports set_receive_fragments/set_compression/set_limits/set_timeout/set_idle_timeout/set_tls and copied configuration. send_text_fragment/send_binary_fragment/ping/try_control expose data; receive operation has is_text/is_first/is_final/data, control has has_value/is_ping/payload. add_subprotocol/clear_subprotocols/set_require_subprotocol and connection.subprotocol expose negotiation. No control event means empty payload with has_value=false.

## Defaults

| API | Value |
| --- | --- |
| `FOptions::MaxMessageBytes` | `16 MiB` |
| `MaxQueuedBytes / MaxQueuedMessages` | `32 MiB / 128` |
| `Close(Code)` | `1000` |

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](websocket.md) · [fr-FR](../../fr-FR/Reference/websocket.md) · [ja-JP](../../ja-JP/Reference/websocket.md) · [ko-KR](../../ko-KR/Reference/websocket.md) · [zh-CN](../../zh-CN/Reference/websocket.md) · [zh-TW](../../zh-TW/Reference/websocket.md)
