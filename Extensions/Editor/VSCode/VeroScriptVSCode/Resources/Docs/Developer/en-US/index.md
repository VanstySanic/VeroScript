# Developer documentation — VS Code

Shared editor/native responsibilities and six-language maintenance are maintained in [Common editor integration](../../../../../../../../Resources/Docs/Developer/en-US/editors.md).

Keep Core, Tooling, VM, JIT and GUI providers from a matching native source/build configuration, including the C++ language mode; the same product version alone does not prove ABI compatibility. SDK integration cases cover static generic, handle and nested-type receivers through completion and hover. Optional `GetLastJitDiagnostics` snapshots belong to the C++ host API and are not exposed by the current LSP/DAP wire protocol.

Use `vscode.l10n.t` with `Resources/Localization/Bundles` for runtime UI messages. Localized manifest sources under `Resources/Localization/<locale>/manifest.json` generate `package.nls` files. Preserve localization argument indexes and manifest references.

Use Node.js 22.18.0+ and desktop VS Code 1.106.0+. Run the commands below from this module directory. Unit tests verify configuration and UI resources; integration tests need desktop Electron/VS Code and current Runtime. Domain protocol fixtures do not run VM code. Review package:contents and the VSIX licenses; native acceptance, remote hosts and real application domains require their own reports.

```sh
npm ci
npm run typecheck
npm run build
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
```

Run desktop Extension Host suites one at a time. Simultaneous VS Code test windows can compete for the editor focus used by completion acceptance, undo and Enter cases.

Native argv tests allow 180 seconds per CLI call and avoid unused host macros, keeping argument forwarding independent of full GUI cold compilation. Macro configuration remains covered by dedicated configuration/protocol tests; real GUI compilation has its own acceptance gates. These test budgets do not change LSP or debug deadlines. Normalize JIT fixture paths before comparing the actual process arguments.

[Standards](../../../../../../../../Resources/Docs/Standards/en-US/GENERAL.md)



## Host integration and domain protocol

C++, UE and other hosts must provide a compatible TCP DAP endpoint and real bindings. The embedding example's RunDapTcp uses VeroScriptDapTcp::Run with Stdio.LiveBindings. The host owns binding lifetimes, execution threads and listener lifetime after disconnect. The plugin debugs the script VM; native code uses its corresponding debugger. Starting a new script session in a running host is supported; Domain attachment needs a host-prepared bridge; an already executing JIT VM cannot be taken over. Configure matching bindingCatalogPaths metadata for editing, and keep imports, macros and standard-library versions consistent with the host. Metadata does not register executable host callbacks in local Runtime. The command leaves compile options to the host; override them explicitly in launch.json when needed. Save host-only dependencies outside configured source/import scopes yourself. Connection addresses resolve on the extension host, including remote sessions.

The host must supply IVeroScriptLiveDebugDomain and declare veroscriptSupportsDomainAttach: true in initialize. Missing/false declarations stop attachment without falling back to program mode; the initial connection/initialize deadline is 10 seconds. Output records host/domain identity and opaque source/binding revisions; automatic source comparison and LSP synchronization are not provided. Variable editing follows veroscriptCanSetVariable, and unsupported pause/edit requests fail explicitly. Disconnect sends terminateDebuggee: false, and connection loss ends the session without automatic reconnect. The host owns cancellation, lifetimes and safe-point scheduling, prepares the bridge before execution and serializes debugged calls for the current single active context. Actual UE host integration requires separate validation.

Host flags also control stepping, expression evaluation and conditional breakpoints; omitted flags mean unsupported. The plugin updates the corresponding VS Code capabilities where available and rejects unsupported requests explicitly. Ordinary breakpoints remain available. Stop/disconnect still preserves host ownership of cancellation, even when the host reports veroscriptCanTerminate. Host `terminated` events still allow the editor’s final `disconnect` handshake with `terminateDebuggee: false`. Pending operations fail once, late replies and duplicate termination events are ignored, and a closed transport acknowledges the final disconnect locally.

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/Developer/en-US/workspace-and-debug.md)

## Memory inspection contract

Keep DAP `memoryReference` and `veroMemory` fields as opaque decimal strings, including values beyond JavaScript’s safe integer range. Variables inspection children are readonly and use stop-scoped handles. Forward host capabilities; do not infer support for `readMemory`/`writeMemory` from metadata. Integration tests cover native memory editing/run/check/debug and domain metadata forwarding; domain fixtures do not validate a real host VM.

