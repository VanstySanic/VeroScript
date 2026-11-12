# VeroScript for VS Code

Some editor versions restrict terminal processes in an empty workspace. If Run/Check refuses the script's working directory, open that directory as a workspace. Domain attachment does not use a terminal.

## Quick start

Install the local VSIX in desktop VS Code 1.106.0+. Extract a matching Runtime for the extension host, run **VeroScript: Select Runtime**, and select the root containing all three programs, stdlib and localization.

Save a .vs file, then use **Run File**, **Check File** or **Debug File**. Basic coloring/snippets work without Runtime. WSL/SSH/container sessions need a Runtime inside that host.

## Project settings

A trusted workspace can select a host-provided stdio language server with `veroscript.lsp.executablePath`. The executable path uses the shared path rules below. `veroscript.lsp.arguments` passes each array element as one literal argument, including spaces or an empty string. This mode preserves the host's compilation defaults for settings you leave unset. An empty executable path selects the Runtime language server. Changes to the executable or its arguments restart the language server. The custom server inherits the extension host environment and has ten seconds to initialize.

Accepting a namespace completion opens its members, including nested namespaces, unless editor.suggestOnTriggerCharacters is disabled. VeroScript defaults editor.wordBasedSuggestions to off; override it in [veroscript] settings to enable textual words. Built-in templates match typed prefixes and use conservative lexical checks: imports and entry points at file scope, declarations in supported declaration scopes, and loops at statement boundaries. Templates are suppressed in comments, strings and unfinished expressions; user snippets remain managed by VS Code. Native filtering and sorting fields are preserved, with VS Code also matching the typed prefix.

Hover signatures use VeroScript code blocks and separate overloads, descriptions and definition locations. Resolved completion details avoid duplicate signatures and retain documentation, links and examples. Colors follow the VS Code theme; active parameters in signature help remain controlled by the native server.

Configure veroscript.importPaths, implicitImportPaths, defines, predefinedMacros and bindingCatalogPaths in VS Code settings. Relative paths use the owning workspace root or source directory. Shared settings accept workspaceFolder and environment variables; task program also accepts the current file. Missing variable contexts report errors.

Each root, including nested roots, owns a separate language server. Outside files have standalone contexts. Use **Show Output** for errors and **Restart Language Server** after replacing Runtime binaries.

Unsaved files in configured import/stdlib directories and explicit implicit imports synchronize to referring servers, with editor providers and diagnostics owned by their original context. The Runtime updates diagnostics for the edited file; a different referencing file updates its diagnostics when reanalyzed, for example after editing or saving it.

## Running and debugging

Run/check commands offer to save participating open sources before starting. Direct tasks obey task.saveBeforeRun. Tasks use native process execution and return actual exit codes. Arrays and task env replace inherited values.

F5 launches native VM DAP; use stopOnEntry, args and input as needed. The Debug Console evaluates expressions. Use Run File for execution without debugging. Initial stdin is supplied through input; interactive stdin is available through normal run tasks.

Program-mode attach uses program, host and port for an existing TCP DAP host. It does not require local Runtime or inject launch settings, cwd or env. It loads a program in the host rather than attaching to an executing VM/PID. Paths must be valid on both sides. Function/exception/log/hit-count breakpoints, reverse execution, inlay hints and CodeLens are not supported.

Use **VeroScript: Attach to Host** from the Command Palette or a .vs file's Explorer menu. It uses the selected/active saved script, or offers a file picker, then asks for an address and port (127.0.0.1:4711 by default). Canceling either input starts no session. It checks for unsaved participating sources and stops at entry. Enter IPv6 addresses without brackets. F5 configuration generation and launch.json completion also offer an attach template; save a configuration for fixed entry files, arguments or explicit compile options.

Connect to a compatible TCP DAP endpoint supplied by the host application. Use its matching `bindingCatalogPaths`, imports, macros and standard-library version for editing. Symbol metadata does not make host APIs executable in a local Runtime. Set explicit compile overrides in `launch.json` when required and save dependencies outside the configured source/import paths yourself. Addresses resolve on the extension host, including remote sessions. Use the native debugger for native code; this plugin debugs scripts.

### Memory types and debug inspection

With a matching Runtime, `std::memory` types and generic calls use native completion, hover and signature help. Debugging exposes buffer/view inspection children in Variables. These fields are readonly and their handles expire after resuming or stepping. Host-provided memory metadata is forwarded unchanged; arbitrary memory read/write requests and a dedicated memory editor are not supported.

### Live domain attachment

Use **VeroScript: Attach to Domain** or the domain launch.json template. Set request to attach and attachMode to domain, with host/port and an optional expected domainId. Empty/omitted domainId uses the endpoint’s domain. No open file or local Runtime is required. This mode does not save, load or restart scripts and rejects program, compile/run overrides and debugServer. Host calls can finish and start again in one connection. When the host ends the debug session, the plugin completes disconnection without requesting script cancellation.

The host endpoint must support live domain attachment; otherwise the connection fails without loading a program. The initial connection and initialization deadline is 10 seconds. Check the host/domain identity and source/binding revisions in the output. Revisions are opaque: compare your sources and API metadata manually, as automatic comparison and LSP synchronization are unavailable. Disconnect leaves execution and cancellation under host control. Connection loss ends the debugging session without automatic reconnection. Domain attachment cannot take over a VM already executing JIT code.

