# Debugging

The VeroScript debugger uses the Debug Adapter Protocol (DAP) and executes scripts through the VM. Debugging and hot-reload sessions do not use the JIT execution path.

## Standard-input/output mode

```powershell
veroscript --debug script.vs
```

This mode transports DAP messages over standard input and output, which is suitable for an editor or another debug client that launches and manages the process.

## TCP mode

```powershell
veroscript --debug --dap-port=4711 script.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 script.vs
```

`--dap-port` starts a TCP DAP service on a port from `1` to `65535`. `--dap-bind` selects the bind address and defaults to `127.0.0.1`. Bind to a non-loopback address only when an external client genuinely requires it.

`veroscript-dap` is a thin alias for the same debug host. IDE F6 launch and attach sessions also use DAP; when a session disconnects, the client can choose whether to terminate the debuggee with `terminateDebuggee`.

## Session lifecycle and launch inputs

A client must send `initialize` before `launch` or `attach`. The adapter returns
capabilities including `supportsTerminateRequest`, together with `name`,
`version`, `platform`, and `jitCapability` metadata. The wire host emits the
`initialized` event after the `initialize` response.

The adapter currently reports these DAP capabilities:

| Capability | Value |
| --- | --- |
| `supportsConfigurationDoneRequest` | `true` |
| `supportsStepInRequest` | `true` |
| `supportsStepOutRequest` | `true` |
| `supportsStepBackRequest` | `false` |
| `supportsConditionalBreakpoints` | `true` |
| `supportsEvaluateRequest` | `true` |
| `supportsSetVariable` | `true` |
| `supportsTerminateRequest` | `true` |

`jitCapability` identifies the core JIT service contract (currently
`backend-services`), not an installed or selected native backend.

Domain attach uses `attachMode: "domain"` and `domainId` to connect a
host-prepared persistent runtime across successive script calls. Clients must
first require `initialize.body.veroscriptSupportsDomainAttach` to be exactly
`true`; otherwise reject this mode without falling back to loading a program.
`configurationDone` activates the connection without loading or starting scripts.
Use the attach response for domain-specific capabilities. Default disconnect
resumes paused execution according to host policy and leaves the host alive.
This cannot take over a VM already executing JIT code. See the
[domain contract](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/session-debugging.md).

The program-loading flow below applies to `launch` and program-mode `attach`
(`attachMode: "program"` or omitted). Both load a program through the same debug loader. `attach`
is the request used when an IDE connects to an embed or TCP host that owns live
bindings; it does not attach to a VM that is already executing. After setting
breakpoints, send `configurationDone` to start execution.

The supported `launch` and `attach` arguments are:

| Field | Meaning |
| --- | --- |
| `program` | Script path. The CLI debug command can provide it as a default. |
| `source` | Inline source text. When non-empty, it takes precedence over `program`. |
| `fileLabel` | Display label and source identity for inline `source`. |
| `workspace` | Base directory for relative launch paths. |
| `cwd` | Working directory for the script and relative path resolution. `workingDirectory` is an accepted alias. If omitted, `workspace` is used when available. The directory must exist. |
| `args` | String array passed to `main(string[] args)`. The script path is not included. |
| `input` | Initial script standard input. It is independent from the DAP protocol stream. |
| `env` | Environment overrides for the debug run. An object is canonical; `environment` is an alias, and an array of `{ "name", "value" }` entries is also accepted. |
| `stdlibSearchPaths` | Ordered standard-library search roots. `stdlibPath` is accepted as a single-root alias. |
| `importPaths`, `implicitImportPaths` | Ordinary and implicit module search paths. |
| `defines`, `predefinedMacros` | Preprocessor definitions and host-style predefined macros. |
| `sourceOverlays` | In-memory source files used when resolving imported modules. |
| `bindingCatalogPaths` | Ordered binding-catalog files used as metadata/stub bindings. An embed host's `LiveBindings` takes precedence when supplied. |
| `allowCircularImports`, `allowProjectWideAccess` | Import and project-visibility policies. |
| `stopOnEntry` | Stops at the entry point before normal execution continues. |
| `backend`, `maxCallDepth` | Backend preference and call-depth limit. Debug execution still uses the VM path. |

Relative `program`, import, standard-library, implicit-import, overlay, and
binding-catalog paths are resolved from `cwd` when it is set, otherwise from
`workspace`. When a workspace is supplied without `cwd`, the workspace also
becomes the execution working directory. Environment overrides are scoped to
the debug run and restored afterward. A live-binding attach cannot combine
environment overrides or a working-directory override.

The CLI accepts trailing script arguments and forwards them as defaults for the
DAP request:

```powershell
veroscript --debug hello.vs red blue
veroscript --debug --dap-port=4711 hello.vs red blue
```

An explicit DAP `args` array replaces those defaults. A launch is rejected when
it contains more than 4096 script arguments, more than 2048 entries in any of
the three module-path arrays, or more than 1024 environment entries. Environment
names must be non-empty, must not contain `=`, and must not contain NUL; values
must not contain NUL.

## Asynchronous execution and controls

For stdio and TCP wire hosts, `configurationDone` is acknowledged before the
VM finishes and the VM runs on an execution worker. The protocol reader remains
available for control and stopped-state requests. A `stopped` event reports one
of `entry`, `breakpoint`, `step`, or `pause`; `pause` is generated when a
cooperative pause interrupts a running VM.

