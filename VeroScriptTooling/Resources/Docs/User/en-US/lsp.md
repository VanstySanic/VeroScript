# VeroScript Language Server

`veroscript-lsp` is the VeroScript language server. It communicates through
standard input and output using JSON-RPC messages framed with `Content-Length`.
An editor or another LSP client should launch the language-server executable
from the same VeroScript distribution as the project.

The server uses the compiler and tooling engine directly. It therefore shares
the language rules, imports, preprocessor state, host-binding metadata, and
diagnostic catalog with the command-line tool. An editor should pass the same
project settings to both processes instead of implementing a second parser.

Hover returns structured Markdown: a symbol heading, fenced VeroScript signatures
with overloads on separate lines, documentation and a definition location. Native
Tooling and CLI hover summaries remain plain text.

## Lifecycle and transport

The normal request order is:

1. send `initialize`;
2. wait for the response and send `initialized`;
3. send document, workspace, and language-feature messages;
4. send `shutdown` and wait for its response; and
5. send the `exit` notification.

The `exit` notification cancels queued and active semantic analysis, so process shutdown does not wait for the analysis deadline.

The server accepts JSON-RPC messages from the wire while a request is running.
The protocol worker applies document updates in wire order. A client can send `$/cancelRequest`
for a pending request. A cancelled request returns JSON-RPC error code `-32800`.
Closed-workspace symbol scans check cancellation cooperatively while enumerating
files and between individual file analyses. Requests also have a bounded server
deadline, so a stale editor query cannot keep a large workspace scan alive
indefinitely. The input queue is bounded and applies backpressure instead of
growing without limit.

If the client advertises dynamic registration for `workspace/didChangeWatchedFiles`,
the server sends `client/registerCapability` after `initialized` and requests
watchers for `**/*.vs` and `**/*.[vV][sS][lL][iI][bB]`. Clients without dynamic registration
support may configure those patterns themselves and send
`workspace/didChangeWatchedFiles` notifications explicitly.

`Content-Length` is measured in UTF-8 bytes. Headers and the body are separated
by an empty CRLF line. Protocol errors or malformed JSON are reported on the
process error stream. An `exit` sent without a successful `shutdown` produces a
non-zero process result.

File URIs follow standard LSP file-URI rules. A UNC URI such as
`file://server/share/module.vs` remains a network path and round-trips without
losing its host or share components.

## Semantic analysis and request budgets

Document updates are applied in reception order. Semantic-token analysis runs on a separate worker with owned document and binding snapshots, allowing hover and completion to respond while coloring is running. Valid parsed snapshots and full token streams are reused for unchanged documents. Document/dependency changes invalidate affected results; analysis-setting, watched-file and binding changes trigger reanalysis. Changing only request budgets, tracing or result limits retains existing semantic snapshots; workspace discovery limits refresh symbol search without clearing parsed programs. Disk dependency timestamps and sizes are checked before reuse. Superseded analysis returns `-32801` and cannot publish stale tokens.

Closing and reopening the same URI starts a new document lifecycle: old semantic token result IDs are discarded even if the version number restarts. A delta request with an old ID receives full token data; use the newly returned `resultId` for subsequent requests.

Budgets start when the protocol worker dispatches a request. Semantic analysis queue time counts toward its budget; time spent in the input queue does not. Cancelled work returns `-32800`, expired work returns `-32001`. Queued expired requests are released without waiting for an active analysis. For compatibility, setting only `lspRequestTimeoutMs` also sets the analysis budget; set `lspAnalysisTimeoutMs` explicitly to separate them. Tracing reports `lex`, `preprocess`, `parse`, import resolution (`imports`), symbols, queries, encoding and analysis queue times. `load` includes its nested parsing/preprocessing/import stages; timings must not be added as exclusive costs.

Client timeouts must also allow for protocol-queue waiting and document-update/diagnostic work. These cooperative analysis budgets do not guarantee a maximum end-to-end response time.

## Initialization and configuration

`initialize.locale` is a BCP 47 language preference for diagnostics and other
tooling-produced display text. Set it before opening documents. The language
server applies the `veroscript` object in `initializationOptions`, and accepts
the same object in `workspace/didChangeConfiguration.settings`:

