# WebAssembly runtime and bindings

## Runtime factories

`createVeroScriptRuntime()` is the Asyncify-capable factory for interactive
input. `createFastVeroScriptRuntime()` omits whole-program Asyncify stack
instrumentation and is the preferred path when all input is supplied before the
run. Both factories expose the same language and result contract where the
capability flags permit it.

`runSource` is synchronous after module creation. `runSourceInteractive` keeps
VM/script state suspended while an input provider awaits a logical line. Only
one interactive run may be active on one Wasm module. A provider exception
rejects the promise after the runtime leaves its input wait safely.

See [WebAssembly/WASM SDK](../../User/en-US/index.md#runtime-factory-configuration).

## Inputs and results

Runs accept a file label, optional virtual files, standard input, script
arguments, and a selected `vm`, `mixed`, or `jit` backend. `void main(string[]
args)` and `int main(string[] args)` are supported. A non-zero integer result is
still a successful run and is returned as `exitCode`. Embedded NUL input is
rejected.

The browser baseline does not advertise script-level worker threads. An
unbounded program is stopped by terminating the owning Web Worker. Independent
programs should use independent runtime modules or workers.

See [WebAssembly ABI, artifacts, and release contract — Program and input capabilities](release-contract.md#web-execution-policy).

Executable field and method reflection runs in the `vm` and `mixed`
backends. Callable task spawning, like legacy task spawning, is unavailable in
the published non-pthread browser modules. The `is_file`, `is_directory`, and
`remove_dir` helpers access only the module's virtual filesystem, including
embedded standard-library files; they cannot access a visitor's local files.

## Compiled programs

`compileSource` accepts the source file label and in-memory virtual files and
returns a result plus an optional owned `VeroScriptProgram`. The program handle
can be run repeatedly with new input, arguments, and backend selection without
reparsing the source graph. Each run has fresh execution state; compilation
diagnostics remain in the compile result.

`VeroScriptProgram.dispose()` releases the native handle and is idempotent at the
SDK boundary. The runtime tracks live programs and releases them during runtime
disposal. Interactive compiled execution requires the Asyncify artifact, a
`readLine` provider, and no other active interactive run on that WebAssembly
module.
## Complete source and compiled-program surface

The TypeScript SDK maps runSource to vero_web_run_source_with_context,
including script arguments, the selected backend, and paired in-memory virtual
files. compileSource maps to vero_web_compile_source_with_context; a successful
result owns a VeroScriptProgram. VeroScriptProgram.run uses
vero_web_run_program, and runInteractive uses the Asyncify-only
vero_web_run_program_interactive. A program run receives fresh VM state and
may select its own input, arguments, and backend.

The native C++ surface has the corresponding FRuntime::RunSource,
RunSourceWithInputStream, and CheckSource operations, plus SetLanguage and
GetAvailableLanguages. The C ABI additionally exposes argument, option,
context, compile, program, interactive, and context-check entry points. C ABI
arrays and strings are borrowed for the duration of their call; returned JSON
and result pointers follow the runtime-owned lifetime rules.
VeroScriptResult keeps backend, output, error output, diagnostics, timings,
program handle, and per-run JIT statistics as separate fields.

## Result and JIT field mapping

`VeroScriptResult.jit` is null for check, compile, API errors, or a source run that fails to load. A loaded run returns per-run counters even when execution is rejected before script initialization, as with an explicit `jit` request without a backend. All counters are zero in the published modules. `directNativeCalls` and `compileAttempts` are statistics, not language or ABI version values.

## Managed memory

See [WebAssembly/WASM SDK — Managed memory](../../User/en-US/index.md#managed-memory-contract).

## Failure cleanup and snapshots

Input-bridge allocation failure and Wasm32 size overflow resume the native stack and return a failed API result, preserving the module for subsequent runs. Fault injection covers both source and compiled execution with `vm` and `mixed`, including recovery after each failure. Normal EOF retains its existing behavior.

Failure cleanup preserves ownership: the interactive input allocation is released even if copying its line throws, and a compiled program is removed if its result JSON cannot be produced before its handle reaches the caller. Read-only leases create one owned snapshot; `toArray` returns that snapshot without a second full copy. Regressions cover independent snapshot lifetime and fresh managed-memory globals after a failed compiled run.

## Core compatibility and diagnostics

See [WebAssembly/WASM SDK — Core compatibility and diagnostics](../../User/en-US/index.md#compiler-diagnostics).

## Interactive ownership

Interactive calls recheck the module after reading options. Reentrant or disposed requests are rejected before taking ownership of the input provider, preserving an active run and its remaining input.

String pointer tables are allocated from the actual marshalled item count, so custom or changing array iteration cannot write beyond an allocation based on the initial length. Both synchronous and asynchronous helpers validate the Wasm32 bound. Runtime disposal visits tracked programs without creating a second array.

See [WebAssembly/WASM SDK — Interactive ownership](../../User/en-US/index.md#input-provider-failures).

## Console output and capabilities

See [WebAssembly/WASM SDK — Console output and capabilities](../../User/en-US/index.md#console-capabilities).

Invalid color names, clearing modes, cursor coordinates, argument types or argument counts still produce diagnostics before unsupported-terminal fallback. The runtime remains reusable after rejection. Captured text is returned in `VeroScriptResult` when execution completes.

## Current language features

See [WebAssembly/WASM SDK — Current language features](../../User/en-US/index.md#compiler-and-application-services).

The published C ABI and TypeScript options do not expose `FModuleEnvironment`, native extension registration, resource providers or `FEngineConfig::I18nServices`. Core supports static native registration on WebAssembly for custom C++ builds; desktop dynamic libraries cannot be loaded into the published engines. These host integrations require a separately configured artifact. Existing source overlays and program ownership retain their contracts.

## Annotation name matching

See [WebAssembly/WASM SDK — Annotation name matching](../../User/en-US/index.md#annotation-name-matching).

Conflicting annotation registrations with overlapping accepted names produce a compilation diagnostic. Both engines apply the same policy during checking, source execution and compiled-program creation. Compiled programs retain their definition policies; recompile to change them. The Web C ABI and TypeScript options are unchanged. Rebuild older development packages and caches after source-metadata contract changes; SDK program handles remain in-memory.

## Core execution and limits

See [WebAssembly/WASM SDK — Core execution and limits](../../User/en-US/index.md#numeric-and-library-limits).

Repeated compiled-program execution may reuse internal workspace while each run starts with fresh script globals, locals and arguments, including after failure. Rebuild both Wasm variants and rerun the existing SDK release gates after Core changes. Earlier SDK verification records cover their recorded artifacts only.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](runtime-and-bindings.md) · [fr-FR](../fr-FR/runtime-and-bindings.md) · [ja-JP](../ja-JP/runtime-and-bindings.md) · [ko-KR](../ko-KR/runtime-and-bindings.md) · [zh-CN](../zh-CN/runtime-and-bindings.md) · [zh-TW](../zh-TW/runtime-and-bindings.md)
