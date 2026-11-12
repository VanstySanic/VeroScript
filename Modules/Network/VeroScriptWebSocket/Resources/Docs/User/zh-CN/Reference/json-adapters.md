# WebSocket JSON 适配

可选组合仅依赖安装的 Json 和 WebSocket SDK；基础 API 不强制引入 Json。

## WebSocket

```cmake
find_package(VeroScriptWebSocket 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::WebSocketJson)
```

包含 VeroScriptWebSocketJson.h。TryMakeJsonMessage 编码完整文本消息，失败不改变输出。
TryParseJsonMessage 只接受完整文本消息，二进制消息报 Type，字节分片报 State。
启用分片接收时先按 bFirst/bFinal 组成完整消息，或组合增量 JSON reader；不得只解析最后一块。
失败保留 JSON 输出和原始消息。数值精度、Unicode、重复键、限制和工作检查沿用 Json 公共合同。

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
