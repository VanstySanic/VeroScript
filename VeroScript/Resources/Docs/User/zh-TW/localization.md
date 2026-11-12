# 診斷本地化

本頁說明診斷與宿主工具介面的顯示語言。已載入的目錄選擇顯示文字，不改變指令碼語意。應用文字使用獨立的 [std::i18n API](standard-library/i18n.md)；修改應用內容不會改變處理程序的診斷語言。

語言選擇作用於已載入目錄中收錄的診斷。CLI 說明及部分參數驗證訊息目前仍為英文；目錄未收錄的訊息保留原文。

## 支援策略

標準套件提供 `en-US`、`fr-FR`、`ja-JP`、`ko-KR`、`zh-CN` 與 `zh-TW`。應用程式可用的語言取決於實際載入的目錄，可透過 `GetAvailableLanguageTags` 或對應 SDK API 查詢。有效的新增文化目錄可以提供其他語言；請將其隨應用程式攜帶，或明確載入其在地化根目錄。

語言選擇 API 建議使用規範的 BCP 47 標籤，例如 `en-US`、`zh-CN` 或 `zh-TW`。執行階段也會規範化作業系統常見寫法，例如 `ja_JP.UTF-8`、帶有 `@modifier` 的標籤以及 `C`/`POSIX`；規範化後的結果使用連字符、語言小寫、腳本首字母大寫和地區大寫。

正規化會先移除首尾空白、編碼和修飾符後綴，再將 `C`/`POSIX` 解析為 `en-US`，也支援 `C.UTF-8` 和 `POSIX.UTF-8@modifier`。

## 語言匹配與回退

標籤解析按以下順序進行：

1. 精確匹配已經載入的規範標籤；
2. 逐級去除最後一個子標籤，嘗試父標籤；
3. 在相同主語言的文化目錄中選擇確定性的候選；
4. 如果沒有任何可用候選，明確選擇 API 傳回失敗，初始化階段回退到 `en-US`。

中文會保留簡繁體語意：`zh-Hant-HK` 可以符合 `zh-TW`，`zh-Hans` 可以符合 `zh-CN`。明確指定的 `Hans` 或 `Hant` 子標籤優先於地區推斷；擴充及私人用途子標籤不參與書寫系統選擇。在預設資源目錄中，`zh-Hans-TW` 符合 `zh-CN`，`zh-Hant-CN` 符合 `zh-TW`。請求了簡體或繁體時，不會靜默選擇另一種書寫系統的目錄。翻譯缺少某個檔案或訊息鍵時，只要英文基準中存在對應條目，就回退到 `en-US` 的文字。

`TrySetLanguageFromTag` 成功後，語言會切換到實際符合的規範標籤；失敗時不會改變目前語言。 `GetAvailableLanguageTags` 傳回目前目錄中已經載入的語言集合。

英文基準目錄中沒有的診斷保留原文。載入譯文目錄不會翻譯腳本輸出或應用中的任意字串。

## 選擇語言

CLI 使用 `--lang`，環境變數使用 `VERO_LANG`：

```powershell
veroscript --lang zh-TW --check script.vs
$env:VERO_LANG = "en-US"
veroscript --check script.vs
```

CLI 先載入環境和本地化目錄，再處理命令列參數，因此 `--lang` 覆寫 `VERO_LANG`。 `VERO_LANG` 又覆寫作業系統語言；若環境或作業系統語言無法匹配，初始化會使用 `en-US`。 CLI 明確傳入一個無法匹配的 `--lang` 時會報告參數錯誤。

對於由編輯器管理的語言伺服器工作階段，應在開啟文件前把 BCP 47 偏好寫入
`initialize.locale`。`workspace/didChangeConfiguration` 可以修改分析設定，但語言
最好在下一次初始化時套用，確保所有開啟文件使用同一個目錄。`veroscript-lsp` 與
CLI 使用相同的 `tooling.json` 和 compiler 目錄；用戶端邏輯應使用診斷 ID，不應依賴
翻譯後的訊息文字。
解析器的宣告診斷（包括 `static class` 和 `static struct` 的限制）也使用相同的穩定
ID 和目錄載入路徑。因此，對於相同的原始碼錯誤，CLI 與 LSP 會選取相同的本地化訊息。

## 目錄佈局

在地化根目錄必須包含完整的 `en-US` 基準目錄；其他一級目錄都是文化目錄，目錄名稱必須是帶有地區或其他限定部分的規範 BCP 47 標籤。 VeroScript 目前要求文化目錄名稱包含連字符，因此單獨的 `zh`、`ja` 或非規範的 `zh_cn` 會被拒絕。

預設目錄範例為：

```text
localization/
  en-US/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
  <culture>/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
```

也可以繼續以相同規則增加文化目錄或在文化目錄下以相對路徑拆分 JSON 檔案：

```text
localization/
  en-US/
    runtime.json
    tooling/compiler.json
  <culture>/
    runtime.json
    tooling/compiler.json
```

載入器會遞歸發現 `.json` 檔案，並以相對路徑合併。對於每個翻譯文化：

