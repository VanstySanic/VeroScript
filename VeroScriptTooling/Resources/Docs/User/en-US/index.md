# Tooling module

`VeroScriptTooling` is the public analysis layer around the VeroScript language
core. It is useful to editor, language-server, static-checking, and debugger
integrations. It does not replace the runtime embedding API.

## Query surface

The public C++ surface is organized around these operations:

| Surface | Purpose |
| --- | --- |
| Analyzer | Parse and analyze source or files and return structured diagnostics. |
| Formatter | Produce a complete formatted source replacement. Formatting is lexical and does not expand macros. |
| Completion and call help | Contextual completion, grouped overload candidates, signature help, generated inherited/interface method implementations, host metadata, soft builtins, and preprocessor entries. |
| Hover and navigation | Resolved declarations, documentation comments, definitions, and namespace-aware ranges. |
| Outline and semantic tokens | Document symbols and declaration-based highlighting, including namespace, macro, annotation, and host symbols. |
| References and highlights | Cross-file references, read/write information, and document highlights. Inactive preprocessor branches are excluded. |
| Rename | Validation, exact prepare range, and workspace text edits. User namespaces are supported; standard-library namespaces are read-only. |
| Workspace | Open-buffer overlays, multi-document analysis, import resolution, and workspace-symbol search. |
| LSP transport | `VeroScriptLspStdio::Run` provides JSON-RPC over stdio, including completion resolve, type navigation, selection ranges, document links, semantic-token deltas, range formatting, formatting code actions, cancellation, and workspace invalidation. |
| DAP transport | The debug-session and adapter surfaces provide editor debugging through DAP. |

Generated method completion items set `FCompletionItem::bOverrideMember`;
`bImplementsInterfaceMember` is true when a generated method satisfies a pure
interface requirement; overrides for virtual default methods are classified as
inherited overrides. `Documentation` uses the active tooling locale. `InsertText`
contains the generated method skeleton, and `bInsertTextSnippet` indicates its
snippet placeholders. Overload candidates sharing a name appear as one item with
each signature on a separate detail line. The LSP sends generated candidates as
explicit text edits and uses plain method text when the client does not support
snippets.

All source positions returned by the C++ query API are one-based. Open document
overlays have priority over disk files, so a workspace query sees unsaved
imports. `FWorkspaceOptions` carries import paths, standard-library search roots,
defines, predefined macros, implicit imports, project-wide access, and optional
host-binding metadata.

`FWorkspaceSymbolsOptions.WorkspaceRoots` supplies roots for closed-file symbol
indexing and is deliberately separate from `ImportPaths`: indexing a project
does not make every indexed directory an implicit import path. The
`MaxWorkspaceFiles` and `MaxWorkspaceFileBytes` limits protect scans of large
or untrusted workspaces. `ShouldCancel` is checked cooperatively during a
long scan; a cancelled result sets `FWorkspaceSymbolsResult::bCancelled` and
must not be published or cached as a fresh index.
Workspace symbol collection removes duplicate declaration identities, so the
same declaration observed through multiple imports is returned once.

`VeroScriptWorkspace::ImplementationDocument` finds derived types, matching
interface or virtual-member overrides, and separately defined or annotation-generated
function/method bodies. Declaration navigation still targets the declaration.
`SourceImplementation` owns the body filename and stores its physical position;
an empty filename means that no separate body was bound. Direct implementation
lookup for free functions and non-virtual methods does not scan the workspace.

Native embedding hosts can supply `AnnotationProcessing` in workspace/query
options, `FLspStdioRunOptions` and `FDapStdioRunOptions`/`FDapLaunchArgs`. Use the
same frozen processor registry as compilation. LSP factories and dependency
readers must be safe for concurrent protocol/analysis workers. Recorded dependency
contents and existence are checked before semantic cache reuse; results from
processors with `Cacheable=false` are returned normally but are not cached.

