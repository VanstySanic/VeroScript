# Workspace, LSP, and DAP

## Workspace state

`VeroScriptWorkspace` is the stateful boundary for project and module context.
It owns open-document overlays, source and import resolution, cross-file
analysis, workspace symbols, preprocessor context, and binding-catalog inputs.
An open overlay takes precedence over the corresponding file on disk. Closing
the document ends that overlay lifetime.

`TryGetDocument` returns an owned copy of one open buffer. A missing path returns
`false` and leaves the output unchanged. Use `SnapshotDocuments` for queries
that require all buffers; single-document LSP reads do not copy unrelated sources.

Workspace options include import paths, the standard-library path, implicit
imports, defines, predefined macros, circular-import policy, project-wide
access, and ordered binding-catalog paths. Catalogs are merged in order;
duplicate definitions are rejected. A catalog describes host functions, types,
members, annotations, templates, and documentation. It does not register
runtime behavior.

The LSP reloads binding catalogs when file timestamps or sizes change, or a catalog watcher reports a change. It loads
the complete replacement before swapping it into the workspace; an unavailable
or incomplete catalog retains the last valid bindings and reports an offline
status through `window/logMessage`. A later valid snapshot refreshes analysis.

## Document lifecycle and invalidation

The LSP host creates an overlay at `textDocument/didOpen`, applies incremental
changes at `textDocument/didChange`, refreshes it from the optional `text` field
at `textDocument/didSave`, and removes it at `textDocument/didClose`.
Diagnostics are published after open, an accepted change, save, and a workspace
configuration change.

Document versions are tracked per URI. A change whose version is older than or
equal to the last accepted version is ignored, so delayed client messages cannot
replace newer text. Reopening a URI starts a new overlay lifetime.

The LSP host can continue reading input while a request is running, but request
work is bounded and processed in wire order. A client can cancel a pending
request with `$/cancelRequest`; the server reports JSON-RPC `-32800`.

## Language feature flow

The direct services and their scope are:

- `VeroScriptAnalyzer`: workspace-aware diagnostics, including imported files;
- `VeroScriptComplete`: context- and prefix-aware completion;
- `VeroScriptCallHelp`: callable signatures and argument-position help;
- `VeroScriptHover`: resolved symbol and type information;
- `VeroScriptNavigation`: definition and declaration locations;
- `VeroScriptFindReferences`: references in the requested source context;
- `VeroScriptDocumentHighlight`: references filtered to the current file;
- `VeroScriptOutline`: parse-tolerant document symbols;
- `VeroScriptWorkspaceSymbols`: flattened symbols across the workspace;
- `VeroScriptRename`: validated targets and re-resolved reference edits; and
- `VeroScriptFormat`: whole-document and whole-line-expanded text edits.

The LSP adapter exposes these services through completion and completion
resolve, hover, definition, declaration, type definition, implementation and
type hierarchy; document symbols, selection and folding ranges, and document
links; full and delta semantic tokens; signature help; references and
highlights; prepare rename and rename; document, range, and on-type formatting;
the `source.formatDocument`, `source.organizeImports`, and `refactor.rewrite`
code actions; and workspace symbols with resolve. `source.organizeImports` is
returned when exact duplicate imports can be removed safely. `refactor.rewrite`
offers generated method implementations at a blank cursor position inside a
class or struct. Range formatting expands the requested selection to complete
physical lines and returns the actual replacement range; it does not require
the selection to cover the complete document.

Namespace segments are resolved semantically, not by identifier casing. This
includes nested user namespaces and the `std::` namespace tree. Host catalog
entries and soft builtins can participate in semantic results; host-only
locations use the virtual `veroscript:host-bindings` URI.

