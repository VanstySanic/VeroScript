# VeroScript C++ 技術文件

本目錄是與 C++ 原始碼繫結的開發者材料，描述由公共標頭檔案、CMake 目標、測試和生成產物實現的契約。它不是使用者指南；API 名稱、命令列選項、JSON 鍵、診斷和示例程式碼不得被翻譯。

## 模組歸屬

- `VeroScriptCpp`：核心之上的高階 C++ 嵌入與會話介面。
- `VeroScriptCppAot`：把透過檢查的 VeroScript 程式發射為一個 C++ 源 Bundle；單翻譯單元使模板例項化、內部連結和初始化順序明確。
- `VeroScriptCppFrontend`：解析受支援 C++17 子集，提供給原始碼工具的穩定 AST。
- `VeroScriptCppBindGen`：把可表示的 C++ 宣告轉換為 Binding Catalog；目錄是後設資料而非原生呼叫 thunk。
- `VeroScriptCppTranspiler`：把保持語義的 C++17 子集降級為 VeroScript 源，並由真實編譯器驗證。

語言中立核心位於 `VeroScript/`。C++ 模組可依賴核心，核心不得依賴 C++ 工具。前端由 BindGen 與 Transpiler 共享，二者必須在解析後實施不同語義策略。

## 語義邊界

C++ 工具只接受有界子集；不支援的所有權、連結、指標、模板、預處理或控制流必須產出診斷。悄然近似任何一種構造都是 ABI 或原始碼正確性迴歸。前端在 AST 可表示時保留限定名、別名、屬性、宣告範圍、構造/析構、friend、型別引數模板和同檔案類外定義；BindGen 可以保留而 Transpiler 必須拒絕沒有 VeroScript 對等物的後設資料。這是兩項獨立策略，不能為了方便合併。

## 輸出與事務規則

多輸入工具必須在替換任何輸出前解析併合並全部輸入。目錄、API 引用、原始碼對映和清單作為一個邏輯結果原子替換；重複基名使用確定的路徑派生消歧。任何解析、合併、驗證或最終編譯失敗都不得留下部分輸出樹。生成 API 引用僅為文件，JSON Binding Catalog 才是編譯器/IDE 的機器輸入。

## 併發與宿主契約

嵌入和 AOT 宿主必須在完整執行期間保持回撥狀態存活，並同步可能被 worker 到達的狀態。指令碼全域性和類靜態欄位按原生 worker 隔離，`channel<T>` 是顯式同步跨 worker 能力；原生回撥、攔截器、變更觀察器和可達 AOT 符號可能併發。

## 引擎與會話契約

`VeroScriptEngine` 提供無狀態的 `CheckSource`、`CheckFile`、`RunSource` 和 `RunFile`；檔案檢查和執行可使用 bytecode cache，源標籤、I/O/error stream、迴圈匯入策略和專案範圍訪問策略均為顯式輸入。`VeroScriptSession` 是有狀態介面：執行期間拒絕配置、註冊、載入、編譯、提交、過載和清理。`LoadFile` 建立檔案過載路徑，源模組不能按路徑過載；所有過載先暫存驗證再提交，包種類與入口形狀必須匹配。`Reload`、`ReloadCompiledModule` 和 `ReloadPackage` 的失敗不得替換工作模組。

`Run(ERunMode::Eval)` 執行模組初始化而不呼叫 `main`；跨呼叫的全域狀態僅在啟用 `SetHotReloadEnabled(true)` 時保留。`Clear()` 清除載入狀態、最近 JIT 統計和可複用會話 JIT 程式碼。`SetJitPolicy` 影響後續執行。普通 `CallFunction` 可在新 VM 間複用已編譯程式碼；`SetJitCacheLimits` 和 `GetJitCacheStats` 提供當前代/配置的受限保留資源與累計編譯資訊。

## 公共 C++ AOT 與工具輸出

AOT API 返回單個生成 `.cpp` 字串，或包含相對原始檔和 manifest 的 `FCppAotSourceBundle`。BindGen 返回合併目錄、可選 API 引用和專案 manifest；Transpiler 返回生成源和 source map，專案模式還為每個輸入返回一個檔案記錄和 manifest。共享輸出 writer 是原子的：解析、合併、lowering、驗證或路徑衝突失敗後不會替換輸出檔案。

frontend AST 記錄源範圍、宣告 attribute、alias、template、C linkage、storage/semantic qualifier、body 和類外定義。`FTypeMapOptions` 明確控制 raw host pointer、rvalue reference、已知 host type、已知 enum type、已知 type parameter 與已知 alias。

## 詳細頁面

- [C++ 組架構](architecture.md)
- [C++ 嵌入與會話](session.md)
- [C++ 前端、BindGen 與 Transpiler](frontend-and-tools.md)
- [C++ AOT 發射器](aot.md)
- [C++ 構建與測試契約](build-and-tests.md)

## 執行治理與會話可觀測性

`VeroScriptSession` 還提供 `SetExecutionLimits`、`SetExecutionControl`、`RequestCancel`、`SetCapabilities`、`SetJitPolicy` 和 `GetLastJitStats`，並透過 `EExecutionBackend` 請求執行後端。零限制不限，取消是協作式的，能力限制必須由 VM 強制執行。符合條件的 `Run`、`CallFunction`、`CallMethod` 和保留 callable 呼叫可共用同代 JIT 快取，並會在模組或執行配置改變時失效。公共呼叫面還包含例項呼叫、構造、欄位/全域性訪問、可呼叫物件、反射、外部序列化和 `CallMethodBypassingInterceptor`；後者僅繞過所選入口，巢狀呼叫仍經過攔截器。

## 依據

`Public/` 下公共頭、各模組 `Private*` 實現、模組 CMake 檔案和專門測試是權威依據。修改公共契約、輸出格式、支援子集、所有權或併發保證時必須同步更新本檔案；識別符號、路徑、目標名、診斷和版本值保持原始碼原樣。

## Session JIT 重用

Session 程式碼重用資格、執行治理、保留限制與記帳統一維護在 [C++ Session](session.md#session-jit-cache)。

## 保留位元組配額

`MaxNativeBytes` 或 `MaxRetainedPayloadBytes` 為零不允許保留對應位元組；與表示無限制的零策略預算不同。

## 取消與宿主投遞

請參閱[C++ 嵌入與會話 — 取消與宿主投遞](session.md#session-control-and-streams)。

共用同一個 `FExecutionControl` 權杖的 Session 會共用取消狀態。呼叫 `Reset()` 並重用權杖前，應等待先前的取消呼叫結束，避免舊要求取消下一次使用該權杖的執行。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
