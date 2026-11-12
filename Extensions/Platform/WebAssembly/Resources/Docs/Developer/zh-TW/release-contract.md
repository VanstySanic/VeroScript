# WebAssembly ABI、產物與發行契約

## 三個公開介面

`VeroScriptWebAssembly.h` 定義 C++ 介面，`VeroScriptWebAssemblyC.h` 定義 WebAssembly C ABI，TypeScript 包裝層與型別宣告組成瀏覽器 SDK。共同的結果欄位、後端名稱、語言標籤和輸入行為必須一致。獨立的原生 Interop ABI 不定義 WebAssembly ABI。

## 名稱與相容性

模組與 CMake 目標使用 `VeroScriptWebAssembly`，C++ 命名空間為 `VeroScript::WebAssembly`，TypeScript 入口為 `veroscript-webassembly.ts`。SDK 封存檔名為 `VeroScriptWebAssemblySDK`，安裝程式名為 `VeroScriptWebAssemblyInstaller`。保留 C ABI 名稱 `vero_web_*`、`VeroWeb*`、`VERO_WEB_*` 及 ABI 版本 `0.1.1`。安裝設定 `web`、元件 `web-sdk` 和產品 ID `dev.veroscript.web` 用於識別既有安裝記錄。

## 產物與出版驗證

出版完整八項檔案：一般和快速引擎的 `.mjs`/`.wasm`、包裝層 `.ts`/`.d.ts`，以及兩種引擎的 `.d.mts` 宣告。標準程式庫和本地化目錄內嵌於虛擬檔案系統。執行繫結與宣告型別檢查、完整清單稽核，並在無頭 Chromium 中驗證互動輸入、Web Worker 執行與終止。產物稽核對 Release 套用下載預算：互動 Wasm 為 17 MiB，快速 Wasm 為 11 MiB；這是出版限制，不是 ABI 限制，僅通過原始碼檢查不能證明瀏覽器驗收完成。

目前基準包含精確數值及巢狀、區域和泛型型別作用域。

Web 產品靜態組裝 `VeroScriptBytecodeVm`。原始碼檢查、編譯擴充、直接執行和可重複執行的編譯程式均使用明確選定的解譯器。

兩種 Wasm 引擎均透過 Core 支援初始化擷取、`mutable` 閉包與區域 `static` 變數。值擷取預設為唯讀；複製函式值會共用環境，重新求值 Lambda 則建立新環境。區域靜態變數在首次使用時初始化，初始化失敗後可以重試。每次程式執行都使用全新狀態，包括區域靜態變數；互動執行亦同。Web ABI 與 TypeScript API 保持不變。

## ABI 與本地化

執行時期語意版本、WebAssembly ABI 版本、位元組碼版本、能力旗標和選定語言分別記錄。在執行時期探索實際可用的 BCP 47 目錄標籤，不翻譯 API 識別碼、通訊協定欄位、診斷 ID 或版本，也不按固定語言數量推斷行為。

## 程式與輸入能力

C ABI 匯出、TypeScript 宣告和能力偵測必須一致。一次編譯多次執行需要編譯、執行和銷毀匯出；互動式程式執行還需要 Asyncify 互動匯出。缺少新增匯出時應回報能力不可用，不能虛構程式控制代碼。

內容陣列的 UTF-8 路徑與原始碼數量相等，僅在呼叫期間借用。程式控制代碼歸建立它的執行時期所有，不能移至其他執行個體。結果 JSON 攜帶診斷、輸出、要求的後端策略、計時和 JIT 統計。出版驗證須涵蓋控制代碼重複使用、每次執行的新狀態、銷毀、互動暫停及語言選擇。

<a id="web-execution-policy"></a>

發行模組不選擇 JIT 後端。`vm` 解譯執行位元碼；`mixed` 維持預設值，也透過 VM 執行。`jit` 在應用程式初始化、`main` 執行或要求輸入前回傳結構化診斷 `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory`。原生 JIT 執行層歸屬 `Modules/Execution/VeroScriptAsmJit`；Web 模組不內建也不動態載入該 JIT 後端。原先指定 `jit` 的呼叫端需要改用 `vm` 或 `mixed`。策略名稱與 C ABI ID 保持穩定；`result.backend` 記錄要求的策略，所有 JIT 計數均為零。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/release-contract.md) · [fr-FR](../fr-FR/release-contract.md) · [ja-JP](../ja-JP/release-contract.md) · [ko-KR](../ko-KR/release-contract.md) · [zh-CN](../zh-CN/release-contract.md) · [zh-TW](release-contract.md)
