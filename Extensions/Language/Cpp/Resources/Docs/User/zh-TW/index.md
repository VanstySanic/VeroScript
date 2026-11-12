# C++ SDK

C++ SDK 是 VeroScript 的原生整合介面，包含執行階段嵌入 API 以及面向 C++ 的原始碼工具。Python、C#、Rust 等配接器使用的 C ABI 另見 [Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md)。

## 模組選擇

| 模組 | 用途 |
| --- | --- |
| [嵌入](embedding.md) | 從 C++ 宿主檢查、載入、執行、呼叫、重新載入和設定 VeroScript。 |
| [宿主繫結](bindings.md) | 描述並註冊原生函式、宿主型別、成員、運算子和泛型宿主型別。 |
| [AOT](aot.md) | 為原生建置從 VeroScript 產生 C++17 原始碼。 |
| [C++ 前端](frontend.md) | 將支援的 C++17 子集解析為共用公開 AST。 |
| [BindGen](bindgen.md) | 從 C++ 宣告產生繫結目錄與面向人的 API 參考。 |
| [Transpiler](transpiler.md) | 將支援的 C++17 子集轉換為 VeroScript 原始碼與原始碼對映。 |

前端由 BindGen 和 Transpiler 共用。它有意只接受邊界明確的宣告/原始碼子集；無法保持語意的構造會產生診斷並失敗，不會靜默近似。這些工具都不是通用 C++ 編譯器。

## 整合順序

<a id="explicit-vm-selection"></a>

執行前，透過 `FEngineConfig::VmBackendFactory`／`VmBackendPath` 或 Session 選擇 API 設定相符 VM。僅連結 C++ SDK 不會選取直譯器，指令碼編譯處理器也需要 VM。所需 SDK 目標與設定步驟見 [VeroScriptBytecodeVm](../../../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/zh-TW/index.md)。

1. 一次性處理使用 `VeroScriptEngine`，長期持有模組和狀態使用 `VeroScriptSession`。
2. 載入模組前註冊宿主函式和型別。
3. 需要分開報告驗證錯誤時，先呼叫 `CheckSource`/`CheckFile`，再呼叫 `RunSource`/`RunFile`。
4. 如果 IDE、LSP、套件或靜態檢查器需要宿主介面，匯出並校驗繫結目錄。
5. 回呼和 worker 執行可能並行，宿主擁有的狀態必須自行同步。

腳本全域變數和類別靜態欄位按原生執行緒隔離；需要明確同步共享時使用 `channel<T>`。原生回呼、執行個體攔截器、變更觀察器以及 AOT 宿主符號都可能從 worker 並行呼叫。

## 受管理記憶體

宿主介面：`VeroScriptSession::GetMemoryContext()` / `VeroScript::Memory`。所有權、配額、檢視失效與租借規則請參閱 [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)。

## 原始碼工具的診斷語言

C++ 前端及 C++ BindGen/Transpiler 自身的說明、引數錯誤和宣告診斷目前使用英文。BindGen/Transpiler CLI 不接受 `--lang`，切換執行時期診斷語言不會翻譯工具自身的訊息。診斷代碼和原始碼位置仍可供程式處理。

## 模組環境與後端

提供的模組環境含執行解析器時，Engine／Session 可繼承其中的自動後端選擇。明確工廠與檔案／目錄仍優先，原始 `VeroScriptVm` 仍需明確組裝。參見[服務後端選擇](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/introduction.md#backend-selection)。

<!-- docs-navigation -->

---

[使用者文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
