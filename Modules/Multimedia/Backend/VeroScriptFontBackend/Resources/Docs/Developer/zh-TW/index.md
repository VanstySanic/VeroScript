# 維護 VeroScriptFontBackend

## 原始碼職責與驗證

本模組維護字型塑形、點陣化與雙向文字分析提供者。公開介面宣告位於 [`Public/VeroScriptFontBackend.h`](../../../../Public/VeroScriptFontBackend.h)，模組專屬輸入位於 [`Resources/Tests`](../../../Tests)。

公共提供者邊界、重建要求、驗證與法律資源處理統一維護在[多媒體維護](../../../../../../../../Resources/Docs/Developer/zh-TW/multimedia.md)。


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/zh-TW/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/zh-TW/GENERAL.md)

<a id="native-provider-maintenance"></a>

可選 `VeroScriptFontBackendModule` 分別註冊字型和文字布局服務。靜態宿主使用 `veroscript_font_backend_query`，動態擴充宿主使用 `veroscript_extension_query`；轉換前逐項核對 Font 定義的對應服務契約。保留的 face 和分析物件須獨立於原始註冊控制代碼持有提供者租約。FreeType／HarfBuzz／SheenBidi 輸入、聲明及原生提供者建置歸本模組；公開契約、預設字型及獨立 mock 契約測試歸 Font。

## 提供者建置與生命週期

`VEROSCRIPT_BUILD_FONT_BACKEND` 選擇本模組；`VEROSCRIPT_FONT_BACKEND_NATIVE_EXTENSION` 建置匹配 Core 的提供者，`VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER` 選擇具名靜態入口。Font 擁有通用介面，不探索具體後端。FreeType／HarfBuzz 字型面、SheenBidi 分析、固定相依輸入及法律資源歸本模組。即使後端 ID 相同，font 與 text-layout 仍是獨立服務描述。

指令碼回歸明確連結匹配的 `VeroScriptBytecodeVm`，使用其公開工廠。檢查模組租約時，應在釋放登錄表控制代碼後分別保留字型面、分析物件及版面快照。允許執行時驗證純原生及匹配 Core 的建置、相依隔離與移動位置後的 SDK 使用端，並記錄實際失敗及未驗證目標。

[字型與版面公開契約](../../../../../../VeroScriptFont/Resources/Docs/User/zh-TW/index.md) · [GUI 使用端維護](../../../../../../VeroScriptGui/Resources/Docs/Developer/zh-TW/index.md#gui-text-layout-maintenance)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
