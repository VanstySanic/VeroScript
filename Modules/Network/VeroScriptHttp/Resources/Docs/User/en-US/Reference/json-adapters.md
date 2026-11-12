# JSON adapters

Optional C++ composition requires VeroScriptHttp COMPONENTS Json and VeroScript::HttpJson; include VeroScriptHttpJson.h. The base HTTP/Network/WebSocket SDKs do not require Json, and these helpers are not extra script methods.

TrySetJsonBody encodes completely before atomically replacing body/Content-Type; success removes old upload/factory/multipart/framing, failure preserves the request. TryParseJsonBody requires a complete buffered response, by default application/json or +json (case/parameters allowed), independent of HTTP status. FJsonBodyError.Http identifies body/media constraints, .Json the JSON failure. Preserve output/status/head/body on failure; an early stream head is not a buffered body. TryParseJsonEvent parses SSE data only, leaving type/ID in the event. JSON exactness, Unicode, duplicate policies, limits and work checks remain active.

## Examples

```cmake
find_package(VeroScriptHttp 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::HttpJson)
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
