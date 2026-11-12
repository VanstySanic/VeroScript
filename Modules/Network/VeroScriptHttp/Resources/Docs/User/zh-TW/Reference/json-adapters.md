# JSON 配接

可選C++用VeroScriptHttp COMPONENTS Json、VeroScript::HttpJson、VeroScriptHttpJson.h，不強加JSON相依或另增script方法。TrySetJsonBody先完整encode再原子取代正文/Content-Type，成功清舊upload/factory/multipart/framing，失敗保request。TryParseJsonBody須完整bufferresponse、預設application/json/+json（大小寫/參數可），與HTTPstatus獨立；FJsonBodyError.Http為條件、.Json為JSON錯，失敗保out/status/head/body，早期head不足。TryParseJsonEvent只解析SSEdata、type/ID不變；精度/Unicode/重複鍵/限額/work沿JSON。

## 範例

```cmake
find_package(VeroScriptHttp 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::HttpJson)
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](../../ja-JP/Reference/json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](json-adapters.md)
