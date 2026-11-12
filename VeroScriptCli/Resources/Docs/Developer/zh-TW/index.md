# VeroScript CLI 技術文件

`VeroScriptCli` 是輕量的 process layer。它提供 `veroscript`、`veroscript-lsp`、`veroscript-dap` entry point，而不重新實作 compiler、VM、C++ embedding API 或 language-service semantics。

## process 職責

CLI 擁有 argument parsing、command dispatch、process-level stream、REPL/watch orchestration、source/file selection、environment/SDK asset lookup、exit-code policy 與 LSP/DAP process main。core、C++ session、AOT、Tooling module 仍是 language behavior 的 source of truth。

command option、subcommand name、environment variable、exit code、diagnostic ID、output format 都是 compatibility surface。不得只為改善 prose 而翻譯或變更它們。

## 執行階段資源

standalone process 必須定位 version-matched standard library 與 localization catalog。asset resolution 必須可 relocatable，且不得悄悄使用過期的 build-directory copy。asset 缺少或不相容屬於 startup/configuration failure，不是 empty-language fallback。

## 通訊協定進入點

LSP 與 DAP main 將 protocol behavior delegate 給 `VeroScriptTooling`。CLI 必須保持這些 service 預期的 stream framing、request ordering、cancellation、stderr/stdout separation 與 process exit behavior。

## command 與 asset 細節

ordinary main dispatch compile/check/run 與 package-oriented operation；REPL/watch process 增加 interactive source/file-change orchestration。`VeroScriptLspMain`、`VeroScriptDapMain` 是 protocol-only process entry point，並把 language behavior delegate 給 `VeroScriptTooling`。CLI 不得建立第二套 Session、debugger 或 workspace state implementation。

CLI path resolver 必須保留 source root、SDK root、`bin` directory、standard-library directory、localization directory 的區分。發現的 executable 與 asset 必須來自 compatible installation；environment-variable/`PATH` fallback 是 discovery behavior，不是允許混用 version。

## 詳細頁面

- [CLI process 與 asset 契約](process-and-assets.md)

## 規格依據

以 `Private/`、CLI CMake target、process test、Tooling public API、packaging/SDK smoke test 為準。command dispatch、asset lookup、stream behavior、environment variable、exit code、LSP/DAP process boundary 變更時，需同步更新本文檔。

## 專案初始化

`Private/VeroScriptCliProject.cpp` 以本機 C++17 檔案建立實作 `project init`，不載入套件環境或呼叫編譯。
獨佔建立保證並行呼叫也不覆寫既有檔案；原生 CLI 登錄表保留 `project` 命令名稱。
`VeroScriptProjectInitCliTests` 程序測試涵蓋路徑、重複執行、衝突、並行建立及寫入失敗復原，
僅於原生建置時註冊；包含建置服務時，
也會建置並執行產生的應用程式和文件專案範例。專案與建置範本須符合現有 `0.1.1` Schema；
初始化不建構相依鎖定檔或安裝狀態。
UTF-8 路徑轉換相容 C++17 與 C++20；封裝工具的 CMake 回歸會在兩種標準下獨立編譯初始化器，並將警告視為錯誤。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
