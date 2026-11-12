# WebAssembly/WASM SDK

`VeroScriptWebAssembly` is a UI-free WebAssembly SDK for browser and other JavaScript
hosts. It provides in-memory source checking and execution. It does not
include HTML, CSS, an editor, or a UI framework; the host is responsible for
rendering output and diagnostics.

<a id="runtime-factory-configuration"></a>

The TypeScript factories copy the caller’s configuration before creating an Emscripten instance. The same options object, including a frozen object, can be reused for independent runtimes without their exports, heaps or ownership being shared. Declared `locateFile` and `wasmBinary` options are preserved even when inherited.

## Artifacts and deployment

Deploy the generated runtime and its declarations as one matched set:

| Artifact | Purpose |
| --- | --- |
| `veroscript-engine.mjs` | Emscripten module with interactive-input support. |
| `veroscript-engine.wasm` | WebAssembly binary for the regular runtime. |
| `veroscript-engine-fast.mjs` | Emscripten module for non-interactive low-latency runs. |
| `veroscript-engine-fast.wasm` | WebAssembly binary for the fast runtime. |
| `veroscript-webassembly.ts` | TypeScript wrapper source. |
| `veroscript-webassembly.d.ts` | TypeScript declarations for the wrapper and WebAssembly C ABI. |
| `veroscript-engine.d.mts` and `veroscript-engine-fast.d.mts` | Emscripten module-option declarations. |

The standard library and localization catalogs are embedded in the generated
module's virtual filesystem. Imports such as `import "string.vs"` therefore do
not require a separate standard-library download.

The TypeScript entry point loads the selected Emscripten engine on demand.
Importing the wrapper does not download either engine; choosing the fast
runtime loads only the fast engine, and choosing the regular runtime loads only
the interactive engine.

The WebAssembly SDK does not contain the desktop CLI, the Interop shared library, or
UI resources. If building it yourself, use the Emscripten SDK and keep the
`.mjs`, `.wasm`, declaration, and ABI versions together. Generated files are
release artifacts and should not be hand-edited.

## Choosing a runtime

Use `createVeroScriptRuntime` when a script may pause for asynchronous input.
Use `createFastVeroScriptRuntime` for checking and ordinary runs where all
input is already available:

```TypeScript
import {
	createFastVeroScriptRuntime,
	createVeroScriptRuntime,
} from "./veroscript-webassembly";

const runtime = await createFastVeroScriptRuntime();
console.log(runtime.supportsInteractiveInput); // false
runtime.dispose();

const interactive = await createVeroScriptRuntime();
console.log(interactive.supportsInteractiveInput); // true
interactive.dispose();
```

The fast runtime omits whole-program Asyncify instrumentation. It cannot run
`runSourceInteractive`; calling that method is an error. The regular runtime
supports asynchronous `readLine` callbacks and keeps the script suspended
until the returned promise resolves.

## Execution backends

`runSource` and `runSourceInteractive` accept `vm`, `mixed`, or `jit`:

The published modules select no JIT backend. `vm` interprets bytecode; `mixed` remains the default and also executes through the VM. `jit` returns the structured diagnostic `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory` before executing application initializers or `main`, or requesting input. Existing callers that used `jit` must select `vm` or `mixed`. The policy names and C ABI IDs remain stable; `result.backend` records the requested policy, and all JIT counters are zero.

## TypeScript API

The wrapper exports the following public types and functions:

| API | Meaning |
| --- | --- |
| `VeroScriptDiagnosticSeverity` | `"error"`, `"warning"`, `"info"`, or `"hint"`. |
| `VeroScriptExecutionBackend` | `"vm" \| "mixed" \| "jit"`. |
| `VeroScriptLanguage` | Open string type containing a canonical BCP 47 tag. |
| `VeroScriptDiagnostic` | Severity, message, code, file, and one-based line and column. |
| `VeroScriptResult` | `abiVersion`, `operation`, `success`, `exitCode`, `exitRequested`, `backend`, `output`, `errorOutput`, `diagnostics`, optional `programHandle`, `timings`, and `jit`. |
| `VeroScriptJitStatistics` | `compileAttempts`, `compiledEntries`, `bridgeTrampolineEntries`, `linearTrampolineEntries`, `vmTrampolineEntries`, `unsupportedEntries`, `dispatchAttempts`, `dispatches`, `fallbacks`, `hotFunctions`, and `directNativeCalls` for one run. `directNativeCalls` counts generation-scoped native calls that bypass the VM value bridge. |
| `VeroScriptRuntime.supportsThreads` | Whether the artifact advertises script-level pthread-backed concurrency. The published browser artifacts return `false`. |
| `VeroScriptRuntime.supportsCompiledPrograms` | Whether the artifact supports compile-once, run-many program handles. Current published artifacts return `true`. |
| `VeroScriptVirtualFile` | `{ path: string; source: string }` import overlay. |
| `VeroScriptSourceOptions` | Optional `fileLabel` and virtual `files`. |
| `VeroScriptRunOptions` | Source options plus `input`, `args`, and `backend`. |
| `VeroScriptInteractiveRunOptions` | Source options plus `args`, `backend`, and `readLine`. |
| `VeroScriptInputRequest` | Zero-based input request index passed to `readLine`. |
| `VeroScriptProgram` | A compiled program with `disposed`, `run`, `runInteractive`, and `dispose` operations. |
| `VeroScriptCompileResult` | The compile result and an optional `VeroScriptProgram`; failed compilation never returns a program. |
| `VeroScriptProgramRunOptions` | Per-run `input`, `args`, and `backend` for a compiled program. |
| `VeroScriptProgramInteractiveRunOptions` | Per-run `args`, `backend`, and `readLine` for an interactive compiled-program run. |
| `VeroScriptRuntime` | Runtime state, language selection, checking, running, and disposal. |
| `VeroScriptEngineModuleOptions` | Emscripten module options forwarded by the factories: `locateFile(path, prefix)` or a preloaded `wasmBinary`. |
| `createVeroScriptRuntime(options?)` | Creates a runtime with interactive-input support. |
| `createFastVeroScriptRuntime(options?)` | Creates a non-interactive low-latency runtime. |
| `runtime.compileSource(source, options?)` | Compiles source and virtual files once for repeated runs. |

The normal execution pattern is:

```TypeScript
import { createVeroScriptRuntime } from "./veroscript-webassembly";

const runtime = await createVeroScriptRuntime();
try {
	runtime.setLanguage("zh-CN");
	const result = runtime.runSource(
		`int main(string[] args)
{
	print("argument count={}", len(args));
	return 0;
}`,
		{
			fileLabel: "main.vs",
			args: ["alpha", "beta"],
			backend: "mixed",
		},
	);
	console.log(result.output, result.diagnostics);
}
finally
{
	runtime.dispose();
}
```

Bound method calls and other native reentry into the VM are subject to the core's platform stack protection. Excessive nesting returns `diag.call_stack_overflow`. This protection is separate from the call-depth limit for ordinary interpreted recursion. Reduce native reentry nesting before retrying.

## Compile once, run many

For repeated runs of unchanged source, call `compileSource` once and reuse the
returned program. This avoids repeating parsing, import resolution, semantic
analysis, and bytecode generation:

```TypeScript
const compiled = runtime.compileSource(
	`int main(string[] args)
{
	return len(args);
}`,
	{ fileLabel: "main.vs" },
);

if (compiled.program) {
	const first = compiled.program.run({ args: ["one", "two"], backend: "mixed" });
	const second = compiled.program.run({ args: ["one"], backend: "vm" });
	console.log(first.exitCode, first.timings?.executeMs, first.jit?.dispatches);
	console.log(second.exitCode, second.timings?.executeMs);
	compiled.program.dispose();
}
```

Compilation accepts the same `fileLabel` and virtual `files` overlay as source
checking, but not run-time `input`, `args`, or `backend`. Each subsequent
`program.run` receives fresh execution state and may choose its own input,
arguments, and backend. The compiled bytecode is shared; the VM state and JIT
statistics are per run.

The regular runtime also supports `program.runInteractive({ args, backend,
readLine })`. The fast runtime does not support interactive execution. Use the
`supportsCompiledPrograms` and `supportsInteractiveInput` capability flags when
working with artifacts whose declarations may come from a different release.
Call `program.dispose()` when the handle is no longer needed; disposing the
runtime releases all remaining program handles.

Factory `options` are forwarded to the generated Emscripten module. Use
`locateFile(path, prefix)` when the `.wasm` file is served from a custom URL, or
`wasmBinary` when the binary has already been loaded as an `ArrayBuffer` or
`Uint8Array`.

`checkSource` validates source without invoking `main`. `runSource` checks and
runs source. Both `void main(string[] args)` and `int main(string[] args)` are
valid entry points. A successful non-zero `int main` keeps `success` true and
is reported through `exitCode`. The wrapper rejects NUL characters and
validates the returned ABI JSON before exposing it to the host.

## Results and exit state

`VeroScriptResult.success` reports whether compilation and execution completed
normally; it is not equivalent to `exitCode === 0`. A successful `int main`
may return a non-zero exit code. `exitRequested` is true only when the script
explicitly called `exit(n)`; an ordinary `int main` return does not set it.

`backend` contains the selected backend for a run and is `null` for checks or
API errors. A successful compilation has `operation: "compile"` and a non-null
`programHandle`; a failed compilation has no program. `timings.compileMs`
measures compilation and `timings.executeMs` measures a program run. The
optional `jit` object reports JIT activity for that run; the published modules have
zero JIT counters. `output` and `errorOutput` contain captured text.
Diagnostics use one-based source coordinates.

