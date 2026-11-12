# Tooling architecture and protocols

`VeroScriptTooling` is the language-service layer shared by the command-line,
LSP, DAP, and other hosts. It consumes the public language-core contracts and
keeps semantic queries separate from protocol transport.

LSP and DAP share the private JSON value, parser and serializer in `Private/Internal/VeroScriptToolingJson.h` and `.cpp`. String escaping uses `Shared/Private/VeroScriptJsonString.h`. Framing, request correlation, scheduling, cancellation and protocol-specific fields remain in their adapters. Preserve deterministic object ordering, numeric conversion, the existing depth limit and error text when changing the shared implementation.

## Layered data flow

The intended dependency direction is:

1. The language core owns parsing, semantic analysis, module loading, runtime
   metadata, diagnostics, and debugger semantics.
2. `VeroScriptWorkspace` supplies project context, document overlays, source and
   import resolution, preprocessor context, binding catalogs, and cross-file
   state.
3. Public feature services query a source or workspace state and return stable
   tooling result types.
4. LSP and DAP adapters serialize those results and own transport concerns.

Feature services do not read editor state or write source files. Adapters do not
reconstruct semantic results from protocol text when a public service already
provides the result.

## Public feature families

`VeroScriptAnalyzer` provides workspace-aware diagnostics. Diagnostics retain
stable identifiers, severity, source ranges, and substitution arguments. An
analysis may report issues in imported files; the LSP adapter groups them by
file label and publishes each URI separately, including an empty list when old
diagnostics must be cleared.

`VeroScriptOutline`, `VeroScriptNavigation`, `VeroScriptHover`,
`VeroScriptCallHelp`, `VeroScriptComplete`, `VeroScriptFindReferences`,
`VeroScriptDocumentHighlight`, `VeroScriptRename`, `VeroScriptFormat`, and
`VeroScriptWorkspaceSymbols` expose parse-tolerant structure, semantic lookup,
completion, calls, references, edits, formatting, and workspace searches.

The feature layer treats confirmed namespace segments as namespaces, including
nested user namespaces and the standard `std::` tree. Host binding catalogs and
soft builtins can participate in lookup, completion, hover, and workspace
symbols. Host-only symbols use the designated virtual host-bindings location
when a protocol result requires a URI.

Completion groups callable overloads with the same visible name into one item
and lists each source-facing signature on a separate detail line. In a call
context, the grouped item uses an empty argument stop instead of guessing
parameters from one overload. A non-overloaded callable or generic entry can
include snippet insert text when the client advertises
`textDocument.completion.completionItem.snippetSupport`; otherwise the LSP
adapter sends plain-text insertions. In a call context, callable completion
omits its snippet when an opening `(` already follows the completion target,
including across whitespace or comments, to avoid duplicating call syntax.
Rename validates the target before producing edits and re-resolves each
reference before changing it. Standard-library namespaces are not renameable.
Reference collection deduplicates token positions with a request-local set and resolves each existing path spelling once per scan.
Reference and rename passes use `Internal/VeroScriptSourceQuerySession` to retain
the current file's exact parsed program for their point queries. This state is
request-local, checks disk dependencies, and does not reuse recovered or
processor-driven programs.
Resolved block/callable locals whose declarations belong to the current source
do not require scanning other open modules with the same spelling.
Formatting returns text edits; the tooling layer never applies them.

`FCompletionItem::MatchQuality` is the primary textual ordering key;
`SortPriority` only breaks equal-quality matches. Protocol adapters preserve
this order through LSP `sortText` and set `preselect` only for an unambiguously
better exact, case-insensitive-exact, or prefix match. Primitive words are
built-in `Keyword` entries while container words are `TypeAlias`/`type`
entries. Both display localized detail, retain stable type identifiers, have no
source definition, and refuse rename.

Qualified namespace completion keeps the leaf in standard LSP `label` and
insertion text (`insertText` and, when a replace range exists, `textEdit.newText`),
including an available callable or generic type snippet when the client advertises
`textDocument.completion.completionItem.snippetSupport`; otherwise it sends the
plain-text leaf name. It sends the container in `labelDetails.description`. For
Valence, the optional
`$valencePresentation` field carries final label/declaration text and UTF-16
semantic spans; other LSP clients can ignore it. Presentation metadata never
changes filtering, sorting, or insertion.

