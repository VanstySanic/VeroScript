# VeroScript — Rider

## 安裝

透過「設定 → 外掛 → 從磁碟安裝」安裝 ZIP。Rider 2026.2.2（262.10315.191）是 SDK 驗證基準與宣告的最低組建；更高版本仍需相容性驗證。編輯、執行、檢查與本機偵錯需要解壓 相容的 Runtime，在「語言和框架 → VeroScript」選擇根目錄，直下包含 `veroscript`、`veroscript-lsp`、`veroscript-dap`、`stdlib/`、`localization/`。專案個人 Runtime 優先於應用預設值；兩項皆空則透過 `PATH` 尋找。基礎著色不需 Runtime。替換工具後重新啟動語言服務。

更新或移動 Runtime 時，保留完整的解壓目錄，包括 `Vm/` 及配套原生程式庫。
工具負責提供預設 VM，不需新增 Rider 設定。

## 設定

專案語意設定為經過驗證的 JSON 物件。支援 `importPaths`、`implicitImportPaths`、`stdlibPath` 或 `stdlibSearchPaths`、`bindingCatalogPaths`、`defines`、`predefinedMacros`、`allowCircularImports`、`allowProjectWideAccess`、`lspTraceAnalysis`、`lspRequestTimeoutMs`、`lspAnalysisTimeoutMs`、`maxCompletionItems`、`maxWorkspaceSymbolResults`、`maxReferenceResults`、`maxWorkspaceFiles`、`maxWorkspaceFileBytes`。繫結目錄描述符號，實際回呼由宿主註冊。共享設定存於 `.idea/veroscript.xml`，Runtime 路徑為個人設定。工具語言為 BCP 47，留空跟隨 IDE；介面提供六語言。

## 編輯

`//` 註解行末的反斜線緊鄰 LF 或 CRLF 時，下一實體行仍屬於註解。本機著色與空區塊換行遵循相同規則，GUI 標記中的內嵌 VS 亦同；反斜線後有空格或定位字元時不接續。

懸停將符號類別與名稱分開顯示，內建基本型別與容器型別統一使用一個類別標題；原始碼定義的型別別名保留原類別與宣告。簽章、多載、說明、範例及定義位置仍來自 Runtime，並保留其內容。

VeroScript 設定中的「在空程式碼區塊內按 Enter 時，將左大括號移到新行」預設啟用。在同一行的空宣告區塊或控制區塊內按 Enter，會將左大括號移至獨立行，並將游標放在內部縮排處。註解、字串、選取範圍、多游標及不明確或跨行的標頭使用一般 Enter。只修改此設定不會重新啟動 LSP；全文及範圍格式化仍採用伺服器樣式。

診斷保留 Runtime 的結果。`void t(){}` 是合法的完整函式；`void t` 尚未完成，需要 `()`。編輯期間的診斷會保留，並在伺服器接受補全後的程式碼時清除。

## 路徑

專案路徑以根目錄為基準。工作目錄也從根目錄解析；入口、輸入與執行覆寫路徑（含 JIT）以解析後的工作目錄為基準。繼承路徑保持原基準；`$PROJECT_DIR$` 始終指向根目錄。省略選項繼承專案設定，明確清單（包含空清單）在啟動時取代；執行覆寫不修改 LSP 設定。

## 執行與檢查

右鍵保存的 `.vs` 或建立 VeroScript 執行組態。沒有 `main` 的頂層陳述也可作入口。`run` 執行，`check` 僅編譯且不接受程式引數，`attach` 使用 Debug 連線。引數遵循 IDE 引號規則（`""` 為空引數），環境為字串 JSON 物件。一般輸入按位元組傳入，偵錯輸入為準備好的 UTF-8 文字。本機操作先保存文件。Current File 使用 Rider 設定；若未顯示可在 Registry 啟用 `run.current.file.item.in.run.configurations.combobox`。

上下文操作僅接受已儲存的本機檔案，獨立選擇 Run/Check，並清除範本中的附加專用欄位。
Check 同時清除繼承的腳本參數、輸入及 JIT 路徑，保留適用的工作目錄、環境和編譯設定。

## 本機偵錯與 JIT

