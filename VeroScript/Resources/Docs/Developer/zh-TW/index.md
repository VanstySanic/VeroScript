# VeroScript 核心技術文件

[可替換 VM 後端](vm-backends.md)

本目錄面向核心維護者與模組作者，說明由實現、公共標頭檔案、構建目標和測試約束的技術契約；它不是使用者教程，也不替代 API 參考。

## 編譯管線

核心依次解析源、覆蓋層、匯入和包，進行詞法分析、預處理、公共 AST 構建、語義/型別/泛型檢查，生成帶除錯資訊的位元組碼，最後建立模組或包並透過 VM 或 JIT 執行。每個階段都必須保留可定位的結構化診斷，失敗不會生成可部分使用的產物。

模組載入器以顯式策略處理匯入、隱式匯入、迴圈關係和專案範圍訪問。匯入路徑、隱式匯入、stdlib 搜尋根、源覆蓋層、`-D` 定義、預定義宏、宿主型別名稱、迴圈匯入策略和專案範圍訪問策略均為顯式輸入；路徑、覆蓋層和宏定義會規範化並參與輸入指紋。宿主型別應在解析前註冊。新增 stdlib 根目錄時可合併相鄰 `localization/` 樹，因此打包的 stdlib 與其診斷資源仍是單一資源單元。

## 執行時不變數

`VeroScriptVm`、`FValue`、物件/容器控制代碼、函式值、宿主值和引用構成同一個所有權模型。析構、引用、變更事件、外部序列化及保留可呼叫物件都必須保持物件身份和生命週期。容器變更回撥觀察的是已完成的變更；回撥異常不會回滾該變更。VM 所有權、位元組碼後設資料、異常和控制流必須對所有後端一致。每個工作執行緒隔離指令碼全域性變數和靜態欄位，`channel<T>` 是跨工作執行緒共享狀態的顯式同步機制；任務登錄檔與任務 ID 也是 worker 本地的。會話和保留可呼叫物件的建立執行緒限制屬於嵌入契約。宿主回撥及可達宿主狀態可能併發，生命週期和同步責任仍屬於宿主。

## 執行後端與治理

請參閱[核心架構與編譯管線 — 公共/私有邊界](architecture-and-pipeline.md#core-vm-ownership)。

`FJitPolicy` 和 `FJitStats` 描述自適應編譯及最近一次執行。普通自由函式呼叫可按受限資格跨新 VM 複用會話程式碼；`FSessionJitCacheLimits` 的預設值為 8 MiB 原生程式碼和 32 MiB 總保留載荷，零禁止對應保留。快取統計是當前上下文的累計、穩定計量，不是實時或程序峰值。

## 公共與私有邊界

`Public/` 是受支援的原生 include 介面；`Private/` 包含 compiler、parser、VM、JIT 和實現細節。外部消費者和語言介面卡必須使用公共標頭檔案。原始碼樹內目前只有少量 embedding/tooling/AOT implementation target 由 CMake 明確取得 private include path；它們是需要核心負責人審查的 friend dependency，不是安裝或支援的 API，也不能隨意擴大。

原生 C++ SDK 是要求 build-matched 的 static-library contract：消費者必須以完全匹配的 header、library、architecture、runtime 和 build identity 重新建置，不承諾跨 build 的 C++ binary-layout compatibility。穩定的 process ABI 僅指 C11 `VeroScriptInterop` 介面。公共 C++ layout、enum value、bytecode format、host-binding record、diagnostic ID 或 package metadata 變更仍需 compatibility review。static state、generic instantiation 和 closure capture 的 ownership rule 由核心定義，language adapter 不能重寫。

## 本地化與執行時資源

部署必須包含標準庫和本地化目錄。載入器要求 `en-US` 基線、BCP 47 文化目錄、匹配且唯一的 JSON 鍵，並可從多個模組根目錄合併目錄。語言集合動態發現；宿主 UI 文字與執行時診斷目錄仍是獨立資產。

## 會話、包與繫結

會話在執行期間拒絕會改變配置、載入、提交、過載、清理或繫結的操作。檔案載入建立可過載路徑，源字串載入不會。包和過載採用暫存、驗證、提交流程，失敗始終保留上一可工作模組。反射、外部序列化、可保留可呼叫物件、欄位/全域性訪問、攔截器和變更回撥分別擁有其生命週期與空閒狀態條件。

## 詳細頁面

- [核心架構與編譯管線](architecture-and-pipeline.md)
- [核心構建與執行時資源](build-and-assets.md)
- [核心執行、狀態與包](execution-and-state.md)
- [核心 JIT 與併發](jit-and-concurrency.md)

## 執行治理與 JIT 可觀測性

VM 透過 `FExecutionLimits`、`FExecutionControl` 和 `FScriptCapabilities` 提供協作式執行治理。指令與牆鍾限制為零表示執行不限。取消是協作式的：`RequestCancel` 標記共享控制物件，但不能強制終止已在宿主執行緒執行的原生或託管回撥。

能力策略控制檔案、環境、控制檯和任務等標準庫副作用。活動限制必須由 VM 強制實施，不能被 JIT 程式碼繞過。請求後端是 `Vm`、`Mixed` 或 `Jit`；偵錯程式附加、保留初始化區間、有界清理以及需要直譯器的執行治理會選擇有效 VM 路徑。

JIT 策略和 `FJitStats` 是公共可觀測性契約。統計會區分編譯、快取、排程、回退/失敗、預算、入口種類和快速路徑活動。未受支援的編譯是正常回退決定，除非執行本身失敗，否則不是語言診斷。

## 編譯器模型更新

parser/compiler 模型會把 static 型別修飾符和 static 資料成員保留為模組擁有的後備全域性變數。靜態訪問透過宣告型別層級和 interface 解析，泛型例項化會獲得各自的替換後靜態儲存。static 型別限制是 compiler 強制的語義規則，不是 emitter 約定。

AST 還保留 lambda 捕獲後設資料、泛型型別引數與邊界、alias、宣告 annotation，以及診斷和工具鏈所需的源範圍。任何消費 AST 的 lowering 路徑都必須保持這些身份和源位置；不支援的捕獲或泛型形式必須明確失敗，不能近似。

## 變更時的依據

語言行為以公共標頭檔案、核心實現、模組 CMake 檔案、專門測試和釋出資源清單為準。修改編譯模型、後端、併發、包、本地化或穩定契約時，必須在同一變更中更新這些文件。識別符號、路徑、目標名、診斷 ID、JSON 鍵和版本值均按原始碼原樣書寫，不翻譯。

- [在地化資源與部署](localization-and-resources.md)

- [受管理記憶體實作契約](memory-runtime.md)

- [通用編譯處理與原始碼中繼資料](compile-processing.md)
- [腳本編譯擴充](script-compile-extensions.md)

- [應用程式國際化執行契約](i18n-runtime.md)

- [模組環境與原生擴充](module-environments.md)

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