## Language selection

Web runtimes start with deterministic `en-US`. Use the catalog metadata rather
than hard-coding a language list:

```TypeScript
const languages = runtime.availableLanguages;
runtime.setLanguage("ja-JP");
console.log(runtime.language);
```

Only catalogs embedded in the current Web build can be selected. The selected
language is reapplied before each wrapper operation. The underlying locale
state is process-wide within a Wasm module, so do not concurrently use two
runtimes in one module with different language requirements; use separate Web
Workers for isolation.

## Virtual imported files

`files` supplies unsaved modules for one check or run. The paths and sources
must form paired entries, and the overlay does not write to a real filesystem:

```TypeScript
const result = runtime.runSource(
	`import "helper.vs";

int main()
{
	return answer();
}`,
	{
		fileLabel: "main.vs",
		files: [{
			path: "helper.vs",
			source: `export int answer()
{
	return 42;
}`,
		}],
	},
);
```

## Interactive input

Use the regular runtime and `runSourceInteractive` when the script uses
`read_line`, typed input, or `pause`:

```TypeScript
const result = await runtime.runSourceInteractive(source, {
	args: [],
	readLine: async ({ index }) => terminal.readLine(index),
});
```

Return `null` for end-of-file. Only one interactive run may wait on a Wasm
module at a time. The TypeScript wrapper rejects other operations and disposal
while input is pending. At the raw C ABI level, the whole module is guarded:
create and text getters return null, stateful text operations return an API
failure, numeric getters and program operations return zero, and destroy is
ignored until the interactive call completes. Memory operations return Busy (9),
memory result getters return zero, and lease release remains available.

An input-bridge allocation failure or a line exceeding the Wasm32 allocation range returns a failed API result. The module remains available for subsequent runs; `null` still signals normal EOF.

## Host and page boundaries

Ordinary `runSource` and `checkSource` are synchronous after module creation.
Run them in a Web Worker so an unbounded script cannot block the page's main
thread; terminate the Worker to stop such a script. Reuse one runtime instead
of creating a WebAssembly module for every request.

The current browser baseline does not promise script-level `spawn` as a
cross-browser concurrency API. Browser pthreads are not required, so the
published integration does not need COOP/COEP headers. `supportsThreads` is
`false` for both published artifacts. Channel, mutex, semaphore, and
synchronized-value operations that do not spawn a task remain available in one
execution; attempting to spawn reports `diag.failed_to_spawn_task_thread`.
Run independent programs in separate Web Workers. Async input is supported
only by the regular runtime. For the step-by-step interactive-input workflow,
see the [WebAssembly SDK
interactive-input tutorial](../../../../../../../Resources/Docs/Tutorials/en-US/toolchain/16-web-interactive-input.md).

## C++ facade and C ABI

Successful source runs through `FRuntime` also expose the same JIT statistics
as the JSON result, including `directNativeCalls`.

WebAssembly code can use `VeroScript::WebAssembly::FRuntime`, including language
selection, source checking, source execution, virtual files, input streams,
backend names, and JSON result conversion. The current C++ facade exposes
source check/run operations directly; compile-once program ownership is exposed
by the C ABI and TypeScript wrapper.

Hosts that do not use the TypeScript wrapper can include `VeroScriptWebAssemblyC.h`.
The C ABI also exposes `vero_web_compile_source_with_context`,
`vero_web_run_program`, `vero_web_run_program_interactive`, and
`vero_web_destroy_program`. A compiled program handle belongs to its runtime,
can be run with fresh state, and must be released before the runtime is
destroyed. Backend IDs are `0 = VM`, `1 = Mixed`, and `2 = JIT`. The language
string remains valid until a successful language change or runtime destruction.
The available-language JSON string remains valid until the next
available-language query or runtime destruction. Other returned JSON is owned
by the runtime until the next API call that returns runtime-owned text or
destruction; input strings and arrays are borrowed only for the duration of the
call.

For the lower-level cross-language process ABI, see the [Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md).

## Managed memory

Host API: `runtime.memory` / `runtime.memory.setLimits` (bigint). See the [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md) reference for ownership, quotas, view expiry and leases.

<a id="managed-memory-contract"></a>

The host facade supports buffers, slices, copies, scalar reads/writes, snapshots and explicit leases. Sizes, offsets, quotas, IDs and 64-bit integers use `bigint`; scalar reads/writes accept little or big endian. `fromArray` takes an owned snapshot, including Node.js `Buffer` inputs, and `toArray` returns a copy. Dispose regions when finished. A lease pins its backing storage, prevents resize and remains releasable after runtime disposal. `withBytes` accepts only synchronous callbacks: do not release that lease, retain writable views or grow the Wasm heap inside the callback. Read-only leases expose copies; writable views are rebuilt between callbacks after heap growth. Stateful memory calls are rejected while interactive input is active.

