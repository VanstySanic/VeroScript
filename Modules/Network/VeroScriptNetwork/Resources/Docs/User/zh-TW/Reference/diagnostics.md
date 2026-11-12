# 診斷與本地化

各模組 Resources/Localization 的六語言 network.json/http.json/websocket.json 擁有各自訊息。FError::Id 穩定且不配置，成功空、未知列舉 network.unknown；原生協定共用傳輸錯誤域。operation.message_id/error_code/error 為 ID/碼/原詳細，pending 也可空 ID，須另查完成/成功。串流、SSE、multipart、歷史、重連同規則。

參數0為原詳細，類別翻譯而後端詳細原文；不依翻譯/OS 文字分支。同步繫結用 network.binding/http.binding/websocket.binding 與 `FScriptError.MessageId/MessageArgs`，顯示配置失敗保留原始首錯。獨立 SDK 不依 Core/全域語言。

將各已安裝模組的 `share/VeroScript<Module>/Resources/Localization` 掛載到宿主資源解析器。Http 與 WebSocket 還需要其 Network 相依中的 `network.json`，不會重複附帶該檔案。組合宿主可分別掛載模組目錄，也可部署保留全部所需元件檔案的合併 Catalog 根目錄。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](diagnostics.md)