Completion keeps `FCompletionItem::MatchQuality` as its primary textual rank
and uses `SortPriority` only to break that tie. Adapters must preserve the
result order; the LSP `sortText` represents the same ordering and only assigns
`preselect` to an unambiguously better exact, case-insensitive-exact, or prefix
match. Primitive words remain `Keyword` in completion, hover, and semantic
tokens; container words are `TypeAlias`/`type`. Both are built-in types with
localized display detail, no source definition, and are never rename targets.
Use the `tooling.builtin_type.detail` and `tooling.builtin_type.documentation`
catalog keys for that display text; do not translate protocol fields or type
identifiers.

Callable overloads that share a visible name are grouped into one completion
item, with one source-facing signature per detail line. In call contexts, the
group uses an empty argument snippet stop rather than selecting parameters from
one overload. At a blank or identifier-prefix position in a class or struct
body, completion also offers accessible, non-final inherited virtual members
that are not already implemented, along with eligible interface methods,
including pure requirements and overridable virtual defaults. Generic signatures are specialized for the derived type;
the generated method body contains an editable `throw 0;` stub. Pure interface
implementations omit `override`. At a blank cursor position, those candidates
are also available as `refactor.rewrite` code actions.

Generated completion items set `FCompletionItem::bOverrideMember`;
`bImplementsInterfaceMember` distinguishes interface implementations from
inherited overrides. `Documentation` uses the active Tooling locale, and
`bInsertTextSnippet` marks the generated snippet in `InsertText`.

For a qualified namespace member, `FCompletionItem::Label` and `InsertText`
retain the leaf name and any snippet produced by tooling; `LabelDescription`
holds the qualified namespace. When serializing to LSP, include snippet text
only if the client advertises
`textDocument.completion.completionItem.snippetSupport`; otherwise send the
leaf name as plain text. Serialize the namespace as standard LSP
`labelDetails.description`.
The optional `$valencePresentation` adds Valence's rendered label/declaration
and UTF-16 semantic spans, which other LSP clients can ignore. It must not
change filtering, sorting, or insertion.

The LSP semantic-token adapter merges resolved semantic spans with a lexer
baseline. The fallback covers language/type words, directives, macros, and
literals/comments on every valid physical line, remains usable during parse
recovery, and removes overlaps before encoding the LSP stream.

## Diagnostics and localization

Diagnostics are grouped by the source file reported by analysis. Imported files
therefore receive their own `textDocument/publishDiagnostics` notification.
The analyzed document is published even when clean, and files that had
diagnostics in the previous analysis but not the current one are explicitly
cleared with an empty list.

`initialize.locale` is a BCP 47 preference for diagnostic and tooling display
text. Unsupported preferences preserve the catalog selected by the process
environment; missing files or keys fall back to `en-US`. Stable diagnostic
identifiers and message arguments remain machine-facing data. Protocol fields,
URIs, paths, identifiers, type names, and source text are never translated.

## Debugging

`VeroScriptDebugSession` maps core loading, execution, stop callbacks, stack
frames, scopes, variables, and evaluation into tooling data.
`VeroScriptDapAdapter` maps those results to DAP messages, while
`VeroScriptDapStdio` and `VeroScriptDapTcp` provide independent transports.
They must not reimplement VM evaluation, breakpoint semantics, or object
lifetime.

`VeroScriptDebugSession::SetOutputHandler` receives flushed output while
`Run()` is active. The callback receives the text chunk and a boolean that
distinguishes standard error from standard output. `FDebugRunResult` captures
the complete streams in `Output` and `ErrorOutput`, and the DAP adapter emits
corresponding `stdout` and `stderr` output events before the run terminates.

DAP protocol identifiers, paths, and expression text remain unchanged by
localization. For the user-facing LSP setup and configuration contract, see
the [Language Server reference](../../User/en-US/lsp.md).
## Debug session contract

