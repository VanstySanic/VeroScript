# WebAssembly build and artifact contract

## Emscripten modules

The WebAssembly CMake target links `VeroScriptWebAssembly` to the core and creates two executables
from the shared module entry point: `veroscript-engine` with interactive input
support and `veroscript-engine-fast` without it. Both embed `stdlib/` and
`localization/` into the virtual filesystem.

The generated modules export allocation functions, WebAssembly ABI version and thread
capability queries, create/destroy, language selection, catalog discovery,
source run/check, source options, and interactive-context entry points. The
export list is part of the generated SDK contract.

## SDK source generation

The build copies the TypeScript source and declarations into the WebAssembly output
directory. Native public headers, generated TypeScript declarations, and parity
tests must agree. A generated file must not be hand-edited to hide a mismatch.

From the repository root, regenerate declarations with the SPDX notice and Tab indentation using the command below. Then run `npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run typecheck` to verify parity.

```sh
npm --prefix Extensions/Platform/WebAssembly/VeroScriptWebAssembly run generate:declarations
```

## Release checks

Artifact audit requires the complete interactive/fast module set, generated
declarations, and SDK source. Browser smoke tests exercise headless execution,
interactive input, event-loop behavior, Web Worker execution, and worker
termination. Size budgets are release review limits, not ABI limits.

## Export and declaration parity

The generated Emscripten export list must include the thread-capability query,
language and catalog queries, source option/context calls, compiled-program
creation/execution/destruction, interactive program execution, and context
checking. The TypeScript binding must feature-detect additive compiled-program
exports so artifacts from before that capability remain usable without
inventing unsupported operations.

Browser tests must cover borrowed virtual-file arrays, program-handle reuse and
cleanup, fresh state per program run, interactive input while a program is
suspended, language discovery and selection, result-pointer lifetime, and the
thread-capability branch. None of these checks alter the language or bytecode
version contract.

## Interactive stack workspace

The interactive engine allocates a 1 MiB Asyncify save stack from the Wasm heap when execution suspends and frees it after restoring execution. This supports input waits inside nested bound-method calls. Deep native reentry remains subject to the host call stack. The fast engine has no Asyncify workspace.

The save stack is separate from the 16 MiB C/C++ stack and retains native frames when bound methods reenter the VM. Release gates suspend and restore 32 nested bound-method calls twice in source and compiled-program execution with both `vm` and `mixed`. They also verify that the core rejects 128 nested native reentries with `diag.call_stack_overflow` before requesting input, then successfully rerun the same program with shallower nesting in Node and Chromium.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](build-and-artifacts.md) · [fr-FR](../fr-FR/build-and-artifacts.md) · [ja-JP](../ja-JP/build-and-artifacts.md) · [ko-KR](../ko-KR/build-and-artifacts.md) · [zh-CN](../zh-CN/build-and-artifacts.md) · [zh-TW](../zh-TW/build-and-artifacts.md)
