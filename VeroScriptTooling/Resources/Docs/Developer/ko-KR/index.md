# VeroScript Tooling 기술 문서

`VeroScriptTooling`는 CLI, IDE, LSP, DAP host가 공유하는 public analysis/language-service/workspace/debugger integration layer입니다. editor/CLI usage가 아니라 developer contract를 기록합니다.

## 공개 진입점

C++ query service는 `VeroScriptAnalyzer`, `VeroScriptCallHelp`, `VeroScriptComplete`, `VeroScriptHover`, `VeroScriptNavigation`, `VeroScriptOutline`, `VeroScriptFindReferences`, `VeroScriptDocumentHighlight`, `VeroScriptRename`, `VeroScriptFormat`, `VeroScriptWorkspaceSymbols`입니다. state/protocol entry point는 `VeroScriptWorkspace`, `VeroScriptLspStdio`, `VeroScriptDebugger`, debug entry point는 `VeroScriptDebugSession`, `VeroScriptDapAdapter`, `VeroScriptDapStdio`, `VeroScriptDapTcp`입니다. `VeroScriptToolingTypes`와 `VeroScriptDapTypes`가 shared data를 정의합니다.

public result/option type은 `FTextPosition`, `FTextRange`, `FDiagnostic`, `FTextEdit`, `FDocumentOverlay`, `FWorkspaceOptions`, `FAnalyzeOptions`, `FCompletionResult`, `FHoverResult`, `FReferencesResult`, `FRenameResult`, `FWorkspaceSymbolsResult`입니다. exact field/default는 public header가 authority이고 이 문서는 cross-service contract를 설명합니다.

## 계층 구조

core는 parsing, semantic analysis, module loading, diagnostic, runtime metadata, debugger semantics를 소유합니다. `VeroScriptTooling`는 public contract만 consume하고 private compiler/VM state를 inspect하면 안 됩니다.

`VeroScriptWorkspace`는 document overlay, project/module context, source/import resolution, preprocessor context, binding catalog, cross-file state를 소유합니다. feature service는 explicit source/file/workspace를 query하고 global IDE state를 read하거나 user file을 write하지 않습니다. LSP/DAP adapter는 structured result를 serialize하고 framing/correlation/capability/cancellation/protocol error/event ordering을 소유하며 semantic engine을 재구현하지 않습니다.

## source coordinate와 edit

C++ query position은 one-based source coordinate이고 LSP는 zero-based UTF-16으로 convert합니다. host는 second independent conversion을 해서는 안 됩니다. result/edit는 document identity/source state에 tied하며 host는 version/overlay가 여전히 맞을 때 `FTextEdit`를 apply합니다. tooling은 edit를 apply하지 않습니다.

## localization과 diagnostic

diagnostic은 stable ID/source location/severity/substitution argument의 structured record입니다. catalog는 display message를 localize하고 English fallback합니다. diagnostic ID/argument, protocol method, JSON field, language tag, path, URI, symbol name, source/code fragment는 translate하지 않습니다.

LSP `initialize.locale`은 BCP 47 preference입니다. unsupported이면 environment-selected catalog를 유지하고 standard-namespace description도 fallback합니다. binding catalog identifier/type/member/signature/documentation key는 exact identifier이며 translated resource가 아닙니다.

## 상세 페이지

- [Tooling architecture와 protocol](architecture-and-protocols.md)
- [Workspace, LSP, DAP](workspace-and-debug.md)
- [Tooling test와 extension rule](testing-and-extension.md)

## Debug session과 DAP surface

`VeroScriptDebugSession`은 protocol-neutral synchronous debugger입니다. `LoadSource`/`LoadFile`은 import path, define, overlay, optional `IHostBindingCatalog`, implicit import/policy가 든 `FDebugLoadOptions`를 받습니다. breakpoint는 file label/one-based line/optional condition입니다. session은 run mode/backend/call depth/JIT policy/`FJitStats`를 노출하고 stopped host는 `FDebugStackFrame`/`FDebugVariable` inspect, evaluate, local/child write를 합니다. `FDebugRunResult`는 output/error/diagnostic을 분리하며 success/exit code는 independent입니다.

`VeroScriptDapAdapter`는 JSON/framing/transport를 소유하지 않는 in-process DAP-shaped adapter입니다. `Initialize` -> `Launch` 또는 `Attach` -> `SetBreakpoints` -> `ConfigurationDone`은 synchronous stopped/output/exited/terminated를 emit합니다. stop callback에서만 continue/step/stack/scope/variable/evaluate/set-variable가 valid합니다. `Attach`는 `Launch` load semantics이며 mid-run attach가 아니고 effective `Vm`을 force합니다. `FDapLaunchArgs::Bindings`는 live binding이며 external process는 catalog data를 사용합니다. `FDapCapabilities`/`EDapEventKind` name은 exact입니다.

## 명세의 기준 소스

`Public/`, `Private/`, focused feature/LSP/DAP test, page를 함께 update합니다. public contract마다 result shape, coordinate, version, workspace invalidation, capability, diagnostic/localization/cancellation/debugger를 review합니다.

<!-- docs-navigation -->

---

[개발자 문서](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ko-KR/index.md)

언어: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
