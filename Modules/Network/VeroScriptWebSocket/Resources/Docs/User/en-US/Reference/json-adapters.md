# JSON adapters

Install VeroScriptWebSocket COMPONENTS Json and link VeroScript::WebSocketJson, including VeroScriptWebSocketJson.h. Base WebSocket remains independent of Json.

TryMakeJsonMessage produces a complete text message and preserves output on failure. TryParseJsonMessage accepts only complete text: binary is Type, byte fragments State. In fragment mode assemble bFirst/bFinal or use an incremental JSON reader; never parse just the final fragment. Failure preserves JSON output and source message. Exact numbers, Unicode, duplicate-key policy, limits and work checks follow Json.

## Examples

```cmake
find_package(VeroScriptWebSocket 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::WebSocketJson)
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