```json
{
	"locale": "en-US",
	"initializationOptions": {
		"veroscript": {
			"importPaths": ["src", "shared"],
			"stdlibPath": "stdlib",
			"defines": ["FEATURE_SCORE=1"],
			"implicitImportPaths": [],
			"allowCircularImports": true,
			"allowProjectWideAccess": false,
			"lspRequestTimeoutMs": 2000,
			"lspAnalysisTimeoutMs": 5000,
			"lspTraceAnalysis": false,
			"maxCompletionItems": 512,
			"maxWorkspaceSymbolResults": 2000,
			"maxReferenceResults": 5000,
			"maxWorkspaceFiles": 10000,
			"maxWorkspaceFileBytes": 2097152,
			"bindingCatalogPaths": ["bindings/core.json", "bindings/game.json"],
			"predefinedMacros": [
				{
					"name": "FEATURE",
					"parameters": ["value"],
					"variadic": false,
					"body": "value",
					"sourceName": "host"
				}
			]
		}
	}
}
```

| Setting | Type | Meaning |
| --- | --- | --- |
| `importPaths` | string array | Additional module search roots. |
| `stdlibPath` | string | Standard-library search root. An empty value clears the custom root. |
| `defines` | string array | Object-like preprocessor definitions, such as `FEATURE_SCORE=1`. |
| `implicitImportPaths` | string array | Files made available to every analyzed source, subject to normal visibility rules. |
| `allowCircularImports` | boolean | Whether cyclic module dependencies are accepted. |
| `allowProjectWideAccess` | boolean | Whether non-exported declarations can be shared project-wide. |
| `lspRequestTimeoutMs` | integer | Interactive request deadline in milliseconds (50–30000), default 2000. |
| `lspAnalysisTimeoutMs` | integer | Semantic full/delta budget in milliseconds (50–30000), default 5000. |
| `lspTraceAnalysis` | boolean | Emit analysis timings through `window/logMessage`, default false. |
| `maxCompletionItems` | integer | Maximum completion entries returned in one list (32–5000); a truncated list is marked incomplete. |
| `maxWorkspaceSymbolResults` | integer | Maximum symbols returned by one `workspace/symbol` request (64–20000). |
| `maxReferenceResults` | integer | Maximum locations returned by one references request (64–50000). |
| `maxWorkspaceFiles` | integer | Maximum closed `.vs` files indexed below LSP workspace roots (1–100000). |
| `maxWorkspaceFileBytes` | integer | Per-file closed-workspace indexing size limit (1024–67108864 bytes). |
| `bindingCatalogPaths` | string array | Ordered host-binding catalog files. Catalogs are merged and duplicate definitions are rejected. |
| `predefinedMacros` | object array | Host-style function macros with `name`, `parameters`, `variadic`, `body`, and optional `sourceName`. |

Paths and macro definitions should match the CLI invocation used for the same
project. `bindingCatalogPaths` is checked when the server starts and again when
the files change. A changed catalog is reloaded and all open documents are
reanalyzed; a catalog does not register runtime callbacks by itself.

The `initialize` response also contains `serverInfo` with the server name,
generated product version, host platform, and JIT capability. These fields
describe the running distribution; clients may display them but should not
hard-code a particular version or infer language semantics from the platform or
JIT value.

## Advertised capabilities

The server advertises the following LSP capabilities:

| Capability | Contract |
| --- | --- |
| Text synchronization | Open/close notifications, incremental changes, and save notifications with text. |
| Completion | Trigger characters `.`, `:`, and `@`; completion items include resolved documentation when available. Resolve preserves it and promotes `detail` to Markdown only when documentation is absent. |
| Hover | Resolved symbol detail and contiguous source or host documentation. Source `@param` and `@return(s)` tags use localized labels. |
| Definition and declaration | Definition and declaration requests resolve to VeroScript declaration locations. |
| Type definition | Navigates to a matching class, struct, interface, enum, or type-alias declaration, with recovery for incomplete buffers. |
| Implementation | Finds concrete derived types and matching interface/virtual overrides; it does not alias implementation to definition. |
| Document symbols | Nested symbols, including user and standard namespaces. |
| Selection ranges | Token, line, and whole-document parent ranges. |
| Document links | Resolves quoted `import` and `#include` targets through module search paths and open documents. |
| Folding ranges | Lexer-safe block, contiguous comment, and import-group folding ranges. |
| Semantic tokens | Full tokens and full delta updates; range requests are not advertised. |
| Signature help | Trigger characters `(` and `,`. |
| References and highlights | Workspace references plus read/write-aware document highlights. |
| Rename | `prepareRename` is supported before `rename`. |
| Formatting | Whole-document, line-expanded range formatting, and on-type local formatting after `;`, `}`, or a newline. |
| Code actions | `source.formatDocument`, safe `source.organizeImports` duplicate-import removal, and `refactor.rewrite` for generated method implementations; `codeAction/resolve` is supported. |
| Workspace symbols | Case-insensitive matching across open buffers, configured workspace roots, and available host/soft symbols. |

## Protocol method surface

The server accepts these lifecycle and synchronization messages:

- `initialize`, `initialized`, `shutdown`, and `exit`;
- `textDocument/didOpen`, `textDocument/didChange`, `textDocument/didSave`, and
  `textDocument/didClose`;
- `workspace/didChangeConfiguration`, `workspace/didChangeWorkspaceFolders`,
  and `workspace/didChangeWatchedFiles`; and
- `$/cancelRequest` for cancelling a pending request.

The feature request methods are:

- `textDocument/completion` and `completionItem/resolve`;
- `textDocument/hover`, `textDocument/definition`,
  `textDocument/declaration`, `textDocument/typeDefinition`, and
  `textDocument/implementation`;
- `textDocument/prepareTypeHierarchy`, `typeHierarchy/supertypes`, and
  `typeHierarchy/subtypes`;
- `textDocument/documentSymbol`, `textDocument/selectionRange`, and
  `textDocument/documentLink`, and `textDocument/foldingRange`;
- `textDocument/semanticTokens/full` and
  `textDocument/semanticTokens/full/delta`;
- `textDocument/signatureHelp`, `textDocument/references`, and
  `textDocument/documentHighlight`;
- `textDocument/prepareRename` and `textDocument/rename`;
- `textDocument/formatting`, `textDocument/rangeFormatting`,
  `textDocument/onTypeFormatting`, `textDocument/codeAction`, and
  `codeAction/resolve`; and
- `workspace/symbol` and `workspaceSymbol/resolve`.

## Document synchronization

The server uses LSP incremental synchronization (`textDocumentSync.change = 2`).
Each change is applied to the current in-memory overlay in the order sent by the
client. The server accepts full-text changes as well as ranged changes. LSP line
and character positions are zero-based and use UTF-16 code units.

Document versions are monotonic within each open lifecycle of a URI. A `didChange` whose version is not
newer than the last accepted version is ignored, which prevents delayed editor
messages from replacing newer text. `didSave` may include the complete text and
updates the overlay before diagnostics are published. `didClose` removes the
overlay and clears previously published diagnostics for that URI.

Open buffers override files on disk during import resolution. This means an
imported module can be analyzed against its current editor contents even before
it is saved.

`initialize.workspaceFolders` (or `rootUri`) establishes explicit closed-file
index roots. `workspace/didChangeConfiguration` applies the new workspace
settings and reanalyzes open documents. `workspace/didChangeWorkspaceFolders`
adds and removes those roots before reanalyzing open documents. A
`workspace/didChangeWatchedFiles` notification invalidates the workspace symbol
index and reanalyzes open documents so on-disk changes become visible.

## Diagnostics and language features

After `didOpen`, an accepted `didChange`, `didSave`, or workspace configuration
change, the server publishes `textDocument/publishDiagnostics`. Diagnostics are
grouped by source file, including imported files reported by the analyzer. When
a file becomes clean, the server publishes an empty diagnostic array so stale
editor markers disappear. Each diagnostic includes its LSP severity, message,
stable code when available, and `source: "veroscript"`.

The query results follow the language model rather than identifier casing:

