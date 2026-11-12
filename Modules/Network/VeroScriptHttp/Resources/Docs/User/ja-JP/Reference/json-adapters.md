# JSON アダプター

VeroScriptHttp COMPONENTS Json、VeroScript::HttpJson、VeroScriptHttpJson.h を使う任意 C++API で、base へ JSON 依存や scriptmethod を追加しません。TrySetJsonBody は完全 encode 後 body/Content-Type を原子的置換、成功で旧 upload/factory/multipart/framing 削除、失敗 request 保持。TryParseJsonBody は完全 bufferresponse と既定 application/json/+json（case/parameter 可）、status とは独立。FJsonBodyError.Http は条件、.Json は JSONerror、失敗 out/status/head/body 保持、早期 head だけでは解析不可。TryParseJsonEvent は SSEdata のみ、type/ID 保持。数精度/Unicode/重複/予算は JSON 契約です。

## 例

```cmake
find_package(VeroScriptHttp 0.1.1 EXACT REQUIRED COMPONENTS Json)
target_link_libraries(my_app PRIVATE VeroScript::HttpJson)
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/json-adapters.md) · [fr-FR](../../fr-FR/Reference/json-adapters.md) · [ja-JP](json-adapters.md) · [ko-KR](../../ko-KR/Reference/json-adapters.md) · [zh-CN](../../zh-CN/Reference/json-adapters.md) · [zh-TW](../../zh-TW/Reference/json-adapters.md)
