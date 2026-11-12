# JSON アダプター

任意 VeroScriptWebSocket COMPONENTS Json、targetVeroScript::WebSocketJson、headerVeroScriptWebSocketJson.h。TryMakeJsonMessage は完全 text、失敗出力保持。TryParseJsonMessage は完全 text のみ、binaryType、fragmentState。bFirst/bFinal で全体結合か incrementalJSONreader を使い、最後の片だけ解析しません。失敗 out/message 保持、JSON 精度/Unicode/重複/制限/work 適用です。

## 例

```cmake
find_package(VeroScriptWebSocket 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::WebSocketJson)
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
