# 核心構建與執行時資源

[可替換 VM 後端](vm-backends.md)

## 構建目標

請參閱[可替換 VM 後端](vm-backends.md#vm-provider-assembly)。

## 資源依賴

該目標依賴 `VeroScriptRuntimeAssets`。它使用共用的 `Resources/Tools/CMake/VeroScriptLocalization.cmake` 部署設定彙整目錄，刪除過期的目錄 JSON，複製有變更的目錄檔案，並把標準函式庫檔案複製到執行階段輸出目錄。單獨建置測試目標時也保留此依賴，確保測試使用目前原始碼的目錄資源。

本地化載入器先嘗試 `VERO_LOCALIZATION_DIR`，再搜尋部署目錄和原始碼儲存庫；準確順序見[診斷本地化](localization-and-resources.md)。有效目錄樹須包含 `en-US` 基準、規範且帶限定資訊的 BCP 47 語言目錄和唯一訊息 ID。翻譯檔案與鍵可以是英文的子集，缺失項回退到英文。模組和標準函式庫根目錄可非覆寫地合併有效目錄。可用語言從已載入的資源中探索。

## 相容性

公開的 bytecode、package、diagnostic、host-binding 和 locale 行為與是否啟用 asmjit 無關。架構專用的原生 leaf 必須描述為執行能力，而不是新的語言或位元組碼版本。

## 建置基礎設施與原始碼目錄補充

本獨立儲存庫的輔助工具位於 `Resources/Tools/CMake`，從儲存庫根目錄直接引用這些路徑，無需 `Script/` 前綴。 公開 locale API 宣告 `InitializeModuleFromEnvironment`，其參數相對於發現的原始碼模組，例如 `VeroScriptTooling` 或 `Extensions/Language/Cpp/VeroScriptCpp`。它僅在核心選擇原始碼目錄後，以不覆寫既有 ID 的方式合併；明確取代目錄會清除這項原始碼探索狀態。`VeroScriptLocaleSourceTests` 驗證模組改名後的探索和模組目錄補充。

- [在地化資源與部署](localization-and-resources.md)

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/build-and-assets.md) · [fr-FR](../fr-FR/build-and-assets.md) · [ja-JP](../ja-JP/build-and-assets.md) · [ko-KR](../ko-KR/build-and-assets.md) · [zh-CN](../zh-CN/build-and-assets.md) · [zh-TW](build-and-assets.md)