Core has no implicit interpreter. Native hosts select a backend with
`SetVmBackendFactory` / `SetVmBackendPath`, or the mutually exclusive
`FDebugLoadOptions::VmBackendFactory` / `VmBackendPath`. `FDapLaunchArgs` and
`FDapStdioRunOptions` carry the same host-side selection; these are not DAP JSON
arguments. The standalone LSP and DAP products already select the official VM.
The debug session retains one execution domain: after execution begins its
backend selection is fixed, including across `Clear` and reloads. Repeating the
same factory, directory, or equivalent library path in load options is a no-op.
A rejected backend selection during a load clears the old program and debugger
state, so a later `Run` cannot execute stale code.

For debug loads, an unspecified `ScriptCompileExtensions` VM selection inherits
the debug domain's factory. Workspace queries preserve an explicit per-request
compiler VM selection. Selecting a compiler VM alone does not execute extensions
or disable ordinary-source analysis caches; actual roots/imports and processing
policy still govern preparation and cache eligibility.

`VeroScriptDebugSession` is protocol agnostic and is safe to host directly in
an IDE, test process, or adapter. `LoadSource` and `LoadFile` return
`FDebugLoadResult` and compile with overlays, defines, import policies, and an
optional non-owned binding catalog. `Clear` invalidates the loaded module and
its debugger state. `SetBreakpoint` returns a session-local identifier and
accepts a source file label, one-based line, and optional condition.

`BindBreakpoint` is the stricter adapter path: it resolves a one-based physical
source line through the preprocessing map against exactly one compiled source,
then registers only the resolved preprocessed location with
`SetExactBreakpoint`. It returns no registration for an inactive, missing,
ambiguous, or out-of-range request, or a non-executable request with no
eligible location, together with localized display text. A request inside a function may move forward to its next
executable physical line; module initialization requires an exact line.

`SetRunMode` selects continue, step-in, step-over, or step-out behavior.
`SetExecutionBackend`, `SetMaxCallDepth`, and `SetJitPolicy` are applied before
the VM run. `GetLastJitStats` reports the most recent execution snapshot.
Attaching a debugger forces the effective backend to `Vm` and invalidates the
JIT cache, so debugger behavior never depends on a native compiled entry.

Stop and output handlers are synchronous. The stop handler receives
`EDebugStopReason`; the output handler receives text plus an error-stream flag.
`GetStackTrace`, `GetLocals`, `GetChildren`, and `Evaluate` are valid while
stopped. `SetLocal` and `SetChild` update the selected paused value and return
structured evaluation errors instead of throwing protocol-specific errors.
`FDebugRunResult` preserves diagnostics, output, error output, and exit code;
the success flag must be interpreted separately from the exit code.

## DAP adapter lifecycle

`VeroScriptDapAdapter` is an in-process request/event mapper, not a DAP wire
server. A host calls `Initialize`, then `Launch` or `Attach`, installs
breakpoints, and calls `ConfigurationDone`. `ConfigurationDone` blocks through
`Run` and emits `Stopped`, followed by output and terminal events as applicable.
The main thread identifier is the adapter's stable single-thread identifier.

`setBreakpoints` keeps stable DAP IDs even when a source replaces its entries.
Before loading, its entries are pending and unverified. Loading rebinds them
through `BindBreakpoint` and emits a `Breakpoint` event for each entry; wire
adapters serialize it as a standard `breakpoint` event with `reason: "changed"`.
The returned DAP line is the actual bound physical line, not necessarily the
requested line.

Continue, `Next`, `StepIn`, `StepOut`, `StackTrace`, `Scopes`, `Variables`,
`Evaluate`, and `SetVariable` are valid while the adapter reports a stopped
context. In the load-and-run path, the synchronous stopped-event callback is
the place to call them; a live domain handles these requests on its protocol
thread while its host-owned execution remains paused.
The in-process `Attach` method (wire `attachMode: "program"` or omitted)
loads a source or file with the same semantics as `Launch`; it does
not attach to an already executing VM. `Disconnect` resumes an active run and
performs cleanup after the run has returned.