Namespace-segment completion is distinct from namespace-member completion.
At expression or statement sites, a namespace item keeps its leaf in `label`
and `filterText` and appends `::` to insertion only when no separator already
follows the replacement range. A `using namespace` target inserts only the
leaf. When the client supports snippets, variadic host formatting functions
such as `print` use an empty argument stop (`print(${0})`) in call contexts,
rather than placeholders inferred from combined overload detail.

For semantic tokens, the LSP adapter combines resolved spans with a lexer
baseline for language/type words, directives, macros, and literals/comments.
The baseline remains available for incomplete input and overlaps are eliminated
before the LSP stream is encoded.

Constructor initializer lists participate in editing support: completion offers
unused direct bases and legally initializable direct fields, while signature
help resolves a field target through its declared type and shows that type's
constructor overloads. Incomplete later entries are recovered without treating
the initializer as an ordinary statement.

## LSP protocol contract

`VeroScriptLspStdio` owns `Content-Length` framing, JSON-RPC correlation,
request dispatch, cancellation, diagnostics publication, and lifecycle state.
Protocol-only configuration updates (budgets, tracing and result limits) retain
semantic snapshots. Analysis settings invalidate them; workspace discovery
limits invalidate the symbol index without invalidating parsed programs.
The advertised feature requests are:

- `textDocument/completion`
- `completionItem/resolve`
- `textDocument/hover`
- `textDocument/definition`
- `textDocument/declaration` (the VeroScript declaration location)
- `textDocument/typeDefinition`
- `textDocument/implementation`
- `textDocument/prepareTypeHierarchy`, `typeHierarchy/supertypes`, and
  `typeHierarchy/subtypes`
- `textDocument/documentSymbol`
- `textDocument/selectionRange`
- `textDocument/documentLink`
- `textDocument/foldingRange`
- `textDocument/semanticTokens/full`
- `textDocument/semanticTokens/full/delta`
- `textDocument/signatureHelp`
- `textDocument/references`
- `textDocument/documentHighlight`
- `textDocument/prepareRename`
- `textDocument/rename`
- `textDocument/formatting`
- `textDocument/rangeFormatting` (the selection expands to complete physical lines)
- `textDocument/onTypeFormatting`
- `textDocument/codeAction` (`source.formatDocument`, `source.organizeImports`,
  and `refactor.rewrite`)
- `workspace/symbol` and `workspaceSymbol/resolve`

The lifecycle and synchronization notifications are `initialize`,
`initialized`, `textDocument/didOpen`, `textDocument/didChange`,
`textDocument/didSave`, `textDocument/didClose`, `shutdown`, and `exit`.
`workspace/didChangeConfiguration` updates workspace settings. The server also
accepts workspace-folder and watched-file notifications, plus
`$/cancelRequest`, which returns `-32800` for a cancelled request.

Completion triggers are `.`, `:`, and `@`; signature-help triggers are `(` and
`,`; rename preparation is advertised. Completion resolve preserves supplied
documentation and promotes an item's detail into Markdown only when no
documentation is present. Selection ranges nest from token to line to
document, document links resolve quoted imports and includes, and semantic-token
delta returns either complete data or a replacement edit based on the previous
result identifier. `source.formatDocument` is the formatting action; the server
also offers `source.organizeImports` when it can safely remove exact duplicate
imports. At a blank cursor position inside a class or struct body,
`refactor.rewrite` offers generated implementations for accessible inherited
virtual members and eligible interface methods, including pure requirements
and overridable virtual defaults. Range
formatting supports partial selections, expands them to complete physical
lines, and returns the actual replacement range.

LSP positions are zero-based and use UTF-16 character offsets. The C++ tooling
types use one-based source positions and byte columns; the adapter performs the
single conversion between these contracts. Incremental changes are accepted,
and a document version that is not newer than the tracked version is ignored.

## Configuration and localization

The server reads `veroscript` settings from `initializationOptions` and from
`workspace/didChangeConfiguration.settings`. The settings include import and
implicit-import paths, `stdlibPath`, defines, predefined macros, circular-import
and project-wide-access policies, and ordered `bindingCatalogPaths`.

Binding catalogs are merged in order; duplicate definitions are rejected. A
changed catalog is reloaded on the next non-lifecycle request and open
documents are reanalyzed. A catalog describes host symbols and documentation;
it does not register runtime callbacks.

`initialize.locale` is a BCP 47 preference. The process keeps its
environment-selected catalog when the preference is unsupported, and missing
translated files or keys fall back to `en-US`. Diagnostic identifiers, message
arguments, binding identifiers, type and member names, protocol fields, URIs,
paths, and source text are not translated.