一般執行支援 `vm`、`mixed`、`jit`。JIT 檔案／目錄欄位為選填，用於固定相容模組。留空會省略 CLI 路徑覆寫，繼承模組環境的選擇；未設定後端時預設為 auto。停用 JIT 或沒有可用自動候選時，mixed 使用 VM，強制 jit 在執行前失敗。檢查與本機偵錯不載入模組；附加須清空路徑。偵錯使用 VM。入口暫停 `inherit`/`true`/`false`，深度 0 保留預設；啟動準備預算 1–3600 秒，預設 30。支援行/條件中斷點、暫停、步進、堆疊、範圍、分頁變數、求值與賦值；恢復後參照失效。自動懸停只求值名稱路徑，其他運算式使用明確求值。program 執行時變更中斷點會於下次暫停同步。

變數子項每批最多顯示 100 項；宿主僅提供完整清單而不提供數量資訊時也能正確展開。繼續執行後，待展開的舊頁失效。

偵錯器標記為唯讀的變數不顯示修改入口；其他變數是否可修改仍取決於宿主能力與偵錯情境。

## 程式附加

attach/program 連線 TCP DAP 宿主，不是任意 PID 或執行中 VM 接管。先啟動 `veroscript --debug --dap-port=4711 script.vs`，設定地址與連接埠。入口留空繼承宿主；工作目錄、輸入與環境留空，後端與深度由宿主選擇。僅送出明確語意覆寫。非空引數取代，勾選空引數覆寫可清空；部分空語意清單仍繼承宿主，不能清空宿主預設值。

程式和執行域附加都會去除主機位址首尾空白，並拒絕內部空白或控制字元。

## 執行域附加

attach/domain 可填預期執行域 ID，不需本機 Runtime 或目前檔案。執行覆寫留空，環境 `{}`、入口 `inherit`、深度 0、後端 mixed，保留父環境繼承。宿主須宣告 `veroscriptSupportsDomainAttach`，否則明確失敗且不載入程式。同一連線涵蓋連續呼叫，暫停、步進、求值、條件與修改遵循宿主能力。主控台顯示身分與不透明版本，不自動比較原始碼或同步 LSP。實體原始碼路徑須能在本機存取，不提供路徑對應。配接器原始碼引用以唯讀快照顯示。連線預算 5 秒、初始化 10 秒，執行域附加使用啟動預算。中斷送出 `terminateDebuggee=false`，取消和程序由宿主管理；連線遺失結束工作階段，不自動重連。

## 排查

透過語言服務狀態、偵錯主控台與 IDE 記錄排查，使用同一 Runtime 的完整工具。接入目標為本機 Rider 專案與 Windows/Linux x64 Runtime；其他 IDE、Remote Development、Windows 直接啟動 WSL、macOS/ARM64、真實應用或 UE 執行域整合須另行驗收。

```json
{"importPaths":["src"],"defines":["FEATURE=1"],"bindingCatalogPaths":["bindings/catalog.json"]}
```

[LSP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/lsp.md)

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/debugging.md)


## 受管理記憶體

使用配套的目前 Runtime 工具時，`std::memory` 的補全、懸停與語意著色由 LSP 提供。暫停除錯後，在變數或監看中展開 buffer 或 view，可查看 `[byte_length]`、buffer 的 `[object_id]` 及可用位址。檢查項目唯讀，位址僅為暫時觀察值；外掛不提供任意記憶體讀寫。繼續執行後變數參照失效，需在新的暫停位置重新展開。

[std::memory](../../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)

## 編譯擴充與產生的原始碼

目前工具支援公共巨集（`#define_export`、`export import`）及原始碼編譯擴充（`import compiler "extension.vs";`）。請使用配套的 CLI/LSP/DAP 工具並保持匯入路徑一致。產生的方法與虛擬模組提供 DAP `sourceReference` 時，會以唯讀偵錯快照開啟。快照不是專案原始碼，不能 Run、Check、設定中斷點或同步至 LSP。讀取原始碼失敗會顯示錯誤，但不會結束暫停的偵錯工作階段。原生宿主的處理器登錄表與模組環境仍由宿主設定，外掛不將其作為額外的語意 JSON 欄位公開。

<!-- docs-navigation -->

---

[使用者文件](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