For a host-prepared persistent runtime, pass an `IVeroScriptLiveDebugDomain`
to the four-argument `VeroScriptDapStdio::Run` overload (or the two-argument
`VeroScriptDapTcp::Run` overload) and send DAP `attach` with
`attachMode: "domain"` and `domainId`.
The `initialize` response body sets `veroscriptSupportsDomainAttach` to `true`
only when the server was given a domain bridge; otherwise it is `false`.
Clients must require this field to be exactly `true` before sending a domain
attach request. A missing or false field means domain attach is unavailable;
do not fall back to program attach or launch, since older hosts may ignore
`attachMode` and load a program.
When present, `attachMode` must be the string `domain` or `program` on an
`attach` request; `domainId` must be a string and is only valid in domain mode.
Invalid fields fail the request without falling back to program loading.
The attach response includes host/domain identity, source and binding
revisions, source versions, and domain-specific target capabilities. The DAP
`initialize` capabilities describe the adapter across its launch and attach
modes; use the attach response for this domain's supported operations.
`configurationDone`
activates breakpoints without launching a program; the connection stays open
across successive host calls. The domain implementation owns debugger setup
before execution, safe-point dispatch, paused-frame invalidation, and the
resume policy on disconnect. The adapter does not attach to a VM already
executing JIT code. A default disconnect leaves the host alive. The host must
retain the domain object until the DAP session ends. This protocol exposes one
active debug context as DAP thread 1; hosts with concurrent calls must
serialize their debugged contexts for this domain.

In-process hosts may pass a live `IHostBindingCatalog` through launch
arguments. An out-of-process adapter must use serialized catalog data and may
not retain the in-process pointer. DAP names, paths, expressions, source
coordinates, and event payload fields are machine data and remain unchanged by
localization.

## Debug output and completion semantics

VeroScriptDebugSession::Run preserves the distinction between observable output
and process completion:

- FOutputHandler receives stdout as line-buffered output. A complete line,
  std::console::flush, or the implementation's bounded pending buffer flushes
  the current fragment; stderr is forwarded immediately.
- FDebugRunResult::Output and FDebugRunResult::ErrorOutput retain the complete
  captured streams even when no output handler is installed. Handler exceptions
  are isolated from the debugged program.
- bSuccess describes execution success, not the process result. A successful
  int main may therefore have a non-zero ExitCode; a failed execution returns
  diagnostics and a failure exit code.
- Debugger attachment forces the effective execution backend to VM and
  invalidates the JIT cache. SetExecutionBackend remains the stored pre-debug
  preference and does not override this debugger invariant.
- Source loading records preprocessor line maps. Breakpoints and stack
  positions are remapped to physical source locations, while conditional
  breakpoints and debug evaluation use the active defines and predefined
  macros.

## DAP transport and launch contract

`VeroScriptDapStdio::Run` uses DAP Content-Length framing over caller-provided
input and output streams. `FDapStdioRunOptions` provides defaults for live
bindings or binding catalogs, script arguments, standard-library roots,
environment, working directory, and initial input. Live bindings are borrowed
for the duration of `Run` and take precedence over catalog paths; catalog data
supports compilation and analysis, while native calls require the live registry.

Launch and program-mode Attach accept `program` or inline `source`, `fileLabel`, `workspace`,
`cwd` or `workingDirectory`, `stopOnEntry`, `args`, `input`, `env` or
`environment`, `stdlibSearchPaths` or `stdlibPath`, import and implicit-import
paths, defines and predefined macros, binding catalog paths, circular-import and
project-access policies, `backend`, and `maxCallDepth`. `args` is passed to
`main(string[] args)` without the script path. The adapter uses `input` as the
initial script stdin; DAP protocol stdin is not forwarded to the script.
Environment changes are scoped to the run and restored afterward.

The adapter rejects malformed JSON, unsupported backends, invalid call-depth
values, and oversized collections before starting execution. The limits are
4096 script arguments, 2048 import, implicit-import, or standard-library paths,
and 1024 environment entries. `Initialize` returns protocol metadata and
capability flags, while protocol method names and machine-facing fields remain
exact identifiers. `jitCapability` identifies the core JIT service contract
(currently `backend-services`), not the selected backend or its native-code
capabilities; execution hosts obtain those from `IJitBackendFactory::GetDescription()`.

