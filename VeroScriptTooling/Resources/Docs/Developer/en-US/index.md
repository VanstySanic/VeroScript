# VeroScript Tooling technical documentation

`VeroScriptTooling` is the public analysis, language-service, workspace, and
debugger integration layer shared by the CLI, IDE, LSP, and DAP hosts. These
pages describe developer contracts and implementation boundaries. They are not
editor or command-line usage instructions.

## Public entry points

The public C++ query services are `VeroScriptAnalyzer`, `VeroScriptCallHelp`,
`VeroScriptComplete`, `VeroScriptHover`, `VeroScriptNavigation`,
`VeroScriptOutline`, `VeroScriptFindReferences`, `VeroScriptDocumentHighlight`,
`VeroScriptRename`, `VeroScriptFormat`, and `VeroScriptWorkspaceSymbols`.

State and protocol entry points are `VeroScriptWorkspace`,
`VeroScriptLspStdio`, and `VeroScriptDebugger`. Debug integration is exposed by
`VeroScriptDebugSession`, `VeroScriptDapAdapter`, `VeroScriptDapStdio`, and
`VeroScriptDapTcp`. `VeroScriptToolingTypes` and `VeroScriptDapTypes` define the
shared result, option, protocol, and debugger data structures.

Important public result and option types include `FTextPosition`, `FTextRange`,
`FDiagnostic`, `FTextEdit`, `FDocumentOverlay`, `FWorkspaceOptions`,
`FAnalyzeOptions`, `FCompletionResult`, `FHoverResult`, `FReferencesResult`,
`FRenameResult`, and `FWorkspaceSymbolsResult`. The exact fields and default
values are defined by the public headers; this documentation explains the
cross-service contracts around them.

## Layering

The language core owns parsing, semantic analysis, module loading, diagnostics,
runtime metadata, and debugger semantics. `VeroScriptTooling` consumes those
public contracts and must not inspect private compiler or VM state through the
C++ embedding layer.

`VeroScriptWorkspace` owns document overlays, project and module context,
source and import resolution, preprocessor context, binding catalogs, and
cross-file state. Feature services query an explicit source, file, or workspace
state. They do not reach into global IDE state and do not write user files.

LSP and DAP adapters serialize structured tooling results. They own framing,
request correlation, capability advertisement, cancellation, protocol errors,
and event ordering; they must not reimplement parsing, resolution, formatting,
bytecode evaluation, breakpoint semantics, or debugger object lifetime.

## Source coordinates and edits

The C++ query API uses the public text-position and range types. Query positions
are one-based source coordinates. The LSP adapter converts them to and from the
protocol's zero-based UTF-16 positions. Hosts must not perform a second,
independent coordinate conversion.

Results and edits are tied to the document identity and source state used to
produce them. A host applies `FTextEdit` values only after checking that the
document is still the same version or overlay. The tooling layer never applies
edits itself.

## Localization and diagnostics

Diagnostics are structured records with stable identifiers, source locations,
severity, and substitution arguments. The shared catalog system localizes the
display message and falls back to English when a locale or message is
unavailable. Diagnostic identifiers, arguments, protocol methods, JSON fields,
language tags, paths, URI values, symbol names, and source/code fragments are
technical data and must not be translated.

The LSP `initialize.locale` value is a BCP 47 preference. The process preserves
the environment-selected catalog when that preference is unsupported. Locale
aware standard-namespace descriptions follow the same fallback rule. Binding
catalog identifiers, types, members, signatures, and documentation keys remain
exact identifiers; binding catalogs are not translated resources.

## Detailed pages

- [Tooling architecture and protocols](architecture-and-protocols.md)
- [Workspace, LSP, and DAP](workspace-and-debug.md)
- [Tooling testing and extension rules](testing-and-extension.md)

## Debug session and DAP surface

`VeroScriptDebugSession` is the protocol-neutral synchronous debugger surface.
`LoadSource` and `LoadFile` accept `FDebugLoadOptions`, including import paths,
defines, source overlays, an optional `IHostBindingCatalog`, implicit imports,
and the circular-import and project-wide-access policies. Breakpoints are
identified by file label and one-based line, with an optional condition.

The session exposes run modes, requested execution backend, maximum call depth,
JIT policy and the last `FJitStats` snapshot. While stopped, hosts can inspect
`FDebugStackFrame` and `FDebugVariable` values, evaluate expressions, and write
locals or children. `FDebugRunResult` keeps stdout-like output and error output
separate from structured diagnostics; success and the script exit code are
independent.

`VeroScriptDapAdapter` is an in-process DAP-shaped adapter. It does not own JSON
encoding, message framing, or a transport. The lifecycle is
`Initialize` -> `Launch` or `Attach` -> `SetBreakpoints` ->
`ConfigurationDone`. The latter runs synchronously and emits the stopped,
output, exited, and terminated event sequence through the event handler.
Continue, stepping, stack, scope, variable, evaluate, and set-variable calls
are valid from a stopped-event callback. `Attach` uses the same load semantics
as `Launch`; it is not a mid-run VM attach, and an attached debugger forces the
effective backend to `Vm`.

Live in-process bindings are supplied by `FDapLaunchArgs::Bindings`. A process
outside the host uses catalog data instead of borrowing that pointer. DAP
capabilities and event names are copied exactly from `FDapCapabilities` and
`EDapEventKind`; they are not localized.

## Source of truth

When behavior changes, update the relevant public header in `Public/`,
the corresponding `Private/` implementation, focused feature/LSP/DAP
tests, and these technical pages together. Review result shapes, source
coordinates, document versions, workspace invalidation, protocol capabilities,
diagnostics, localization, cancellation, and debugger integration for every
public contract change.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