Pause, stepping, expression evaluation, conditional breakpoints and variable editing depend on the host’s supported features. Unsupported requests report an error. Ordinary breakpoints remain available. Stop/disconnect ends the debug connection; ask the host application to cancel script execution when needed.


## Optional JIT module

Runtime and JIT backends are distributed separately. veroscript.run.jitBackendPath optionally pins a compatible library file or directory for normal runs. Leaving it empty omits the CLI path override and inherits module-environment selection: an unset backend choice defaults to auto. If JIT is disabled or no usable automatic candidate exists, mixed uses VM and forced jit fails before execution. A task jitBackendPath overrides the setting; empty clears it. Relative module paths use the owning workspace/source directory, independently of cwd, and support run path variables. Check, LSP and local VM debugging do not load this module; launch/attach reject jitBackendPath. A live domain uses its host’s backend selection. Native directory discovery and ABI checks apply; LSP/DAP jitCapability describes the core service contract, not an installed backend.

## Current language tools and module sources

Raw strings `r"..."` can span lines; backslashes are literal and the first following quote closes the string. Coloring and template filtering follow these rules without Runtime. Changes to `.vslib` libraries, including uppercase extensions, are forwarded to the language server.

When the server advertises a module environment and its source method, navigation opens `veroscript-module:` documents as readonly source snapshots. Identical native paths in different workspaces stay separate. If the server restarts or its compile snapshot changes, navigate again from the owning source file; old views retain their previous text. These views provide source display and syntax coloring; they do not enable editing or add language queries inside the snapshot.

Native compile/annotation processors and module environments must be supplied by the LSP/DAP host. The default Runtime servers do not automatically import a host application’s processors or the CLI package environment. `bindingCatalogPaths` supplies metadata, including the exported compiler-service catalog for extension authoring; it does not install callbacks or processors. Use matching host services when the project requires them.

Annotation names follow the host definition or catalog `caseSensitive` policy; only ASCII name matching can ignore case. The plugin preserves canonical names and native signature parameter labels, while argument names remain case sensitive. Updating an existing `bindingCatalogPaths` file reloads its metadata without restarting the server. Organize Imports applies the server’s precise edits and preserves comments, raw strings, inactive branches and existing line endings.

## Brace layout and type hover

Inside an empty declaration or control block, Enter moves an inline opening brace onto its own line and indents the caret. This also works while typing `void a{}`. Existing separate-line braces, comments, strings and ambiguous initializers use ordinary Enter. VS Code indentation, EOLs, undo and matching multiple cursors are preserved; mixed cursor contexts use ordinary Enter. Suggestions, snippets and inline suggestions keep their Enter bindings. Disable `veroscript.bracesOnNewLine` to retain ordinary Enter everywhere. This local editing behavior needs no Runtime and does not change document formatting policy. Built-in type hover combines the localized category and type name into one title, preserving the description.

The switch defaults to on and can be changed in User or Workspace settings. Continued headers and constructor initializer lines use ordinary Enter to preserve their declaration indentation.

VeroScript files provide a compact V icon for light and dark themes in tabs and Explorer. The selected file icon theme controls its visibility and may supply its own icon; the extension does not change your theme.

Built-in primitive and container types (such as int and array) use the same compact type heading. Completion documentation renders built-in descriptions and metadata as prose; actual declarations retain VeroScript code blocks. Source aliases, overloads, links and examples are preserved.

Built-in templates stay hidden throughout backslash-continued preprocessor directives and line comments, then resume after they end. Continued line comments also retain comment coloring and ordinary Enter behavior.

With supporting language tools, completion in a class or struct body offers eligible inherited virtual-method and interface-member implementations. At a blank insertion point, refactor.rewrite offers the matching implementation action. The extension preserves native signatures, generic substitutions, insertion ranges and editable snippet bodies; the server filters already implemented, final and inaccessible members.

Re-export navigation also preserves transitive dependency targets and opens the read-only module source supplied by the owning server.

## Values and variable editing

With a supporting Runtime or host, VS Code uses each variable's DAP `presentationHint.attributes` to show readonly values, including immutable `variant` inspection children, nested payloads and watch results. The extension preserves these hints independently of the session's variable-editing capability. The native debugger enforces writes; an omitted hint is not inferred by the extension.

Exact numeric types such as `std::bigint`, `std::rational` and `std::numeric::rounding_mode` retain their native names in language-service results. Debugger values remain native display strings, without conversion to JavaScript numbers. Use a matching Runtime and its standard library for the available APIs.

## GUI companion

Install the optional [VeroScript GUI extension](../../../../../VeroScriptGuiVSCode/Resources/Docs/User/en-US/index.md) alongside this extension for `.vswml`/`.vswcss` editing and explicit native check/compile/preview tasks. It depends on this extension's tools API 1. Install matching local builds together. GUI-enabled Runtime/providers are separate; this extension continues to own `.vs`, LSP and DAP.

Keep the extracted Runtime directory complete, including the shipped `Vm/` interpreter module and its dependencies. CLI, LSP and DAP use the Runtime's default VM assembly; selecting `run.backend: "vm"` chooses an execution policy. Move or update the matching Runtime files together, then restart the language server.

<!-- docs-navigation -->

---

[User documentation](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