- Completion groups callable overloads with the same visible name into one item
  and lists each source-facing signature on a separate detail line. In a call
  context, an overloaded callable uses one empty argument stop, such as
  `Select(${0})`, rather than guessing which overload's parameters to insert.
  A non-overloaded callable can include an LSP snippet such as
  `Helper(${1:count}, ${2:name})`; generic type completions can include snippets
  such as `HostPair<${1}, ${2}>`. If `<`
  follows the replacement range, with optional spaces or tabs, the server omits
  the generic snippet and inserts only the type name, avoiding a second `<`.
  Callable completion omits its snippet when an opening `(` already follows
  the target, avoiding duplicate call syntax.
  Snippet text is sent only when the client advertises
  `textDocument.completion.completionItem.snippetSupport`; otherwise insertion
  falls back to plain text. With snippet support, variadic host formatting
  functions use one empty argument stop in call contexts, for example
  `print(${0})`, rather than placeholders derived from combined overload
  detail. Without snippet support, only the function name is inserted. Host
  catalogs and soft builtins participate in completion.
- At a valid blank or identifier-prefix position in a class or struct body,
  completion can generate declarations for accessible inherited virtual
  members that are not final and have not already been implemented, plus
  eligible interface methods, including unimplemented requirements and
  overridable virtual defaults. Generic signatures are
  specialized for the derived type. Accepting a candidate inserts a method
  skeleton with an editable `throw 0;` body; pure interface implementations do
  not add `override`. At a blank cursor position, the same candidates are
  available as `refactor.rewrite` code actions. Clients without snippet support
  receive plain method text.
- A completion from a qualified namespace keeps its leaf name in `label` and
  uses an available callable or generic type snippet in `insertText` (and
  `textEdit.newText` when present) when the client advertises support;
  otherwise it inserts the leaf name as plain text. Its container is in
  standard `labelDetails.description`. The optional `$valencePresentation`
  metadata provides Valence with rendered label/declaration text and UTF-16
  semantic spans; other LSP clients can ignore it. It never changes filtering,
  sort order, or the inserted text.
- Namespace-segment completion keeps the leaf in `label` and `filterText`. At
  an expression or statement site, accepting a namespace inserts `::` unless a
  separator already follows the replacement range; `using namespace` targets
  insert only the leaf.
- Completion orders textual-match quality before contextual priority: exact,
  case-insensitive exact, prefix, word-boundary/camel-case, contained, then
  subsequence matches. Its `sortText` carries that complete order. The first
  item is marked `preselect` only when its exact, case-insensitive-exact, or
  prefix tier is strictly better than every other returned item.
- Primitive and container words are built-in types. A primitive spelling stays
  a `keyword` in completion, hover, and semantic tokens; a container spelling
  is a `type`/type alias. Both provide localized hover detail, have no source
  definition, and cannot be renamed.
- `completionItem/resolve` keeps the original completion item and preserves
  supplied documentation. If documentation is absent but `detail` is present,
  resolve exposes `detail` as Markdown documentation.
- Hover reads contiguous comments before source declarations. Completion items
  for source-defined preprocessor macros can carry their preceding comment;
  host completion documentation comes from binding metadata. Hover formats
  source `@param`, `@return`, and `@returns` tags with localized labels.
- Hover, definition, type definition, references, rename, and semantic tokens
  treat every confirmed namespace segment as a namespace. This includes nested
  user namespaces and the reserved `std::` tree. Incomplete input is handled as
  an editor state; hover may use the last structurally usable snapshot when the
  current token is unchanged.
- `textDocument/declaration` is compatible with definition lookup because
  VeroScript has no separate forward-declaration target in this protocol.
- `textDocument/implementation` returns derived types and matching interface or
  virtual-member overrides; only non-final virtual, override, or abstract members
  with matching parameter and const signatures qualify. Ambiguous type-name matches
  are omitted, and the request does not masquerade as a definition lookup.
- `textDocument/prepareRename` validates the symbol and returns its exact range
  and placeholder. User namespaces can be renamed together with their qualified
  uses. Standard-library namespaces are read-only and return a rename error.
- Type-definition lookup prefers semantic navigation and can recover a matching
  type declaration from a parse-tolerant outline or incomplete declaration head.
- Selection ranges nest from the token under the cursor to its line and then to
  the whole document.
