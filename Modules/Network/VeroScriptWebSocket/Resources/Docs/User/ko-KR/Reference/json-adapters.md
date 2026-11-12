# JSON 어댑터

선택적으로 VeroScriptWebSocket COMPONENTS Json 을 요청하고 VeroScript::WebSocketJson 및 VeroScriptWebSocketJson.h 를 사용합니다. TryMakeJsonMessage 는 완전한 텍스트를 만들고 실패 시 출력을 유지합니다. TryParseJsonMessage 는 전체 텍스트만 허용하며 바이너리는 Type, 조각은 State 입니다. bFirst/bFinal 로 전체를 조립하거나 증분 JSON reader 를 사용하고 마지막 조각만 파싱하지 않습니다. 실패 시 JSON 출력과 원본 메시지를 보존하며 JSON 정밀도, Unicode, 중복 키, 한도 및 작업 검사를 유지합니다.

## 예제

```cmake
find_package(VeroScriptWebSocket 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::WebSocketJson)
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