Host regions and script sessions have separate quotas, initially 16 MiB per buffer and 32 MiB retained in each context. `runtime.memory.setLimits` changes only host-region quotas; zero means unlimited. These quotas cover managed byte storage, not the entire Wasm heap or a complete sandbox.

## Core compatibility and diagnostics

<a id="compiler-diagnostics"></a>

The embedded compiler prepares cross-file declarations and accepts structured annotations; unknown annotations remain metadata. The browser SDK does not register native annotation processors. Import `delegate.vs` for `std::delegate` and `std::multicast_delegate`, whose members use lowercase names. Diagnostics preserve `error`, `warning`, `info` and `hint`; successful results may contain non-error diagnostics. Source execution retains compilation diagnostics, while reusable programs expose them in the compile result. The bytecode uses the fixed `VSBC` identifier and product version `0.1.1`; rebuild packages and caches from older development builds.

## Interactive ownership

Interactive calls recheck the module after reading options. Reentrant or disposed requests are rejected before taking ownership of the input provider, preserving an active run and its remaining input.

<a id="input-provider-failures"></a>

SDK input-provider errors and rejected promises abort the current execution and reject with the original error; invalid result types and NUL-containing lines reject with `TypeError`. Failure resumes the native stack and performs cleanup, so it cannot be mistaken for EOF followed by ordinary script execution. The runtime and compiled program remain reusable. `null` still means normal EOF. Unmarked raw-module providers retain their existing EOF fallback for rejected or invalid input.

## Console output and capabilities

<a id="console-capabilities"></a>

`std::console::write` appends text without a newline to `output`; `flush` flushes standard output. `std::console::error` uses the host’s diagnostic line channel: an unterminated final line gains a newline in `errorOutput`. The SDK has no terminal host: capability queries return `false`, dimensions are zero, and valid color, cursor and clearing operations return `false` without escape sequences or deletion of captured text. Async input support is independent of terminal capabilities.

Invalid color names, clearing modes, cursor coordinates, argument types or argument counts still produce diagnostics before unsupported-terminal fallback. The runtime remains reusable after rejection. Captured text is returned in `VeroScriptResult` when execution completes.

## Current language features

<a id="compiler-and-application-services"></a>

Both engines support lowercase `pair<T, U>` / `std::pair`, `std::attribute<T>`, `std::bind_method` and the embedded `attribute.vs`, `callable.vs`, `i18n.vs` and `compiler.vs` modules. Application `std::i18n::context` supports literal text, typed named formatting and independent snapshots; its language does not change the SDK diagnostic language. The SDK supplies no application resource or formatter providers.

`import compiler "builder.vs";` can load a script compile extension from `files` during `checkSource`, `compileSource` or `runSource`. Checking therefore can execute extension code even though it does not call application `main`. Core applies its default extension limits and capability policy. Use a Web Worker when compilation must remain interruptible by the frontend. Extension failures produce diagnostics and leave the runtime reusable.

## Interactive stack workspace

Interactive input can suspend inside nested bound-method calls. Deep native reentry remains subject to the host call stack; the fast engine requires input to be supplied before execution.

## Annotation name matching

<a id="annotation-name-matching"></a>

Annotation names are case-sensitive by default. A script compile extension can set `annotation_definition.case_sensitive = false` before registration to select locale-independent ASCII case folding for that definition. Processor subscriptions and reflection through `has_annotation`, `find_annotations` and `annotation_info.matches_name` follow the policy. `name()` and `raw_text()` preserve source spelling; `definition().case_sensitive()` reports the policy. For `@Name` annotations, `find_annotations` and `matches_name` require the `@` prefix; `has_annotation` permits it to be omitted. Argument names, metadata keys and unregistered annotations remain case-sensitive.

## Core execution and limits

<a id="numeric-and-library-limits"></a>

Core supplies `std::bigint` and `std::rational` through `bigint.vs`, `rational.vs` and `core.vs`. They preserve exact integer and rational values inside the VM; use `to_string()` when exporting exact text. They are separate from JavaScript `bigint` used for fixed-width Wasm ABI arguments. Nested generic types, inherited nested types and local types follow Core scope and access rules.

`runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts)` accepts unsigned 64-bit `bigint` limits for per-operation text bytes, bulk-read bytes and split parts. Zero adds no host limit; omitted arguments reset to zero. Changes apply to subsequent source and interactive runs and the next run of existing compiled programs. Changes are rejected while awaiting interactive input. These limits are separate from managed-memory quotas and Core numeric limits; they do not bound the entire Wasm heap. The Web API uses Core's finite default numeric limits.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
