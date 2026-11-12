# 應用程式國際化執行契約

核心負責值型別、原子設定根、不可變提供者快照及一般原生呼叫診斷。應用程式國際化不修改診斷語言。ICU、資源檔案格式及 GUI 更新由擴充模組負責。

## 公開 API

- [`VeroScriptI18n.h`](../../../../Public/VeroScriptI18n.h) 提供 `FI18nArgument`、`FI18nArguments`、`FI18nConfiguration`、`FI18nText`、`FI18nContext`、`FI18nSnapshot`、`FI18nVersion`。
- 使用 `CreateContext(config, services, out, error)` 建立內容，以 `CaptureSnapshot()`、`TryLookup`、`TryResolve`、`TryFormat` 查詢；`ToScriptValue` / `FromScriptValue` 驗證 Core 包裝物件身分。
- `FI18nServices::Publish` 以不可分割操作發佈兩個不可變檢視；`PublishResources` / `PublishFormatter` 保留另一側的最新檢視，`Acquire` 傳回擁有資料的快照。資源或設定內容改變時使用新檢視。同一檢視重新發佈時若省略擁有者，會保留已有模組參考；具有擁有控制區塊的空別名 shared_ptr 也是有效的保活參考。
- `ConfigureAndSetServices` 以不可分割操作替換設定及服務參考，不會以交易方式修改其他內容共用的服務本體。`Set*` 方法與其他欄位的最新值合併。
- 資源解析器每次接收一個精確的標準語言，傳回 `Found` / `NotFound` / `Failed`；找到空範本也是成功。明確的後援鏈僅由 Core 處理。
- 格式化器不可變，允許並行呼叫 `SupportsFormat` 和 `Format`。要求保留原始具型別引數、要求語言、符合語言、有效格式地區設定及具型別選項。`literal` / `vero.named` 保留給 Core。
- `FI18nWork` 借用目前作業的檢查回呼與輸出上限，零表示不限。耗時的提供者工作須檢查取消，並在配置輸出前遵守預算；Core 在回呼前後檢查，但無法強制中斷阻塞的原生呼叫。
- 值與快照擁有資料，不得保留回呼內容、VM、借用的要求檢視或 `FI18nWork`。單一一般原生值的修改由呼叫者在外部同步；內容及服務方法具執行緒安全性。自訂值和提供者須不可變並支援並行讀取。
- 提供者/自訂值的模組擁有者須比回呼及虛擬解構函式存續更久，動態程式碼須明確提供擁有者。避免內容/服務 → 提供者 → 內容/服務的強參考循環，請使用弱參考。
- 同一內容的 `Try*` 遞迴按執行緒拒絕。提供者內仍可讀取或更新設定；即使回呼替換或重設呼叫者的值變數，目前作業仍固定持有起始快照與文字值。借用的 string_view 輸入須在傳回前有效，錯誤輸出不得擁有輸入字串；不得重入正在執行的 VM。
- 版本相等性包含內容身分、設定修訂、資源修訂及格式化器修訂。修訂耗盡時失敗，不循環，也不具持久化或跨程序意義。
- 結構化失敗使用 `FI18nError::Status`、自有 `Diagnostic` 及網域/索引鍵/語言/名稱。解析器的 `NotFound` 不應攜帶錯誤；失敗須有非 Ok 狀態與診斷，成功回呼不得傳回錯誤。
- `FNativeCallContext::DiagnosticOutput` 是自有 `FScriptError` 的借用輸出；VM/JIT 橋接保留 ID/引數並設定目前呼叫位置。`I18nServices` 提供目前呼叫借用的服務存取；腳本建立的內容持有具擁有權的服務選擇，不持有 VM。
- 設定 `VeroScriptVm::SetI18nServices`，或 C++ Engine/Session 的 `FEngineConfig::I18nServices` / `I18nLimits`。工作執行緒繼承服務選擇及預算，使用自身執行控制；宿主設定改變後，已有內容仍保留原服務參考。

## 編譯與執行

剖析器與編譯器使用共用 `StandardLibrary::GetBindingCatalog`，標準記憶體項目位於 i18n 項目之前。既有 Core 相對操作碼保留內部名稱 `CallMemory`，改為索引共用目錄，不增加新操作碼或 `FValue` 分支。`Memory::UsesRuntimeMemory` 排除 i18n 項目。位元碼/套件載入檢查相同目錄；存續中的內容、提供者與自訂承載資料屬於執行階段值，不會序列化至程式庫。

AsmJit 在進入前拒絕不支援的 Core 相對呼叫，沿用 VM 後援且不重播回呼。標準值使用一般宿主複製/相等協定，一般值相等性不要求索引鍵雜湊；Map/Set 索引鍵相等性仍要求有效的雜湊協定。

一般具名宿主方法支援依參數型別多載。腳本 API 保留 `text::message_key(domain,key)` / `text::message(domain,key,args)` 及 `format` / `format_with`；型別化格式化不會隱式呼叫僅接受字串的位置參數 `std::format`。

## Validation

`VeroScriptI18nTests` 涵蓋純量精確度、自有/COW 資料、提供者、版本、並行發佈、重入、取消/輸出限制、腳本泛型/容器/Attribute、工作執行緒繼承、位元碼/程式庫往返、VM/Mixed/JIT 及診斷橋接。JIT 部分在提供相容後端時執行；`VeroScriptI18nExample` 連結 Core 和相符的 Bytecode VM SDK。

[I18n SDK 範例](../../../Examples/I18n/README.md) · [使用者 API](../../User/zh-TW/standard-library/i18n.md).

<!-- docs-navigation -->

---

[開發文件](index.md) · [en-US](../en-US/i18n-runtime.md) · [fr-FR](../fr-FR/i18n-runtime.md) · [ja-JP](../ja-JP/i18n-runtime.md) · [ko-KR](../ko-KR/i18n-runtime.md) · [zh-CN](../zh-CN/i18n-runtime.md) · [zh-TW](i18n-runtime.md)
