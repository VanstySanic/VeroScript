# CLI process 與 asset 契約

## 進入點

`VeroScriptCli` 是供 `veroscript`、`veroscript-lsp`、`veroscript-dap` 使用的 thin process layer。ordinary main dispatch compile/check/run 與 package operation。REPL/watch code 增加 interactive source/file-change orchestration。LSP/DAP main 將 language behavior delegate 給 Tooling。

CLI 不得重複實作 compiler、VM、debugger 或 workspace semantics。subcommand name、option、environment variable、exit code、diagnostic ID、output format 是 compatibility token，不應 localize。

## 資源解析

resolution 必須區分 source root、SDK root、`bin` directory、standard-library directory、localization directory。executable 及其 runtime asset 必須來自 compatible installation。environment-variable/`PATH` fallback 是 discovery behavior，並非允許混用 installation。

## 處理程序行為

對 IDE、SDK、shell consumer 而言，stdout、stderr、diagnostic line handling、input stream、process cancellation、exit-code propagation 必須 stable。LSP/DAP framing 屬於 Tooling transport implementation；CLI 僅擁有 process startup 與 termination。

## Unicode 行程邊界

Windows `wmain` 入口在呼叫 `VeroScript::Cli::Run` 前，將 UTF-16 參數一次轉換為 UTF-8。持有參數的字串及參數指標陣列必須在整個呼叫期間保持有效；檔案操作使用 UTF-8 路徑輔助函式。`VeroScriptUnicodeCliTests` 涵蓋 VM、mixed 和 JIT 優先模式下的非 ASCII 原始碼與匯入路徑、指令碼參數及套件執行。終端顯示和管道解碼仍由使用端負責。

## 登錄與分派契約

Core 提供 `VeroScriptCommandLine.h`，CLI 擁有登錄表、引數剖析及分派。在原生 `Create` 內取得 `INativeExtensionHost::GetService("veroscript.cli", "0.1.1", error)`，使用相符的 `ICommandLineExtensionService` 同步登錄自有 `FCommandLineModule` 描述元。分派前關閉登錄；錯誤宣告使整個批次失敗。組合角色模組應相容於非 CLI 宿主不提供此服務的情況。 每個模組須至少宣告一個命令或選項；宣告選項時必須提供 Configure 回呼。

ID 與名稱區分大小寫，使用 ASCII 字母/數字/底線/連字號，最長 64 字元，首尾不得為連字號；Core 名稱保留。最多 256 個模組，每模組命令與選項合計最多 256 項；描述/用法/值標籤不含控制字元，最多 8192 位元組。選項預設要求值且不允許重複；已指定旗標提供一個空字串，未指定選項沒有對應項。值類似其他選項時使用 `--name=value`。

Configure 按模組 ID 順序在一般執行/命令分派前呼叫一次，僅驗證及儲存自身選項，不得執行腳本。說明跳過設定及命令執行，但仍載入並登錄模組。Run 接收完整 UTF-8 引數、語言標籤、資料流及選定環境。借用內容只在分派期間有效，不得強參考反向擁有登錄的環境；原生 Destroy 傳回前應結束所屬工作執行緒。宿主持有程式碼、攔截回呼例外並檢查結束碼 0..255；阻塞的原生命令自行處理取消。

[NativeModule 原始碼範例](../../../Examples/NativeModule/README.md) 示範相符 SDK 建置與套件整合。`VeroScriptCliModulesTests`、`VeroScriptCliRegistryTests` 涵蓋剖析、名稱衝突、Unicode、生命週期、啟用/停用與引數邊界。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/process-and-assets.md) · [fr-FR](../fr-FR/process-and-assets.md) · [ja-JP](../ja-JP/process-and-assets.md) · [ko-KR](../ko-KR/process-and-assets.md) · [zh-CN](../zh-CN/process-and-assets.md) · [zh-TW](process-and-assets.md)