Generated declarations retain their virtual identity for queries and rename is
rejected. `GeneratedOrigin` identifies the physical generating annotation;
standard LSP definition/implementation and workspace-symbol locations use this
origin. Generated members remain available to completion and signature help but
are omitted from a physical document's outline. Navigation does not rerun a
processor to obtain the origin.

Program debugging captures generated code during load. Generated stack sources
carry a `sourceReference`; DAP `source` returns the immutable captured text.
`TryGetGeneratedSource` provides the same read-only text to native debug hosts.
An online-domain host can implement the optional domain method using its prepared
program's generated documents, following the domain's stopped-query threading rules.

`FormatRangeDocument` formats a
selection through complete physical lines and returns the replacement range;
it does not rewrite only a partial token or line fragment.
The overload that accepts `FWorkspaceSymbolsOptions` applies the same workspace
limits and cancellation probe to implementation lookup; when it is cancelled,
`FNavigationResult::bCancelled` is set and no partial definitions should be
published.
`FormatDocument` and `FormatRangeDocument` have overloads that accept an
indentation width and a spaces-or-tabs preference. A width of zero keeps the
workspace default of four spaces; `bInsertSpaces=false` emits one tab per level.
The stateless formatter accepts the same style option.
The stateless `VeroScriptFormat::FormatRangeSource` entry point provides the same
whole-physical-line range contract without an open workspace.

The formatter uses Allman braces for block bodies. `else`, `else if`, `catch`, `finally`,
and the trailing `while` of `do…while` start on new lines aligned with the preceding closing brace.
Expression initializers retain their syntax; literal contents and preprocessor directives are preserved without expansion.

Range formatting returns no edits when a boundary falls inside a block comment, raw string, or multiline
preprocessor directive. Select the complete directive or following ordinary code to continue formatting;
macro text and line continuations remain unchanged.

Tooling resolves identifiers from the parsed program and host catalog; it does
not infer semantic kinds from capitalization. The reserved `std` tree and user
namespaces are first-class namespace symbols. This keeps completion, hover,
navigation, references, rename, outline, and semantic tokens consistent while a
qualified expression is being edited.

Completion items may use the LSP snippet grammar. Callable items can insert
parameter placeholders, and generic type items can insert type-argument
placeholders. The LSP adapter sends snippet-formatted insertion text only when
the client advertises `textDocument.completion.completionItem.snippetSupport`;
otherwise it sends plain text. The result also carries stable sort priorities
and a replacement range so an editor can apply the item without duplicating
already typed scope or generic separators.

Namespace-segment items differ from member items: at expression/statement
sites, they append `::` to `InsertText` when no separator follows the replace
range. If `::` already follows, or the completion targets `using namespace`,
only the leaf is inserted.

For a member of a qualified namespace, `FCompletionItem::Label` and
`InsertText` keep the leaf name and any existing snippet, while
`LabelDescription` carries the qualified namespace separately. In LSP this is
standard `labelDetails.description`; snippet text is sent only to clients that
advertise `textDocument.completion.completionItem.snippetSupport`, with plain
text used otherwise. The server can additionally send
`$valencePresentation` for Valence's rendered label/declaration and UTF-16
semantic spans; other LSP clients can ignore that extension. These presentation
fields never change filtering, sorting, or the inserted text.

Completion ranks textual match quality before contextual priority: exact,
case-insensitive exact, prefix, word-boundary or camel-case, contained, then
subsequence matches. `FCompletionItem::MatchQuality` is the primary key and
`SortPriority` breaks ties, so integrations should preserve the result order;
the LSP `sortText` carries the same complete ordering. The LSP marks the first
item `preselect` only when its exact/case-insensitive-exact/prefix tier is
strictly better than every other returned item. Primitive and container type
words are built-in types: primitive spellings remain `keyword` entries in
completion, hover, and semantic tokens, whereas container spellings are
`type`/type-alias entries. Both have no source definition and rename is refused.
Their descriptive hover text follows the active tooling locale; type identifiers
and protocol fields remain unchanged.