## Invariants

Implementations must preserve:

- the public source-position and range contract;
- document identity and accepted version attached to a result;
- cancellation, request correlation, and wire ordering;
- stable diagnostic identifiers and substitution arguments;
- the distinction between an empty feature result and a protocol failure; and
- the rule that tooling produces edits but does not apply them.

For the user-facing method list, configuration examples, and editor behavior,
see the [VeroScript Language Server reference](../../User/en-US/lsp.md).
## Debug and DAP boundary

The debug layer has two contracts. `VeroScriptDebugSession` owns compilation,
breakpoints, stepping, VM inspection, expression evaluation, variable writes,
output capture, and JIT/backend selection. `VeroScriptDapAdapter` only maps
those operations to DAP-shaped request and event data. It must not add JSON,
stdio framing, TCP framing, or a second breakpoint or evaluation engine.

`VeroScriptDebugSession::BindBreakpoint` owns physical-line/preprocessor-map
resolution and registers only exact compiled source labels. It returns an
unregistered result with localized display text for an invalid request or a
non-executable request with no eligible location. The adapter preserves the external DAP ID, reports the actual bound
line, and publishes a `Breakpoint`/wire `breakpoint` `changed` event after a
pre-launch breakpoint is rebound.

`FDapLaunchArgs` carries source or a path, overlays for multi-file debugging,
binding information, import policies, stop-on-entry, call-depth, and the
requested backend. It also carries script arguments, standard-library roots,
launch-local environment overrides, working directory, and initial stdin. The
adapter resolves the effective backend before loading; debugger attachment uses
`Vm` even when the request selected `Mixed` or `Jit`. `Initialize` reports the
engine version string, host platform, and JIT capability, but these values are
independent fields and are not reconstructed as a product version.

The adapter lifecycle is synchronous: `ConfigurationDone` owns the run until
it returns, and a stopped event is the only valid point for continue, step,
stack, scope, variable, and evaluate operations. `Disconnect` resumes an
active run and clears the session after the run returns. A wire transport must
emit `initialized` after the initialize response, not from
`VeroScriptDapAdapter::Initialize()` itself.

### DAP transport contract

The core debugger's `bReadOnly` status passes through `FDebugVariable` and
`FDapVariable` to `presentationHint.attributes = ["readOnly"]` on DAP variables.
Nested immutable variant payloads retain that status, including watch results.
The core debugger remains responsible for rejecting edits to immutable values.

`VeroScriptDapStdio::Run` reads and writes standard DAP Content-Length framed
JSON over caller-provided streams. `FDapStdioRunOptions` supplies default
bindings, catalog paths, script arguments, standard-library paths, environment,
working directory, and initial input. Live bindings take precedence over
catalog paths; catalog stubs are sufficient for compilation and analysis, but
native calls require a live binding registry. Live bindings are borrowed and
must outlive `Run`.

The stdio adapter accepts `program` or inline `source`, an optional `fileLabel`,
`workspace`, `cwd` or `workingDirectory`, `stopOnEntry`, `args`, `input`,
`env` or `environment`, `stdlibSearchPaths` or `stdlibPath`, import paths,
defines, binding catalog paths, circular-import and project-access policies,
`backend`, and `maxCallDepth`. Script arguments are passed to `main(string[]
args)` without adding the script path. DAP protocol input is not exposed as the
script's stdin; `input` is the initial stream content. Environment overrides
are applied only for the run and are restored afterward. Request values are
resolved against the workspace where applicable.

The adapter validates protocol input and bounded collections before execution:
up to 4096 script arguments, 2048 import, implicit-import, or standard-library
paths, and 1024 environment entries. Invalid JSON, backend names, or call-depth
values become localized protocol diagnostics. `Initialize` returns the adapter
name, symbolic version, platform, JIT capability, and capability flags; these
machine-facing fields and protocol method names are not localized.

`VeroScriptDapTcp::Run` reuses the same DAP framing over IPv4 and serves clients
sequentially. `FDapTcpRunOptions::BindAddress` defaults to `127.0.0.1`; an empty
address or `0.0.0.0` listens on all interfaces, so loopback is the recommended
safe default. `Port == 0` requests an ephemeral port, which is reported through
`OnListening` after the socket is ready. `MaxSessions == 1` serves one client;
`0` means unlimited sessions, and negative values are normalized to one. A
non-terminating attach disconnect closes the client and permits the next
session. A terminate request stops the listen loop cleanly; TCP maps the DAP
host-termination result to a successful server return while other listen,
accept, or session failures remain non-zero.