`continue`, `next`, `stepIn`, and `stepOut` are valid only after `stopped` and
resume that execution. `stackTrace`, `scopes`, `variables`, `evaluate`, and
`setVariable` are also valid only while stopped. Set breakpoints before
`configurationDone` or while stopped; changing them during execution is
rejected. `pause` is valid only while the VM is running and requests a
cooperative stop. `terminate` requests cooperative cancellation. A client
should wait for the resulting `exited` and `terminated` events rather than
assuming that the request itself ends execution.

The `threads` request exposes one logical VM thread, with id `1` and name
`main`. This is the debugger's execution view; the protocol and execution
worker threads used by a wire host are not exposed as script threads.

`disconnect` defaults `terminateDebuggee` to `true` for `launch` and `false`
for `attach`. In a multi-session TCP host, `terminateDebuggee=false` ends the
current client session while leaving the listener available; `true` stops the
host listener. The `terminate` request always cancels the debug run and asks
the host to stop.

## TCP host and embedding

The CLI TCP host serves one client session by default. SDK hosts can use
`VeroScriptDapTcp::Run` with `FDapTcpRunOptions`: `MaxSessions=0` serves clients
until host termination, `Port=0` selects an ephemeral port and reports it via
`OnListening`, and `BindAddress` should normally remain `127.0.0.1`. An embed
host can set `FDapStdioRunOptions::LiveBindings` so an `attach` session executes
registered host functions rather than catalog stubs. The live catalog must
outlive the DAP run. Set `bAnnounceListen=false` when the embed host provides its
own readiness reporting; the default listener announcement writes to the host's
error stream. `VeroScriptDapStdio::Run` returns `0` for a clean session, `1` for
a protocol or I/O failure, and `2` when the client requests host termination.
For `VeroScriptDapTcp::Run`, a client-requested host termination is a clean
return (`0`); listen, accept, and other non-clean session failures return
non-zero.

## Breakpoint support

The adapter supports source line breakpoints through `setBreakpoints`.
Exception breakpoints and function breakpoints are not supported. An empty
`setExceptionBreakpoints` request succeeds only as a clear operation for the
standard client launch handshake; non-empty exception filters and all
`setFunctionBreakpoints` requests return a failed DAP response. Clients should
not show those breakpoint classes as active.

`setBreakpoints` uses one-based physical source lines. Before `launch` or
`attach` has loaded the debuggee, a returned breakpoint is `verified: false`
and pending. Once code is loaded, the adapter binds it and emits a standard
`breakpoint` event with `reason: "changed"` and the same DAP breakpoint ID.
After loading, a breakpoint is verified only when its path resolves to one
compiled source, its requested line is active after preprocessing, and an
executable location can be found. It never falls back to matching a file by
basename. Failed bindings remain unverified and carry a localized `message`.

For a non-executable physical line within a function, the adapter can report
the next executable physical line in that function as the bound line; clients
must use the returned `line`. A module-initializer breakpoint requires an exact
physical line. Lines removed by preprocessing, out-of-range lines, and sources
that are absent or ambiguous do not bind.

Other unsupported DAP commands return a failed response with a localized error
message. Set a BCP 47 `locale` in the DAP `initialize` arguments to select the
display language. Clients should branch on the DAP success/error fields, not on
translated message text.

## Output during a debug run

`std::console::write` and `std::console::error` write without adding a
newline. `std::console::flush()` makes pending standard output visible
immediately. The DAP adapter forwards flushed standard output and error output
as separate `output` events while the program is still running; the final run
result also keeps the two streams separate. This lets an editor display
progress output without waiting for `exited`.

## Execution constraints

The debugger uses the full VM debugging path for breakpoints, stepping, and variable inspection. Requesting `--backend=mixed` or `--backend=jit` does not switch the current debug execution to JIT; ordinary runs can restore the requested backend after debugging ends.

See [Host integration](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md) for finer-grained debugger and host-binding integration.

## Memory inspection

While paused, expand an object or memory region to inspect read-only metadata such as `[object_id]` and `[byte_length]`, with addresses shown only when permitted. IDs identify live objects; addresses are transient observations. Inspector metadata cannot be assigned. DAP `variablesReference` is a protocol handle, and arbitrary `readMemory` / `writeMemory` requests remain unsupported.

## Source snapshots in debug frames

When a stack frame supplies a positive `sourceReference`, the client should request its text with DAP `source`. The returned text is the compiled snapshot for inline, generated or in-memory code, including a source overlay whose disk file is older or absent. Display it read-only; changing a similarly named disk file does not update the running program. Captured source text remains readable after resume, while stack-frame and variable handles expire. Reloading or clearing the program invalidates old source references; obtain the current frame again.

## Native debug-host lifecycle

Serialize lifecycle operations on the owner thread. While Run is active, including its callbacks, LoadSource/LoadFile and a nested Run are rejected without replacing the active program; Clear does nothing. Call Clear after Run returns or unwinds. Run snapshots script arguments: changes in a callback apply to the next run. Replacing/clearing SetStopHandler or the DAP event handler affects later notifications; the active callable and its captures stay alive until it returns. This does not permit concurrent access.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](debugging.md) · [fr-FR](../fr-FR/debugging.md) · [ja-JP](../ja-JP/debugging.md) · [ko-KR](../ko-KR/debugging.md) · [zh-CN](../zh-CN/debugging.md) · [zh-TW](../zh-TW/debugging.md)