The LSP adapter also exposes completion-item resolve, type-definition and
implementation navigation, token/line/document selection ranges, import and
include document links, full and minimal-delta semantic tokens, whole-document,
line-expanded range, and on-type formatting, plus `source.formatDocument`, safe
duplicate-import `source.organizeImports`, and generated-method
`refactor.rewrite` code actions. It accepts
cancellation and workspace-folder or watched-file invalidation notifications;
closed-file workspace-symbol indexing is cached between those changes.
`VeroScriptLspStdio::Run` accepts `FLspStdioRunOptions`, including
`DefaultWorkspaceRoots` for closed-file indexing when the client does not provide
`workspaceFolders` or `rootUri`; client-provided roots replace that default.

The analyzer and all queries accept preprocessor definitions and source
overlays. Binding catalogs add host functions, types, members, annotations,
generic templates, and documentation to the same query model. A catalog is
metadata only; the host still has to register the matching runtime behavior.

For the wire-level lifecycle, capabilities, settings, cancellation, and exact
LSP methods, see [Language Server Protocol](lsp.md). For the separate debugger
transport, see [Debugging](debugging.md).

## Debug Adapter Protocol

The `veroscript-dap` process supports DAP over standard input/output or TCP.
The CLI form is:

```powershell
veroscript --debug hello.vs
veroscript --debug --dap-port=4711 hello.vs
```

`--dap-port` requires `--debug` and a port from 1 to 65535. TCP binds to `127.0.0.1` by default; `--dap-bind=<address>` changes the bind address and requires TCP mode. DAP `launch` or `attach` supplies the program and arguments. Debugging uses the VM, so its performance does not represent mixed or JIT execution.

The adapter implements source line breakpoints through `setBreakpoints`.
Exception breakpoints and function breakpoints are not implemented. An empty
`setExceptionBreakpoints` request succeeds only as a clear operation for the
standard client launch handshake; non-empty exception filters and all
`setFunctionBreakpoints` requests return a failed DAP response. A client must
not display those breakpoint classes as enabled.

A source breakpoint set before `launch` or `attach` is pending and unverified.
After code loads, the adapter sends the standard `breakpoint` event with
`reason: "changed"` and the same DAP ID. It verifies only a line that belongs
to exactly one compiled source, remains active after preprocessing, and has an
executable location; it never falls back to a matching basename. Clients must
use the returned physical line, which may move forward within a function to the
next executable line. See [Debugging](debugging.md) for the full binding rules.

For in-process integrations, `VeroScriptDapAdapter` follows the lifecycle
`Initialize` → `Launch` or `Attach` → `SetBreakpoints` → `ConfigurationDone`.
The in-process `ConfigurationDone` call runs synchronously; wire hosts must call
it on an execution worker so the protocol thread can continue to process
`pause`, `terminate`, and stopped-state requests. `Attach` loads a program with
the same loader as `Launch`; it does not attach to a VM that is already running.
`Pause` and `Terminate` are thread-safe cooperative controls, and
`IsStopped()`/`IsRunning()` expose the current execution state.

`VeroScriptDapStdio::Run` accepts `FDapStdioRunOptions` for defaults such as
`DefaultProgram`, `DefaultScriptArgs`, `DefaultStdlibSearchPaths`,
`DefaultEnvironment`, `DefaultWorkingDirectory`, and `DefaultInput`. An embed
host can set `LiveBindings` to use its registered host functions; the non-owned
catalog must remain valid for the whole DAP run. Without live bindings,
`bindingCatalogPaths` supplies metadata/stub bindings for analysis and launch.

`VeroScriptDapTcp::Run` accepts `FDapTcpRunOptions`. `MaxSessions=0` keeps
accepting clients until the host stops, `Port=0` selects an ephemeral port and
reports it through `OnListening`, and `BindAddress` should normally be
`127.0.0.1`. `bAnnounceListen=false` disables the default readiness message on
the error stream. The stdio host returns `0` for a clean session, `1` for a
protocol or I/O failure, and `2` when the client requests host termination.