## LSP workspace and request contract

VeroScriptLspStdio::Run is a Content-Length-framed JSON-RPC server. The current
server keeps document analysis on a worker so that cancellation messages can be
observed while a request is running. It is an LSP service over the Tooling
workspace, not a second compiler or a separate source model.

### Initialization and workspace roots

The client may select a BCP 47 catalog with initialize.locale. An unsupported
tag leaves the process or host-selected catalog unchanged. initializeOptions
may contain a veroscript object with the settings listed below.

Workspace discovery has a deliberate precedence order:

- workspaceFolders, when present as an array, controls the roots even when empty;
  usable folder URIs become the closed-file indexing roots, and an empty array
  means no roots.
- rootUri is the fallback only when workspaceFolders is not supplied as an array.
- FLspStdioRunOptions::DefaultWorkspaceRoots are used only when initialization
  supplies neither workspaceFolders nor rootUri.

Workspace roots are indexing roots, not import paths. They are recursively scanned
only for .vs files; .git, .verocache, build, dist, Binaries, and Intermediate
directories are skipped. Open documents and explicit source overlays take
precedence over a file read from a root. workspace/didChangeWorkspaceFolders
updates the roots, and workspace/didChangeWatchedFiles invalidates the closed-file
index and reanalyzes open documents.

The server advertises open/close synchronization, incremental text changes, and
save notifications with the document text included. During editing it retains
the last structurally usable snapshot for open-document symbol queries, so a
temporarily incomplete buffer does not unnecessarily erase all navigation data.
The live buffer remains authoritative for diagnostics and requests that require
the current source.

### Advertised LSP surface

The initialize response advertises the following stable protocol surface:

- Completion and completionItem/resolve, with ., :, and @ trigger characters.
- Hover, definition, declaration, type definition, implementation, and type
  hierarchy preparation/query operations.
- documentSymbol, selectionRange, foldingRange, documentLink, full semantic
  tokens, and semantic-token delta. Range semantic tokens are not advertised.
- Signature help, references, document highlights, prepareRename, and rename.
- Full-document formatting, range formatting, and on-type formatting. On-type
  formatting is triggered by ;, }, or a newline.
- workspace/symbol and workspaceSymbol/resolve.
- Source code actions for source.formatDocument, source.organizeImports, and
  refactor.rewrite, including codeAction/resolve.

Implementation lookup is intentionally different from ordinary navigation:
it returns concrete derived or override declarations and does not alias an
implementation request to the original definition. Workspace symbol results
may include open documents, closed files below the configured roots, and
soft/host symbols when the relevant catalog options are enabled.

### Request cancellation and safeguards

The server accepts $/cancelRequest while another request is being processed.
Long-running workspace symbol collection also receives a cooperative cancellation
probe. A cancelled or timed-out request returns the LSP cancellation error and
must not publish or cache a partial workspace-symbol index.

The default server safeguards are:

- 2,000 ms interactive request deadline; 5,000 ms semantic analysis deadline.
- 512 completion items per response.
- 2,000 workspace symbols per response.
- 5,000 reference locations per response.
- 10,000 indexed workspace files.
- 2 MiB maximum source size for one indexed workspace file.

The corresponding veroscript settings are lspRequestTimeoutMs, lspAnalysisTimeoutMs,
maxCompletionItems, maxWorkspaceSymbolResults, maxReferenceResults,
maxWorkspaceFiles, and maxWorkspaceFileBytes. Values are clamped to safe
server-defined ranges. Workspace-symbol options may lower the workspace
resource limits for one C++ query, but may not bypass the limits configured on
the VeroScriptWorkspace.

### Formatting and source actions

VeroScriptFormat is lexical. `FFormatOptions` defaults to four spaces per
indentation level; set `bInsertSpaces=false` to emit one tab per level. LSP
formatting honors the request's `options.insertSpaces` and `options.tabSize`
fields: `insertSpaces=false` emits one tab per level;
otherwise `tabSize` sets the spaces per level, capped at 16 and defaulting to
four when absent or invalid. Defines and predefined macros are accepted for
callers that format and then reanalyze, but formatting itself does not expand
macros.

Range formatting expands a selection to complete physical lines and returns the
actual replacement range in FRangeFormatResult. This prevents a fragment inside
a token or line from producing an invalid partial edit. A failed format returns
an empty LSP edit rather than replacing the document with an untrusted result.

