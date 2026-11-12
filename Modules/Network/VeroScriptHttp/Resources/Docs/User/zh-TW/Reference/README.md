# Http — 網路參考

前端安裝與後端選擇的公共規則統一見 [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/zh-TW/index.md#backend-selection)。

vs-http 0.1.1 依賴匹配 vs-network，不強制 WS/JSON。ID/命名空間 http，目標 VeroScript::Http，Bindings 為匹配 Core 的 VeroScript::HttpBindings。先載 network 再 http 並宣告相依，公開頭不暴露 curl/Boost。提供緩衝/串流客戶端服務端、路由、multipart、SSE、檔案，使用 create_client/create_request/listen/create_response 與型別結果。NUL 保留、選項提交時快照，期限/派發/生命週期/額度依 Network；4xx/5xx 不等於傳輸失敗。正文 setter 清理衝突模式，form 明確 +/空白轉換，multipart 保留重複/二進位。緩衝回應用 response_builder/reply_response，add_header/set_header 為新增/替換。SSE reader/encoder 不自動重連，C++ COMPONENTS Json 可選。

- [HTTP 用戶端策略](client-policy.md)
- [完成回呼與程式生命週期](completion.md)
- [診斷與本地化](diagnostics.md)
- [檔案與原子輸出](files.md)
- [HTTP 回應鏈與統計](http-trace.md)
- [JSON 配接](json-adapters.md)
- [multipart 上傳與解析](multipart.md)
- [HTTP 路由](routing.md)
- [SSE 用戶端](sse-client.md)
- [有界位元組串流](streaming.md)
- [TLS 設定](tls.md)
- [HTTP 升級為 WebSocket](upgrade.md)

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](README.md)