## Client scheduling and bidirectional DAP

Editor event bursts share one reconciliation of the latest document state. Events arriving during reconciliation and explicit restart requests remain pending for the next pass, including after a failed pass. Domain DAP adds local events and renumbers outgoing messages; reverse-request replies must map `request_seq` back to the host’s original sequence, including out-of-order responses. Protocol fixtures verify this mapping without performing terminal launches or child debugging sessions.

## Native module-source integration

`ModuleSources` applies URI converters at the language-client boundary and calls the advertised `moduleSourceMethod`. Editor URIs retain their original wire URI, server lifetime and compile fingerprint; only the original URI is sent back. Readonly views never become source overlays or writable edit targets. Reject missing, retired, canceled or changed snapshots, including changes while awaiting a response. Module symbols and diagnostics stay with their owning client. Automatic restarts create a new source lifetime even when the client object and compile fingerprint are unchanged.

Set `VEROSCRIPT_TEST_MODULE_LSP` to the absolute `VeroScriptModuleEnvironmentToolingTests` executable to run the native `--lsp` fixture in Extension Host tests; an omitted fixture is skipped and cannot establish native module-environment acceptance. Default Runtime servers use their native startup options: this plugin adds no JSON configuration for native processors or automatic CLI package-environment injection. DAP `sourceReference` and frozen source responses continue through standard VS Code handling.

Native signature help owns annotation name matching, canonical spelling and parameter parsing, including multiline labels, nested generics and literals containing commas. Forward its results through the SDK. Integration tests change the same binding catalog’s `caseSensitive` policy without replacing the client, and apply `source.organizeImports` edits through VS Code while checking exact ranges, CRLF preservation and idempotence.

## Variable presentation and exact numeric types

Preserve each DAP variable's complete `presentationHint`, especially `attributes: ["readOnly"]`, through the SDK and domain bridge. `supportsSetVariable` and `veroscriptCanSetVariable` describe session operations; they do not make every variable writable. Native checks remain authoritative. Regression cases cover immutable `variant` children, nested payloads, watch children and editable ordinary locals, plus hint forwarding with both domain editing capability values.

Exact numeric types keep native names and textual values. Contextual TextMate tests cover `std::bigint`, `std::rational` and `std::numeric::rounding_mode` without reserving their leaf names as keywords. New or edited test cases remain unverified until type checking and actual Extension Host/VSIX gates run with matching tools; earlier reports do not validate newer native sources.

## Add-on tools API

`Public/api.d.ts` defines tools API 1. `getToolchain(scopeUri, sourceFile?)` resolves the same Runtime, compiler flags, environment, run backend/arguments and source scopes as the base extension. The optional source file expands file variables while the scope URI selects workspace settings. Resolution can probe the CLI version, so invoke it only for explicit execution. `compileArguments` contains no command, entry or execution backend; clients append their own command contract. GUI check/compile force VM and omit JIT modules. Keep `Public` declarations in VSIX staging; preserve existing `ready`/`snapshot` exports used by regression tests. Validate this API in a real Extension Host and installed VSIX using the matching Runtime; native GUI execution has its own acceptance gates.

The interpreter implementation lives in the separate BytecodeVm module. Preserve the complete packaged Runtime, including `Vm/`, in relocated editor fixtures. Native LSP/DAP startup supplies its default factory; this editor does not add a new wire field or infer a custom host factory. SDK regression cases cover inferred function-capture hover/completion/signatures/definitions, UTF-16 ranges and live receiver hover after an incomplete edit.

When relocating the native module fixture, also set `VEROSCRIPT_TEST_VM_BACKEND` to the absolute path of the matching native VM module in Runtime's `Vm/` directory. This overrides the fixture's build-time VM path; regular Runtime LSP/DAP programs retain their native default factory.


Regenerate file icons with the [shared icon generator](../../../../../../../../Resources/Assets/icons/README.md#regeneration). Builds copy the [shared 256px product PNG](../../../../../../../../Resources/Assets/icons/veroscript-icon-256.png) into the extension display icon.

Use the publisher declared in `package.json`; the GUI dependency must match the base extension identity. Ship the root README as the extension Details page, without links to excluded repository files. Only `extension/readme.md` is exempt from the functional package documentation filter. Marketplace publication requires access to that registered publisher and completed acceptance gates; a manifest change does not register the publisher.

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