### Protocol-agnostic debug sessions

`VeroScriptDebugSession` is the reusable C++ layer below the DAP adapter. Use
it when a host or a custom adapter needs loading, breakpoints, synchronous stop
callbacks, inspection, and run results without implementing the DAP wire
protocol. `LoadSource` and `LoadFile` accept the same import paths, defines,
predefined macros, source overlays, optional binding catalog, implicit import
paths, import policies, and standard-library roots used by the workspace
layer.

`BindBreakpoint` is the strict source-breakpoint path for an adapter. It
resolves a one-based physical line against exactly one compiled source and
returns `FBreakpointBinding`: a nonzero `Id` and the actual physical `Line`
when bound, or a localized `Message` when it cannot bind. A line inside a
function can move forward to the next executable line; callers must use the
returned line rather than assume the requested one.

Configure per-run arguments, environment, working directory, input, maximum
call depth, the requested backend, or the JIT policy before `Run`. The returned
`FDebugRunResult` contains `bSuccess`, structured `Diagnostics`, separate
`Output` and `ErrorOutput`, and `ExitCode`. The debugger still forces the
effective backend to `Vm`; the requested backend and JIT policy are retained
as configuration, and JIT statistics mainly record the resulting invalidation.

`SetStopHandler` runs synchronously while execution is paused. The handler can
use `GetStackTrace`, `GetLocals`, `GetChildren`, `Evaluate`, `SetLocal`, and
`SetChild`, then select `Continue`, `StepIn`, `StepOver`, or `StepOut` with
`SetRunMode`. `RequestPause()` and `RequestCancel()` are thread-safe
cooperative controls for an asynchronous host. Ordinary session configuration
and `Run` calls must remain on the controlling thread; keep the session alive
until a requested operation returns.

## Public library boundary and localization

Use the public tooling headers distributed with the SDK. Implementation-only
files are not part of the supported integration surface. The tooling module
uses the same localization catalogs as the CLI and runtime. Diagnostic codes
and message arguments remain stable across display languages; selecting a
language changes presentation, not script semantics. See [Diagnostic
localization](../../../../../VeroScript/Resources/Docs/User/en-US/localization.md).

## Current syntax-aware editor coverage

Completion, outline, hover, signature help, and semantic tokens recognize same-type declaration
groups, `do`/`while`, classic `for` clauses with expression lists, and signed 8-bit `char` literals.
An initializer resolves before its own declaration is introduced; a shadowing name or nested lambda
local does not leak into a later declaration initializer or a `for` step expression. A multiline raw
string receives one string semantic token per physical line, and text that resembles a comment marker
inside that literal is never classified as a comment.

## Workspace analysis observers

VeroScriptWorkspace::AnalyzeAll(OnDocument) calls its observer synchronously once per document with a borrowed path and analysis result. Copy data that must outlive the callback. The observer must not open, close or otherwise mutate documents in that workspace while analysis is iterating.

## VM selection for embedded analysis and debugging

Direct Tooling hosts select an interpreter explicitly. Use `VeroScriptDebugSession::SetVmBackendFactory`/`SetVmBackendPath` before execution, or supply `VmBackendFactory`/`VmBackendPath` in `FDebugLoadOptions` or `FDapStdioRunOptions`. A factory and a path are exclusive. Selection freezes when execution commits; clearing/reloading retains the domain and does not permit replacement. Failed selection preserves the previous backend. A matching Runtime launcher supplies its product backend; linking Tooling alone does not.

Queries that execute script compile extensions use the `ScriptCompileExtensions` options, including `VmBackendFactory` or `VmBackendPath`, on analysis/workspace options. Debug loads inherit their selected interpreter unless an explicit compile-extension backend was supplied. Parsing and ordinary analysis without executing such extensions need no VM. Keep the selected backend and its owning resources alive through the host operation.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
