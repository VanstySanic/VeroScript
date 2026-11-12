# VeroScript — Rider

## Installation

Install the plugin ZIP through Settings → Plugins → Install Plugin from Disk. Rider 2026.2.2 (262.10315.191) is the tested SDK baseline and minimum declared build; later builds need compatibility verification. For editing, Run, Check and local debugging, extract a matching Runtime and select its root in Settings → Languages & Frameworks → VeroScript. The root contains `veroscript`, `veroscript-lsp`, `veroscript-dap`, `stdlib/` and `localization/`. Project personal Runtime overrides the application default; when both are empty, discovery uses `PATH`. Basic coloring works without Runtime. Restart the language server after replacing its binaries.

Preserve the complete extracted Runtime, including `Vm/` and its native libraries,
when updating or moving it. The tools supply the default VM; no additional Rider
setting is required.

## Settings

Project semantic options accept a validated JSON object. Supported fields are `importPaths`, `implicitImportPaths`, `stdlibPath` or `stdlibSearchPaths`, `bindingCatalogPaths`, `defines`, `predefinedMacros`, `allowCircularImports`, `allowProjectWideAccess`, `lspTraceAnalysis`, `lspRequestTimeoutMs`, `lspAnalysisTimeoutMs`, `maxCompletionItems`, `maxWorkspaceSymbolResults`, `maxReferenceResults`, `maxWorkspaceFiles` and `maxWorkspaceFileBytes`. Binding catalogs describe symbols; the host must register executable callbacks. Shared semantics are stored in `.idea/veroscript.xml`; Runtime paths are personal. Tool language uses a BCP 47 tag; empty follows the IDE locale. Interface resources cover all six documentation languages.

## Editing

When a `//` comment ends in a backslash immediately before LF or CRLF, the next physical line remains a comment. Local highlighting and empty-block Enter follow the same rule, including inline VS in GUI markup. Spaces or tabs after the backslash prevent continuation.

Hover separates the symbol category from its name; primitive and container built-in types have one category heading. Source-defined type aliases keep their category and declaration. Signatures, overloads, documentation, examples and definition locations still come from Runtime and retain their contents.

**Move opening braces of empty blocks to a new line on Enter** is enabled by default in VeroScript settings. With the caret inside a same-line empty declaration or control block, Enter moves the opening brace to its own line and places the caret on an indented line inside. Comments, strings, selections, multiple carets and ambiguous or continued headers use ordinary Enter. This setting does not restart LSP; full/range formatting still uses the server's style.

Diagnostics remain Runtime results. `void t(){}` is a valid complete function; `void t` is incomplete and requires `()`. Editing diagnostics are retained and clear when the server accepts the completed source.

## Paths

Project paths use the project root. Working directory uses that root; program, input and run override paths, including JIT, use the resolved working directory. Inherited project paths keep their original base. `$PROJECT_DIR$` always identifies the project root. Omitted run options inherit project settings; explicit lists, including empty lists, replace them for launch. Run overrides do not change LSP settings.

## Run and Check

Right-click a saved `.vs` file or create a VeroScript Run Configuration. Any source may be an entry, including top-level statements without `main`. Mode `run` executes; `check` compiles without program arguments; `attach` connects using Debug. Arguments use standard IDE quoting (`""` is an empty argument); environment is a JSON object of strings. Normal input is streamed as bytes; debug input is prepared UTF-8 text. Local operations save documents first. Current File uses Rider’s own toolbar setting; enable `run.current.file.item.in.run.configurations.combobox` in Registry if hidden.

Context actions accept saved local files and select Run/Check independently of an
attach template. Check clears inherited script arguments, input and JIT paths;
compatible working-directory, environment and compiler options remain.

## Local debugging and JIT

Ordinary Run offers `vm`, `mixed` and `jit`. The JIT file/directory field optionally pins a compatible module. Leaving it empty omits the CLI path override and inherits module-environment selection: an unset backend choice defaults to auto. If JIT is disabled or no usable automatic candidate exists, mixed uses VM and forced jit fails before execution. Check and local Debug use no JIT module; attach requires an empty module path. Debug always uses VM. Entry pause is `inherit`/`true`/`false`; depth 0 retains defaults. Startup budget is 1–3600 seconds, default 30. Debug supports line/conditional breakpoints, pause, stepping, stack/scopes, paged variables, evaluation and assignment. Frames expire on resume. Automatic hover evaluates name paths only; use explicit evaluation for other expressions. Program-mode breakpoint edits while running wait for the next pause.

Variables are displayed in batches of at most 100 children. Expansion also works with hosts that provide complete lists without child counts. Continuing execution invalidates pending pages.

Variables marked read-only by the debugger do not offer editing. Editing other variables still depends on host capabilities and the debug context.

## Program attachment

Program attach connects to a TCP DAP host, not an arbitrary PID or executing VM. Start a host with `veroscript --debug --dap-port=4711 script.vs`; select attach/program, address and port. An empty program inherits the host entry. Leave working directory, input and environment empty; the host chooses backend and depth. Only explicit semantic overrides are sent. Nonempty arguments override; the override-empty option clears host arguments. Some empty semantic lists inherit host defaults and cannot clear them.

Program and domain attach trim surrounding host whitespace and reject embedded whitespace or control characters.

## Domain attachment

Domain attach selects attach/domain and optional expected domain ID. No local Runtime or current file is required. Leave launch overrides empty: environment `{}`, entry `inherit`, depth 0, backend mixed and parent environment inheritance enabled. The endpoint must advertise `veroscriptSupportsDomainAttach`; unsupported endpoints fail without loading a program. This connection spans successive host calls. Host capabilities govern pause, stepping, evaluation, conditions and assignment. Identity/revisions appear in the console; revisions are opaque, with no source comparison or automatic LSP synchronization. Physical source paths must be accessible locally; path mapping is not provided. Adapter source references are displayed as read-only snapshots. TCP connection budget is 5 seconds and initialization 10 seconds; domain attach uses the startup budget. Disconnect sends `terminateDebuggee=false`; cancellation and process ownership remain with the host. Connection loss ends the session without reconnect.

## Troubleshooting

Use language-service status, the debug console and IDE logs to diagnose failures. Install matching Runtime tools together. Supported integration targets are local Rider projects with Windows/Linux x64 Runtime transports. Other IDEs, Remote Development, Windows-to-WSL launch, macOS/ARM64 and actual application/UE domain integration need separate acceptance.

```json
{"importPaths":["src"],"defines":["FEATURE=1"],"bindingCatalogPaths":["bindings/catalog.json"]}
```

[LSP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/en-US/lsp.md)

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/en-US/debugging.md)


## Managed memory

With matching current Runtime tools, `std::memory` completion, hover and semantic coloring come from LSP. In a paused debug session, expand a buffer or view in Variables or Watches to inspect `[byte_length]`, buffer `[object_id]` and available addresses. Inspection entries are read-only; addresses are temporary observations. The plugin does not provide arbitrary memory reads or writes. Variable references expire on resume; expand the current stop again.

[std::memory](../../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md)

## Compilation extensions and generated sources

Current tools support public macros (`#define_export`, `export import`) and source compilation extensions (`import compiler "extension.vs";`). Use matching CLI/LSP/DAP tools and the same import paths. Generated methods and virtual modules with a DAP `sourceReference` open as read-only debug snapshots. These snapshots are not project sources: Run, Check, breakpoints and LSP synchronization are disabled for them. A source retrieval failure is reported without ending the paused session. Native host processing registries and module environments remain host configuration; the plugin does not expose them as additional semantic JSON fields.

<!-- docs-navigation -->

---

[User documentation](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
