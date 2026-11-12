# Adaptateurs JSON

VeroScriptWebSocket COMPONENTS Json, cible VeroScript::WebSocketJson, en-tête VeroScriptWebSocketJson.h sont facultatifs. TryMakeJsonMessage produit un texte complet, sortie conservée si échec. TryParseJsonMessage exige texte complet : binaire Type, fragment State. Assemblez bFirst/bFinal ou utilisez un reader JSON incrémental, jamais le dernier bloc seul. Sortie/message conservés à l’échec ; précision, Unicode, doublons, limites et travail JSON restent actifs.

## Exemples

```cmake
find_package(VeroScriptWebSocket 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::WebSocketJson)
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
