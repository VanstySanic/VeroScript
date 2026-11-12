# 診斷與本地化

程式判斷使用 FError::Code/Id() 和 error.message_id()，不依原始顯示文字。Resources/Localization/<locale>/json.json 支援六語言。類別、路徑顯示與繫結錯誤有翻譯，後端/使用者轉換器詳細資訊保留原文。

FScriptError.MessageId/MessageArgs 可交宿主診斷服務。普通 JSON 錯誤有兩參數：原始詳細資訊及實例 JSON Pointer（根為空）；json.mapping.declaration 一個詳細資訊參數。語言/目錄缺失回退基準英文，不改類別/路徑。顯示中繼資料配置失敗保留原錯誤。

不擲例外的 `json::error` 依相同參數規則提供 `message_id`、`message` 和 `instance_pointer`。獨立 C++ SDK 不連結 Core，也不維護全域語言狀態。將已安裝的 `share/VeroScriptJson/Resources/Localization` 掛載到宿主資源解析器；與其他模組的 Catalog 合併時仍須保留 `json.json`。

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](diagnostics.md)
