# Diagnostics and localization

Network, Http and WebSocket own network.json/http.json/websocket.json in Resources/Localization/<locale>, with six locales. FError::Id() is stable/allocation-free, empty on success and network.unknown for an unknown enum. Protocol transports share this error domain. operation.message_id/error_code/error expose ID/code/raw detail; pending work can also have an empty ID, so still check state/success. Stream, SSE, multipart, attempt/history and reconnect diagnostics follow the same rule.

Argument 0 is raw detail. Categories are translated, backend details remain original; never branch on translated/OS messages. Synchronous binding failures use network.binding/http.binding/websocket.binding via FScriptError.MessageId/MessageArgs. Existing errors and the first failure survive display-metadata allocation failure. Independent native SDKs have no Core/global language dependency.

Mount `share/VeroScript<Module>/Resources/Localization` from each installed module in the host resource resolver. Http and WebSocket also need their Network dependency’s `network.json`; they do not duplicate it. A combined host may mount these module directories separately or deploy a merged catalog root that preserves all required component files.

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
