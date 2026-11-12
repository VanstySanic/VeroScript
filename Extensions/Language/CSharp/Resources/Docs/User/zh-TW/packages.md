# C# 套件、反射與在地化

## 套件中繼資料

請參閱[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#package-container-format)。

`GetMetadata()` 回傳已擁有的中繼資料，`HasCompiledModule` 表示套件是否仍可
轉移編譯模組，`GetCanonicalSignedBytes()` 回傳簽章涵蓋的位元組。目錄可透過
`VeroScriptRuntime.LoadCatalogDirectory` 或 `VeroScriptRuntime.MergeCatalogDirectory`
載入或合併。

`VeroScriptPackage.Read` 用於解析套件位元組；`LoadPackage`、`ReloadPackage` 和 `BuildPackageFile` 處理套件；`VeroScriptPackageKind` 與 `VeroScriptPackageMetadata` 描述類型和中繼資料。套件是傳輸與載入邊界，不是安全沙箱；宿主必須限制繫結、路徑、輸入、資源和執行時間。

`ExportReflectionJson` 匯出腳本函式、型別、欄位、方法、參數和套件資訊；`ExportBindingsJson` 匯出目前的宿主繫結。反射資訊可用於工具和介面驗證，但不能取代執行時期權限檢查。`EnumerateObjectReferences` 的遍歷值遵守借用生命週期。

`VeroScriptRuntime` 提供目前語言、可用語言 JSON、目錄載入與合併功能。使用 `GetAvailableLanguagesJson` 發現目前建置實際包含的 BCP 47 標籤；目錄合併應使用受信任發布物並驗證訊息鍵及格式參數。語言只影響診斷和格式化，不改變腳本語意。 SDK 自身的參數或生命週期檢查例外可能使用英文；語言選擇適用於執行階段診斷。

使用 `VeroScriptRuntime.Capabilities` 檢查 JIT、套件、反射、熱重載、序列化與執行治理能力。執行治理包括協作式指令/時間限制、取消和標準函式庫能力策略。效能應分開測量冷啟動、熱呼叫、快取、值轉換和宿主回呼。

`VeroScriptRuntime.AbiVersion` 必須與配接器的 ABI `0x00000101`（0.1.1）完全一致。託管套件、原生 Interop 函式庫和執行階段目錄應來自匹配的發佈版本。

## 模組所有權與簽章

`LoadPackage` 或 `ReloadPackage` 成功後，編譯模組會轉移給工作階段。套件中繼資料與規範簽章位元組仍可讀取，但同一個套件實例不能再次轉移編譯模組；需要另一次轉移時，請重新解析原始位元組。`Read` 驗證容器格式，不驗證發布者身分；載入不受信任的套件前，應針對 `GetCanonicalSignedBytes()` 驗證發布者簽章。`BuildPackageFile` 回傳套件位元組與 `VeroScriptRunResult`，選項控制應用程式/程式庫種類、名稱、版本、中繼資料及是否移除除錯資訊。

請參閱[C# 值與所有權](values.md#managed-call-lifetime)。

## 選擇診斷語言

將選定的標籤指派給 `VeroScriptRuntime.CurrentLanguage`，並透過 `VeroScriptRuntime.GetAvailableLanguagesJson()` 取得實際可用的標籤後再提供選項。此屬性改變處理序層級的診斷語言，切換時應與其他工作階段協調。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](packages.md)
