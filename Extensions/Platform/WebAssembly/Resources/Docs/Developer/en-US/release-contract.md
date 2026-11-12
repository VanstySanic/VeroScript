# WebAssembly ABI, artifacts, and release contract

## Three public surfaces

`VeroScriptWebAssembly.h` is the C++ facade, `VeroScriptWebAssemblyC.h` is the WebAssembly C ABI, and the TypeScript wrapper and declarations form the browser SDK. Keep common result fields, backend names, language tags and input behavior consistent across these surfaces. The separate native Interop ABI does not define the WebAssembly ABI.

## Names and compatibility

Use module and CMake target `VeroScriptWebAssembly`, C++ namespace `VeroScript::WebAssembly`, and TypeScript entry point `veroscript-webassembly.ts`. The SDK archive is `VeroScriptWebAssemblySDK`; the installer is `VeroScriptWebAssemblyInstaller`. Preserve the C ABI names `vero_web_*`, `VeroWeb*`, and `VERO_WEB_*` at ABI version `0.1.1`. Installer profile `web`, component `web-sdk`, and product ID `dev.veroscript.web` identify existing installation records.

## Artifacts and release verification

Publish the complete eight-file set: interactive and fast `.mjs`/`.wasm` pairs, wrapper `.ts`/`.d.ts`, and both engine `.d.mts` declarations. Standard-library and localization files are embedded in the virtual filesystem. Type-check the bindings and declarations, audit the complete inventory, and exercise the generated SDK in headless Chromium, including interactive input, Web Workers and Worker termination. The artifact audit enforces Release download budgets of 17 MiB for interactive Wasm and 11 MiB for fast Wasm. These are release limits, not ABI limits; passing a source check alone does not establish browser acceptance.

The current baseline includes exact numerics and nested, local and generic type scopes.

The Web product statically assembles `VeroScriptBytecodeVm`. Source checks, compiler extensions, direct runs and reusable programs use this explicitly selected interpreter.

Both Wasm engines support initialized captures, `mutable` closures and local `static` variables through Core. Value captures are readonly by default; copied function values share their environment, while a new lambda evaluation creates a new environment. Local statics initialize on first use and retry after initialization failure. Each program run starts fresh, including its local statics; this also applies to interactive runs. The Web ABI and TypeScript API are unchanged.

## ABI and localization

Runtime semantic version, WebAssembly ABI version, bytecode version, capability flags and selected language are separate values. Discover available BCP 47 catalog tags at runtime. Do not translate API identifiers, protocol fields, diagnostic IDs or versions, and do not derive behavior from a fixed language count.

## Program and input capabilities

Keep C ABI exports, TypeScript declarations and capability detection in agreement. Compile-once support requires compile, run and destroy exports; interactive program execution additionally requires the Asyncify interactive export. An absent additive export means the capability is unavailable; never fabricate a program handle.

Source-context arrays contain matching numbers of UTF-8 paths and sources, borrowed only for the call. Program handles belong to their creating runtime and cannot move between runtime instances. Result JSON carries diagnostics, output, requested backend policy, timings and JIT counters. Release verification must cover handle reuse, fresh state per run, destruction, interactive suspension and language selection.

<a id="web-execution-policy"></a>

The published modules select no JIT backend. `vm` interprets bytecode; `mixed` remains the default and also executes through the VM. `jit` returns the structured diagnostic `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory` before executing application initializers or `main`, or requesting input. Native JIT execution tiers belong to `Modules/Execution/VeroScriptAsmJit`; Web modules neither include nor dynamically load that JIT backend. Existing callers that used `jit` must select `vm` or `mixed`. The policy names and C ABI IDs remain stable; `result.backend` records the requested policy, and all JIT counters are zero.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](release-contract.md) · [fr-FR](../fr-FR/release-contract.md) · [ja-JP](../ja-JP/release-contract.md) · [ko-KR](../ko-KR/release-contract.md) · [zh-CN](../zh-CN/release-contract.md) · [zh-TW](../zh-TW/release-contract.md)
