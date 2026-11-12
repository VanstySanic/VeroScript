# JSON 配接

可選VeroScriptWebSocket COMPONENTS Json、target VeroScript::WebSocketJson、header VeroScriptWebSocketJson.h。TryMakeJsonMessage產生完整text、失敗保out；TryParseJsonMessage只接受完整text，binary報Type、fragment報State。先按bFirst/bFinal組合或用增量JSONreader，不單解析末片。失敗保JSON輸出/原訊息，數值精度/Unicode/重複/限額/work依JSON。

## 範例

```cmake
find_package(VeroScriptWebSocket 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::WebSocketJson)
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](json-adapters.md)