- 翻譯檔案必須在 `en-US` 下存在同一路徑；
- 翻譯檔案不能增加英文檔案中沒有的訊息 ID；
- 缺少翻譯檔案或訊息鍵時使用英文文字；
- 同一文化內的檔案不能重複訊息 ID；
- 文化目錄不能重複、不能為空，也不能使用非規範或不帶限定部分的標籤。

每個 JSON 檔案必須是 UTF-8 編碼的扁平對象，鍵和值都必須是字串：

```json
{
	"diag.example": "Example message",
	"diag.with_argument": "Value: {0}"
}
```

文字可以使用 `{0}` 到 `{9}` 佔位符接收診斷參數。 JSON 必須是完整對象，不能有重複鍵、非法轉義、無效 UTF-8、尾隨內容或非字串值。

## 載入、替換與合併

`VERO_LOCALIZATION_DIR` 指定完整的在地化根目錄，請在啟動工具或初始化 SDK 前設定。沒有有效的明確指定目錄時，執行階段優先嘗試執行檔附近的發行資源（`localization`、`../assets/localization`），之後才嘗試工作目錄資源。需要確定使用自己的目錄時，設定此變數或明確呼叫 `LoadCatalogDirectory`。自動探索僅在尚未載入目錄樹時執行。

`LoadCatalogDirectory` 會校驗並取代目前行程中的完整目錄樹；載入失敗時保留原先的已載入目錄。 `MergeCatalogDirectory` 要求傳入的目錄本身也是完整且有效的目錄樹，然後依文化合併：預設不覆寫已經存在的訊息 ID，明確啟用覆寫時才取代已有文字。兩種操作都在核心中使用執行緒安全的處理程序級本地化狀態。

標準庫搜尋根、函式庫套件根或模組檔案旁邊可以攜帶同級的 `localization/` 目錄。模組載入器會以非覆蓋方式合併這些目錄；這使程式庫能夠提供自己的訊息 ID，同時避免意外改寫宿主已經載入的文字。來源覆蓋仍保留模組的邏輯路徑，因此旁側目錄仍然可以被發現。

## SDK 邊界

- C++ 宿主直接使用 `VeroScript::Locale` API；語言和目錄狀態是核心行程層級狀態。
- Interop 透過 `vs_interop_locale_set_language`、`vs_interop_locale_get_language`、`vs_interop_locale_get_available_languages`、`vs_interop_locale_load_catalog_directory` 和 `vs_interop_locale_merge_catalog_directory` 暴露相同策略。
- WebAssembly/WASM 建置會把建置時的整個 `localization` 樹嵌入虛擬檔案系統；`setLanguage` 只能選擇該建置實際攜帶的目錄，`language` 和 `availableLanguages` 用於讀取規範化後的目前語言與可用目錄。

CLI 環境變數與命令列選項請參考[命令列工具](../../../../../VeroScriptCli/Resources/Docs/User/zh-TW/index.md)，C++ 宿主設定請參考[宿主整合](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/embedding.md)，Interop 介面請參考[Interop SDK](../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md)，WebAssembly/WASM 介面請參考[WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-TW/index.md)。

獨立的 Python Frontend、BindGen、Transpiler 和 AOT 工具分別選擇 `python_tools.*` 訊息，其 `--lang` 優先於 `VERO_LANG` 和系統語言。它們規範化系統標籤、保留中文簡繁體，依精確、父層、確定性的同語言目錄順序比對，無比對或缺失文字時回退到英文。這不會修改原生 SDK 的行程層級語言。見 [Python frontend](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/frontend.md)。


## 行動本地化的明確設定

原生 Android AAR 與 iOS C ABI SDK 攜帶六語言目錄，需要明確載入。執行腳本前，將完整目錄樹複製至應用管理的真實目錄；APK 資源路徑不是檔案系統目錄。Android AAR 資源位於 `veroscript/localization`，iOS SDK 資源位於 `assets/localization`。Android 提供 `VeroScriptEngine.loadCatalogDirectory(path)` 與 `setLanguage(tag)`，皆傳回布林成功值。iOS C ABI 提供 `vs_interop_locale_load_catalog_directory` 與 `vs_interop_locale_set_language`；用 `vs_interop_buffer_destroy` 釋放錯誤緩衝區。檢查每個傳回值。

C# 行動應用先用 `VeroScriptMobileResources.InstallAsync` 安裝資源，再呼叫 `VeroScriptRuntime.LoadCatalogDirectory` 並設定 `VeroScriptRuntime.CurrentLanguage`。Python 在匯入 `veroscript` 時載入相鄰的 `veroscript/localization` 目錄樹；部署時保留完整目錄，再呼叫 `veroscript.set_language`。見 [C# 行動整合](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/mobile.md)與 [Python 行動整合](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/mobile.md)。

語言選擇在行程內共用，並非各引擎獨立設定；選擇失敗保留目前語言。腳本輸出不變。目錄中沒有的訊息保留原文；記憶體配置失敗也可能保留英文。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/localization.md) · [fr-FR](../fr-FR/localization.md) · [ja-JP](../ja-JP/localization.md) · [ko-KR](../ko-KR/localization.md) · [zh-CN](../zh-CN/localization.md) · [zh-TW](localization.md)