- Document links recognize quoted module paths after `import` or `#include`
  (including an optional `export` prefix). They use the core module resolver,
  including the current directory, configured search paths, standard library
  roots and unsaved open documents. Unresolved targets keep a local link.
- Semantic-token delta returns a complete data payload when the previous
  result is unknown or stale. When the result identifier matches, it returns
  the smallest contiguous integer-stream edit (or an empty edit list) and a
  new result identifier.
- The complete semantic-token legend is `namespace`, `type`, `class`, `enum`,
  `interface`, `struct`, `variable`, `property`, `enumMember`, `function`,
  `method`, `macro`, `keyword`, `decorator`, `decoratorArgument`, `constant`,
  `label`, `string`, `number`, `comment`, and `preprocessor`. A lexical
  baseline also classifies language/type words, directive heads, and macro
  names. It covers every valid physical line without overlapping LSP spans, and
  remains available while declaration analysis recovers from incomplete input.
- Range formatting safely expands a selection to complete physical lines and
  preserves enclosing block indentation. Full-document, range, and on-type
  formatting honor `insertSpaces`: `insertSpaces=false` emits one tab per level;
  otherwise `tabSize` sets the spaces per level, capped at 16 and defaulting to
  four when absent or invalid. On-type formatting applies the same local
  line-safe edit after `;`, `}`, or a newline.
- A code-action request returns `source.formatDocument` unless `context.only`
  excludes it. It also returns `source.organizeImports` only when exact
  duplicate import lines can be identified and safely removed; that action
  removes only those duplicates.
- At a blank cursor position in a class or struct body, a code-action request
  can also return `refactor.rewrite` actions for the same generated method
  implementations offered by completion. `context.only` can filter these
  actions by `refactor` or `refactor.rewrite`.
- Workspace-symbol queries are case-insensitive and split on whitespace. They match
  symbol names, qualified names, containers, details, and file labels; both `::`
  and the legacy `.` separator are accepted. Exact and prefix matches rank ahead of
  fuzzy subsequence matches. Each item carries stable `data.veroscriptSymbolId`;
  `workspaceSymbol/resolve` refreshes its detail and container fields and preserves
  the original item when the ID is unknown.
- Document symbols and workspace symbols expose namespace containers. The same
  declaration identity is returned once even if it is observed through multiple
  imported paths. Host catalog entries without a source file use the
  `veroscript:host-bindings` URI and a zero range.

When a request has no applicable result, the server returns the LSP null or
empty result appropriate to that method. Unsupported methods return JSON-RPC
`-32601`; malformed parameters return `-32602` where the method requires a
specific document or position.

## Localization

The server uses the same localization policy as the CLI and runtime. The
language in `initialize.locale` changes diagnostic and tooling presentation,
not script semantics or symbol resolution. Message IDs/codes remain the stable
integration boundary; clients should not branch on translated message text.
Missing translated files or keys fall back to the `en-US` catalog.

## Related references

- [Tooling module](index.md) for the C++ query and workspace surface.
- [CLI reference](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md) for compilation options,
  script arguments, and package commands.
- [Host bindings and binding catalogs](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/bindings.md) for catalog fields,
  validation, and runtime registration.
- [Debugging](debugging.md) for the separate DAP transport.

## Host-provided module sources

A host can provide a resolved module environment and compilation processors. The standard Runtime server does not automatically adopt the CLI package selection or an application’s native callbacks. Match imports, binding catalogs and host services to the program you are editing; a catalog supplies descriptions, not executable processors.

When `capabilities.experimental.veroscriptModuleEnvironment` advertises `moduleSourceMethod`, a supporting client can use that method (`veroscript/moduleSource`) to open returned `veroscript-module:` locations as read-only source. These are snapshots, not editable workspace files. Navigate again after the server restarts or its compile fingerprint changes. A missing or inaccessible module returns `null`. Ordinary disk sources continue to use file URIs.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](lsp.md) · [fr-FR](../fr-FR/lsp.md) · [ja-JP](../ja-JP/lsp.md) · [ko-KR](../ko-KR/lsp.md) · [zh-CN](../zh-CN/lsp.md) · [zh-TW](../zh-TW/lsp.md)
