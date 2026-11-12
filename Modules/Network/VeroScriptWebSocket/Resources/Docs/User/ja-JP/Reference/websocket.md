# WebSocket メッセージと制御

`Send(FMessage)` は完全 message/bFinal=true。SendFragment は最初で text/binary 固定、最後 bFinal、途中の別 fullmessage は State でも Ping/close 可。待機 fragmentcancel は状態維持、activewritecancel は close。UTF8 を message 全体で検査し文字境界は分片可能、全長 MaxMessageBytes、失敗片は完成 state を消費しません。既定 Receive は完全、fragmentmode は bFirst/bFinal 付き有界 byte で frame/UTF8 境界保証なし、両方 true だけ完全です。最後の片だけ JSON にせず、snapshot 保持中 quota 保持。

Ping≤125byte、serialwrite、autoPong。TryTakeControl は非 blocking 空 out 保持、有界 queue を排出。overflow は Limitclose、既受信 message は読め、その後 Limit、既 cancelled は Cancelled。IdleTimeout60s/keepAlive true、nullopt 無効、operationtotal とは別です。

subprotocol は clientoffer/serverpreference 順、case-sensitive 非空 HTTPtoken、重複なし 128 件/16 KiB。server 最初の共通優先、require 既定 false、true は設定と合意必須。client は重複/複数/未提案選択拒否、結果不変。preread 保持、preference 時 64 KiB 予約、close で timer 停止。scriptoptionsfragment/compression/limits/timeout/idle/tls、send_*_fragment/ping/try_control、receiveis_text/is_first/is_final/data、controlhas_value/is_ping/payload（なしは空と false）、add/clear_subprotocols/require/subprotocol を使います。

## 既定値

| API | 値 |
| --- | --- |
| `FOptions::MaxMessageBytes` | `16 MiB` |
| `MaxQueuedBytes / MaxQueuedMessages` | `32 MiB / 128` |
| `Close(Code)` | `1000` |

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/websocket.md) · [fr-FR](../../fr-FR/Reference/websocket.md) · [ja-JP](websocket.md) · [ko-KR](../../ko-KR/Reference/websocket.md) · [zh-CN](../../zh-CN/Reference/websocket.md) · [zh-TW](../../zh-TW/Reference/websocket.md)