Execution hosts can select a backend factory directly or call `SetJitBackendPath`
with an explicit UTF-8 library or directory path. The core loader checks module
ABI compatibility and keeps the library alive while its factories and artifacts
are retained. It does not discover backends through `PATH` or environment defaults.
Tooling does not load a JIT module: DAP `backend` only stores an execution
preference, and debug execution stays on the VM. For domain attach, the host
owns backend selection and must prepare the debug bridge before execution.

For a stoppable endpoint, set `FDapTcpRunOptions::Control` to a shared `FDapTcpRunControl` and call `RequestStop()` from the host. This wakes socket waits and detaches a live domain under its normal resume policy. Join the `Run` thread before releasing the domain or unloading the module. Use a fresh control after a stop request.

`VeroScriptDapTcp::Run` provides the same framing over IPv4 and accepts clients
sequentially. Its bind address defaults to `127.0.0.1`; an empty address or
`0.0.0.0` listens on all interfaces. `Port == 0` selects an ephemeral port and
invokes `OnListening` with the bound port. `MaxSessions == 1` serves one client,
`0` is unlimited, and negative values behave as one. A clean non-terminating
attach disconnect closes only the client; a terminate request stops the server
loop. TCP maps host termination to a successful server return and preserves
other failures as non-zero results.

## DAP locale and attach termination semantics

DAP stdio treats initialize.arguments.locale as a BCP 47 preference. An
unsupported tag leaves the environment-selected catalog unchanged. The
selection is applied before the initialize response and therefore covers
localized diagnostics emitted by the subsequent session.

Launch and program-mode Attach accept the same source-loading options, including program or
source, fileLabel, import paths, defines, predefined macros, source overlays,
implicit import paths, circular-import policy, project-wide access, backend, and
maxCallDepth. Attach is a protocol-level request shape for a host session; it
does not perform a mid-run VM attach.

After a successful Attach, disconnect defaults terminateDebuggee to false,
whereas a session started by Launch defaults it to true. Hosts may send an
explicit terminateDebuggee value. A false value releases the current debug
session without asking the attached host to terminate; a true value requests
host termination. For domain attach, `terminate` and a disconnect requesting
termination are accepted only when the attach response advertises
`veroscriptCanTerminate: true`; otherwise the request fails and the debug
connection remains active. Accepted cancellation follows the host's policy.
This distinction is important for TCP hosts that continue serving clients
according to their MaxSessions policy.

## Debugging an existing Session

`IVeroScriptLiveDebugDomain` is supplied and kept alive by the host. `Info()` returns a synchronized snapshot. Protocol-thread requests queue VM work at execution safe points; one connection exposes one active context (thread 1), so serialize debugged calls. `Connect()` must not emit callbacks or start/reload code; failed connect retains no callback. `Activate()` completes configuration. `SetBreakpoints` replaces one source atomically and returns one result per request in order with the supplied IDs; it does not emit duplicate events for that update. `Disconnect()` removes session breakpoints, drains callbacks and releases a stopped call according to host policy. Cancellation remains a separate host operation.

Before disconnecting a live domain, the adapter waits for dispatched pause/cancel calls to return. New controls fail while the connection closes; old controls never target the next connection, even when it reuses the same domain object. Control dispatch must not wait for disconnect or work queued to the thread draining those calls. Host disconnect and callback draining run outside the adapter control mutex. The host still serializes attach, resume, disconnect and stopped-state access.

All bridge callbacks and stopped-VM inspection, evaluation, mutation and resume belong to the Session controlling thread. A stop callback must process requests there while paused. Only `RequestPause()` and the shared `FExecutionControl::RequestCancel()` are cross-thread controls. Borrowed program, binding, source-overlay and entry-name views expire when the callback returns. Copy needed metadata; source overlays are current configuration, not a frozen source snapshot. Publish source/binding revisions separately.