The advertised source actions are deliberately conservative. Format document
returns one whole-document edit when formatting succeeds. Organize imports
removes only duplicate import lines with the same lexical text; when block
comment delimiters are present, it declines to edit because line-oriented
matching cannot safely distinguish code from commented text. codeAction/resolve
returns the already materialized action without changing its edit payload.

### C++ workspace contract

VeroScriptWorkspace::WorkspaceSymbols merges open buffers, explicit overlays,
and closed files below WorkspaceRoots. MaxWorkspaceFiles and
MaxWorkspaceFileBytes apply to closed-file discovery; they are not import
resolution limits. FWorkspaceSymbolsOptions::ShouldCancel is borrowed for one
query invocation and is never retained. A cancelled query sets bCancelled; the same rule applies when
ImplementationDocument delegates to a cancelled workspace-symbol query. Callers
must treat its symbols or implementations as incomplete and must not cache or
publish them.

The workspace-symbol cache is used only for the default, uncustomized query and
is invalidated by document or configuration changes. Query-specific options,
explicit overlays, roots, limits, and cancellation probes are not folded into
that cached result.

## Source documentation comments

Completion queries attach documentation from contiguous `//`/`///` or `/* ... */` comments immediately before source-defined preprocessor macros; host completion documentation comes from binding metadata. `completionItem/resolve` preserves supplied documentation and uses `detail` as Markdown only when documentation is absent. Hover extracts contiguous comments before source declarations and formats `@param name text`, `@return text`, and `@returns text` tags with labels from the active locale catalog; unknown tags remain in the comment body. This is a Tooling presentation convention and does not change compiler comment handling.

## Semantic token scheduling

Semantic tokens use a separate analysis worker. The ordered protocol worker owns document updates, request state and output; analysis jobs own copies of buffers/options/bindings and publish immutable parsed/token snapshots only after version and dependency validation. Same-version full/delta requests reuse complete streams. Hover, definition and completion reuse valid parsed snapshots; recovered coloring snapshots are not used as original ASTs for point queries. Cancellation/deadline probes reach lexing, preprocessing, parsing, imports, symbol traversal and encoding. Queued cancellation wakes the protocol worker; expired jobs leave the queue without waiting for active analysis. Defaults are 2000 ms for interactive requests and 5000 ms for semantic analysis. `lspAnalysisTimeoutMs` separates the analysis budget and `lspTraceAnalysis` enables profiling. See the [request budgets](../../User/en-US/lsp.md).

### Callback, cache and lifecycle contracts

`FSemanticTokensOptions::ShouldCancel` and `RecordStage` are borrowed only for the synchronous query invocation and run on its analysis thread. Captures must remain valid until the call returns. Cancellation returns `bCancelled` with no tokens; incomplete results must not be published or cached. Profiling observer exceptions are ignored. `FSemanticTokensResult::Snapshot` owns the program and source storage but is opaque internal tooling state, not a supported external cache or reuse API.

Reuse requires matching document content/version, configuration and dependencies. Disk stamps are captured before reading imported sources/packages and checked again before publishing or reuse; invalid stamps prevent reuse. Overlay changes also invalidate affected imports. Closing and reopening a URI clears its parsed/token state and old result IDs even when the new document version restarts from a smaller number. Analysis-configuration changes invalidate in-flight jobs. Equal source labels avoid redundant filesystem canonicalization; unequal labels still use canonical path comparison.

`VeroScriptLspResponsivenessTest.py` exercises the real stdio process: full/delta reuse, hover during analysis, cancellation, deadlines, dependency edits, configuration changes and close/reopen. Lexer and workspace tests cover retry after cancellation and disk changes between loading and snapshot construction. Keep these regressions with the owned Tooling/core tests.

On Darwin, `Private/Internal/VeroScriptLspThread.h` creates the protocol and semantic workers with 8 MiB pthread stacks. Other platforms use `std::thread`. Preserve join/ownership behavior; the larger worker stack does not increase parser or analysis limits. JSON floating output uses the shared private formatting helper described in [Core decimal conversion](../../../../../VeroScript/Resources/Docs/Developer/en-US/architecture-and-pipeline.md#floating-formatting-support).

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](architecture-and-protocols.md) · [fr-FR](../fr-FR/architecture-and-protocols.md) · [ja-JP](../ja-JP/architecture-and-protocols.md) · [ko-KR](../ko-KR/architecture-and-protocols.md) · [zh-CN](../zh-CN/architecture-and-protocols.md) · [zh-TW](../zh-TW/architecture-and-protocols.md)
