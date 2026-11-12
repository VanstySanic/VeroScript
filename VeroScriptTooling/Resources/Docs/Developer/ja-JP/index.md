# VeroScript Tooling 技術文書

`VeroScriptTooling` は CLI、IDE、LSP、DAP host が共有する public analysis/language-service/workspace/debugger integration layer です。editor/CLI usage でなく developer contract を記録します。

## 公開エントリーポイント

C++ query service は `VeroScriptAnalyzer`、`VeroScriptCallHelp`、`VeroScriptComplete`、`VeroScriptHover`、`VeroScriptNavigation`、`VeroScriptOutline`、`VeroScriptFindReferences`、`VeroScriptDocumentHighlight`、`VeroScriptRename`、`VeroScriptFormat`、`VeroScriptWorkspaceSymbols`。state/protocol entry point は `VeroScriptWorkspace`、`VeroScriptLspStdio`、`VeroScriptDebugger`、debug entry point は `VeroScriptDebugSession`、`VeroScriptDapAdapter`、`VeroScriptDapStdio`、`VeroScriptDapTcp`。 `VeroScriptToolingTypes` と `VeroScriptDapTypes` が shared data を定義します。

public result/option type は `FTextPosition`、`FTextRange`、`FDiagnostic`、`FTextEdit`、`FDocumentOverlay`、`FWorkspaceOptions`、`FAnalyzeOptions`、`FCompletionResult`、`FHoverResult`、`FReferencesResult`、`FRenameResult`、`FWorkspaceSymbolsResult`。exact field/default は public header、ここは cross-service contract を説明します。

## レイヤー構成

core は parsing、semantic analysis、module loading、diagnostic、runtime metadata、debugger semantics を所有します。`VeroScriptTooling` は public contract だけを consume し private compiler/VM state を inspect しません。

`VeroScriptWorkspace` は document overlay、project/module context、source/import resolution、preprocessor context、binding catalog、cross-file state を所有します。feature service は explicit source/file/workspace を query し global IDE state を read せず user file を write しません。LSP/DAP adapter は structured result を serialize し framing/correlation/capability/cancellation/protocol error/event ordering を所有し、semantic engine を重複実装しません。

## source coordinate と edit

C++ query position は one-based source coordinate、LSP は zero-based UTF-16 に変換します。host は second independent conversion をしません。result/edit は document identity/source state に tied し、host は version/overlay が一致する時だけ `FTextEdit` を apply します。tooling は edit を apply しません。

## localization と diagnostic

diagnostic は stable ID/source location/severity/substitution argument の structured record。catalog は display message を localize し English fallback。diagnostic ID/argument、protocol method、JSON field、language tag、path、URI、symbol name、source/code fragment は translate しません。

LSP `initialize.locale` は BCP 47 preference です。unsupported 時は environment-selected catalog を保持し standard-namespace description も fallback。binding catalog identifier/type/member/signature/documentation key は exact identifier で localizable resource ではありません。

## 詳細ページ

- [Tooling architecture と protocol](architecture-and-protocols.md)
- [Workspace、LSP、DAP](workspace-and-debug.md)
- [Tooling test と extension rule](testing-and-extension.md)

## Debug session と DAP surface

`VeroScriptDebugSession` は protocol-neutral synchronous debugger。 `LoadSource`/`LoadFile` は import path、define、overlay、optional `IHostBindingCatalog`、implicit import/policy を持つ `FDebugLoadOptions` を受けます。breakpoint は file label/one-based line/optional condition。session は run mode/backend/call depth/JIT policy/`FJitStats` を公開し、stopped host は `FDebugStackFrame`/`FDebugVariable` inspect、evaluate、local/child write をします。`FDebugRunResult` は output/error/diagnostic を分離し success/exit code は independent。

`VeroScriptDapAdapter` は JSON/framing/transport を所有しない in-process DAP-shaped adapter。 `Initialize` -> `Launch` または `Attach` -> `SetBreakpoints` -> `ConfigurationDone` は synchronous stopped/output/exited/terminated を emit。stop callback だけで continue/step/stack/scope/variable/evaluate/set-variable が valid。 `Attach` は `Launch` load semantics で mid-run attach ではなく effective `Vm` を force。 `FDapLaunchArgs::Bindings` は live binding、external process は catalog data。`FDapCapabilities`/`EDapEventKind` の name は exact です。

## 仕様の基準となるソース

`Public/`、`Private/`、focused feature/LSP/DAP test、page を同時 update します。public contract ごとに result shape、coordinate、version、workspace invalidation、capability、diagnostic/localization/cancellation/debugger を review します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