See [Debugging an existing Session](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/session-debugging.md#debug-disconnect-lifetime).

## Implementation ownership

See [C++ embedding and session — Implementation ownership](../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/en-US/session.md#session-debug-implementation).

## Memory inspection

Memory inspection is carried by `FMemoryInspection` through debugger variables. A permitted storage address becomes decimal-string `memoryReference`; `veroMemory` supplies decimal-string `objectId`, `objectAddress`, `dataAddress` and `byteLength` when present. Preserve uint64 precision and omission when unavailable. Metadata children are read-only and have no writable target or fabricated storage address. `variablesReference` stays an opaque DAP handle, independent of object identity. Do not advertise arbitrary memory read/write capabilities.

## Compile processing and source metadata

Native hosts pass `FCompileProcessingOptions` through `CompileProcessing` on query options, `FWorkspaceOptions`, `FLspStdioRunOptions`, `FDapStdioRunOptions` or `FDapLaunchArgs`. Tooling installs general processors before module loading and shares core preparation with `AnnotationProcessing`. Use the same configuration for analysis, completion, navigation, indexing and debug loading. Registries must be frozen; each compilation creates independent processor instances. Native processors and callbacks cannot be sent in LSP/DAP JSON configuration; provide them when starting the service.

The general dependency reader takes precedence over the legacy annotation reader. Caches recheck dependencies actually read by processors and do not reuse results from non-cacheable processors. Call `SetOptions` after configuration changes. Core-owned origin intervals map `ActiveSource` edits back to original module coordinates for navigation, semantic tokens, diagnostics and debugging. Generated declarations keep separate read-only source identities. Hover and completion use core declaration documentation and respect its comment retention policy. Source metadata reflection types and members advertise signatures matching the core.

## Script compilation extensions

`ScriptCompileExtensions` on the same query, workspace and LSP/DAP startup options carries the core's extension roots, configuration and execution policy. Source-level `import compiler "extension.vs";` follows the core's preamble rules. Completion, path navigation and document links recognize that directive; navigation resolves the target without executing its processors.

For editing extension implementation source, supply `GetScriptCompileServiceCatalog()` through query `Bindings`, or export that catalog to an LSP `bindingCatalogPaths` file. This catalog supplies `std::compiler` types and signatures with compile-only placeholder callbacks. It does not provide an execution service to ordinary runtime code.

Semantic snapshots check the existence and contents of resources read by script callbacks, including resources that were initially missing. Overlay revisions and extension code changes invalidate results too. With a custom resource reader, script semantic results are analyzed afresh because the closed extension task's combined code/resource reader cannot be retained as a reusable callback. Full/delta responses still compare the published token stream. Hosts should call `SetOptions` when extension configuration changes.

## Protocol edits and source coordinates

Range and on-type formatting preserve fragments of multiline comments and raw strings. Format the complete literal or document to adjust surrounding code; formatting a fragment must not reinterpret its contents as executable code.

LSP watches configured binding catalogs alongside scripts when the client supports dynamic file watching. A catalog event forces a reload even when its timestamp and size are unchanged. Without an event, changes to either stamp are detected before queries. Invalid or unavailable catalogs retain the last successful snapshot and report its status. Configuration changes replace the registered catalog watchers.

LSP and DAP catalog paths are normalized and deduplicated using the platform's module identity rules; Windows case aliases identify the same file. DAP validates a replacement catalog and launch configuration before replacing the loaded program's bindings. A request rejected during validation leaves that program and its native function indices intact.

A cancelled or exceptional debug reload clears previous bytecode, bindings, source snapshots and breakpoints before propagating the exception. A subsequent successful load can reuse the debug session. DAP turns these load exceptions into failed request responses and keeps the connection available for another load.

DAP exposes thread 1 and rejects requests for other threads. `stackTrace` honors `startFrame` and `levels`, including zero for all remaining frames, while `totalFrames` describes the complete stack. Paging preserves stopped-state frame identities.

`FDebugStackFrame.Column` uses physical UTF-8 byte columns. `FDapStackFrame.Column` uses one-based UTF-16 columns; the stdio adapter applies the client's zero/one-based preference. Loaded debug sessions capture column corrections before source metadata is trimmed, including imported and generated sources. A later disk edit does not change those coordinates. Live domains must return UTF-16 columns in their DAP stack frames.

## Resolved module environments

Analysis, query, workspace and debug options accept `ModuleEnvironment`, the same immutable `FModuleEnvironment` used by core compilation. `FLspStdioRunOptions` and `FDapStdioRunOptions` expose it to embedding hosts. Publish a complete environment and keep its bindings compatible with any explicit live catalog; conflicting catalogs produce diagnostics. A new workspace environment invalidates its indexes. Resource dependencies observed by processors are rechecked even without a document edit.

The core resolver supplies mapped imports, overlays, source ownership, dependency visibility, processing configuration and resources. Import completion and navigation use that resolver. Host candidates respect binding origins and the native types exposed by visible public signatures. Final completion fallback does not enumerate the host catalog when parsing cannot restore its scope. Standard query metadata comes from the core standard-library catalog, including `std::i18n`; getter-only native properties retain their `property` and `readonly` semantic classification.

When an environment is supplied, LSP advertises `capabilities.experimental.veroscriptModuleEnvironment` with compile/execution fingerprints and `moduleSourceMethod: "veroscript/moduleSource"`. A request with `{ "uri": "veroscript-module:/..." }` returns the accessible snapshot's `uri`, `languageId`, `text`, `readOnly` and compile fingerprint, or `null` for an unavailable or unauthorized module. Source responses remain within the protocol frame budget. Clients supporting these in-memory modules must provide a document-content provider for the URI scheme and call the advertised method; ordinary file navigation continues to use file URIs.

Legal re-export navigation also makes its returned transitive target readable without exposing every transitive module. `veroscript-module:` documents are read-only; rename does not return edits that modify them.

Debug sessions retain the environment for execution and validate its external dependencies. `TryGetSource` and DAP `sourceReference` provide frozen inline main-buffer, memory-module or generated source. An inline main buffer is captured even with a physical file label, so a missing or outdated disk file does not replace the compiled text. A source overlay returns the actual compiled version, while unchanged module text is borrowed from the immutable environment. Reload/clear invalidates old source references. Existing live-domain hosts continue supplying source through their domain source callback.

Source overlays follow the loader's last-wins rule for a path identity. Editor buffers override environment source snapshots consistently in references, rename, documentation and diagnostic source mapping; repeated overlay paths are not indexed as different documents.

Member access checks retain qualified type identities: matching leaf names in different namespaces do not grant private, protected or friend access. A short spelling must identify the same unambiguous declaration, and a global declaration takes precedence over namespaced homonyms. Explicit global receiver spellings such as `::Same` resolve to that global declaration as well.

Import organization removes only complete duplicate global import declarations in one uninterrupted block, confirmed through module preprocessing with the current workspace configuration and buffers. Lines rewritten by source, host or imported public macros, and inactive branches, are preserved. Lines that cannot be confirmed, for example after an unresolved dependency, are retained. Directives, comments, other declarations and scope boundaries end a block. Literal contents and additional statements are preserved; continued physical lines or lexical errors make the organizer decline edits. The action deletes only duplicate lines and preserves the original line endings.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](workspace-and-debug.md) · [fr-FR](../fr-FR/workspace-and-debug.md) · [ja-JP](../ja-JP/workspace-and-debug.md) · [ko-KR](../ko-KR/workspace-and-debug.md) · [zh-CN](../zh-CN/workspace-and-debug.md) · [zh-TW](../zh-TW/workspace-and-debug.md)
