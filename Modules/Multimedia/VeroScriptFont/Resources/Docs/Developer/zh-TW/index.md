# 維護 VeroScriptFont

## 原始碼職責與驗證

本模組維護字型、字形塑形與文字版面配置契約。公開介面宣告位於 [`Public/VeroScriptFontLayout.h`](../../../../Public/VeroScriptFontLayout.h)，模組專屬輸入位於 [`Resources/Tests`](../../../Tests)。

公共提供者邊界、重建要求、驗證與法律資源處理統一維護在[多媒體維護](../../../../../../../Resources/Docs/Developer/zh-TW/multimedia.md)。


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/zh-TW/index.md)
- [Standards](../../../../../../../Resources/Docs/Standards/zh-TW/GENERAL.md)

<a id="font-layout-maintenance"></a>

不可變文字布局持有文字與所選服務；同步的行 run 快取最多保存四份不可變分析，樣式片段只選取相交的邏輯 run。僅當已驗證布局擁有完全相同的緩衝區和長度時才略過 UTF-8 檢查。後備塑形須保留叢集範圍檢查和字形順序。`VeroScriptFontContractTests`、`VeroScriptFontLayoutContractTests` 使用不依賴具體提供者的 mock；語料與指令碼測試歸 FontBackend。繫結明確選擇 VM，不依賴 Core 私有測試標頭。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
