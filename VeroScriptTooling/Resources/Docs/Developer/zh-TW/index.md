# VeroScript Tooling 技術文件

`VeroScriptTooling` 是 CLI、IDE、LSP、DAP host 共享的 public analysis、language-service、workspace 與 debugger integration layer。本文件是 developer contract，不是 editor 或 command-line usage instruction。

## 公開進入點

C++ query service 為 `VeroScriptAnalyzer`、`VeroScriptCallHelp`、`VeroScriptComplete`、`VeroScriptHover`、`VeroScriptNavigation`、`VeroScriptOutline`、`VeroScriptFindReferences`、`VeroScriptDocumentHighlight`、`VeroScriptRename`、`VeroScriptFormat`、`VeroScriptWorkspaceSymbols`。state/protocol entry point 為 `VeroScriptWorkspace`、`VeroScriptLspStdio`、`VeroScriptDebugger`；debug entry point 為 `VeroScriptDebugSession`、`VeroScriptDapAdapter`、`VeroScriptDapStdio`、`VeroScriptDapTcp`。共享型別由 `VeroScriptToolingTypes`、`VeroScriptDapTypes` 定義。

結果/選項公共型別包括 `FTextPosition`、`FTextRange`、`FDiagnostic`、`FTextEdit`、`FDocumentOverlay`、`FWorkspaceOptions`、`FAnalyzeOptions`、`FCompletionResult`、`FHoverResult`、`FReferencesResult`、`FRenameResult`、`FWorkspaceSymbolsResult`。exact field/default 以 public header 為準；本文說明它們跨 service 的契約。

## 分層

core 擁有 parsing、semantic analysis、module loading、diagnostic、runtime metadata、debugger semantics。 `VeroScriptTooling` 只 consume 這些 public contract，不得經 C++ embedding inspect private compiler/VM state。

`VeroScriptWorkspace` 負責 document overlay、project/module context、source/import resolution、preprocessor context、binding catalog、cross-file state。feature service query explicit source/file/workspace，不寫 user file，也不讀取 global IDE state。LSP/DAP adapter serialize structured result，擁有 framing/correlation/capability/cancellation/protocol error/event ordering；不得重實現 parser、resolver、formatter、bytecode evaluation、breakpoint semantics 或 debugger object lifetime。

## source coordinate 與 edit

C++ query API 的 position/range 是 one-based source coordinate；LSP adapter 與 zero-based UTF-16 protocol position 轉換。host 不得獨立進行第二次 conversion。result/edit 繫結 document identity/source state；host 僅在 version/overlay 仍匹配時 apply `FTextEdit`，tooling layer 永不自行 apply edit。

## localization 與 diagnostic

diagnostic 是含 stable identifier、source location、severity、substitution argument 的 structured record。shared catalog localize display message 並在 locale/message 缺失時 fallback English。diagnostic identifier/argument、protocol method、JSON field、language tag、path、URI、symbol name、source/code fragment 永不 translate。

LSP `initialize.locale` 是 BCP 47 preference；不支援時 process 保留 environment-selected catalog，locale-aware standard-namespace description 也按此 fallback。binding catalog identifier/type/member/signature/documentation key 是 exact identifier，不是 translated resource。

## 詳細頁面

- [Tooling 架構與協議](architecture-and-protocols.md)
- [Workspace、LSP 與 DAP](workspace-and-debug.md)
- [Tooling 測試與擴充套件規則](testing-and-extension.md)

## Debug session 與 DAP surface

`VeroScriptDebugSession` 是 protocol-neutral synchronous debugger。 `LoadSource`、`LoadFile` 接收含 import path、define、source overlay、optional `IHostBindingCatalog`、implicit import、circular-import/project-wide-access policy 的 `FDebugLoadOptions`。breakpoint 使用 file label、one-based line、optional condition。session 暴露 run mode、requested execution backend、max call depth、JIT policy、最後 `FJitStats`；stopped host 可 inspect `FDebugStackFrame`、`FDebugVariable`、evaluate、write local/child。 `FDebugRunResult` 分離 stdout-like output、error output、structured diagnostic，success 與 script exit code independent。

`VeroScriptDapAdapter` 是不擁有 JSON/framing/transport 的 in-process DAP-shaped adapter。lifecycle 為 `Initialize` -> `Launch` 或 `Attach` -> `SetBreakpoints` -> `ConfigurationDone`；後者 synchronous 並 emit stopped/output/exited/terminated。continue/step/stack/scope/variable/evaluate/set-variable 僅在 stopped-event callback 中 valid。 `Attach` 使用 `Launch` load semantics，不是 mid-run VM attach，並強制 effective backend 為 `Vm`。live binding 用 `FDapLaunchArgs::Bindings`；external process 採用 catalog data。DAP capability/event name 精確來自 `FDapCapabilities`、`EDapEventKind`，不 localize。

## 事實來源

變更時同步更新 `Public/`、`Private/`、focused feature/LSP/DAP test 與本文件。每個 public contract 都複查 result shape、coordinate、document version、workspace invalidation、protocol capability、diagnostic/localization/cancellation/debugger integration。

<!-- docs-navigation -->

---

[開發文件](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-TW/index.md)

語言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
