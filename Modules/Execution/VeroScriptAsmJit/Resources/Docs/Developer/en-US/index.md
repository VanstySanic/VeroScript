# Backend maintenance

See [Core JIT services and concurrency](../../../../../../../VeroScript/Resources/Docs/Developer/en-US/jit-and-concurrency.md#native-abi-version).

Core owns the execution contract, public JIT contracts, runtime services, validation, scheduling and generation accounting. Concrete IR, lowering, native emission, portable Linear execution and executable storage belong to `Modules/Execution/VeroScriptAsmJit`, not Core. `Private/JitSupport` implements the generic cache and module loader. Public-header fingerprints and the compiler/STL/runtime/platform handshake, not product version alone, determine binary backend compatibility. The bytecode interpreter belongs to the independent `VeroScriptBytecodeVm` module.

## Build and ABI

`VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` builds Core without the concrete backend. Set it to `ON`; `VEROSCRIPT_ENABLE_ASMJIT=ON` enables native emission and `OFF` retains portable Linear execution. Source hosts link `VeroScriptAsmJit` and inject `CreateAsmJitBackendFactory()`. `VeroScriptAsmJitBackend` builds a separate shared module without a Core copy. Imported emitter targets need compatible PIC where required, and their owner deploys shared dependencies. Windows dynamic loading requires `/MD` or `/MDd`.

```cmake
target_link_libraries(MyHost PRIVATE VeroScriptAsmJit)
```

## Runtime contracts

Keep `Ready / Unsupported / Deferred / Failed` compilation and `NotEntered / Returned / ControlTransfer / Failed` invocation distinct. Only `NotEntered` permits VM replay. Core validates ownership, services and budgets before publication. Executables retain code/module ownership; invocation contexts, frames and values are borrowed only while active. Reacquire views after nested calls. Live error/control/callback flags must be reread at every fast-path decision.

Session code-reuse eligibility, governed execution, retention limits and accounting are maintained in [C++ Session](../../../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/en-US/session.md#session-jit-cache).

## Release checks

The `veroscript_jit_query` POD handshake verifies API, public-header fingerprint, compiler/STL, C++ mode, runtime and platform before creating a C++ factory. Preserve library lifetime through factories, workers and executable artifacts. Test Core without a backend, injected custom contracts, incompatible libraries, explicit file/directory selection, fallback without replay, resource reclamation and Session cache invalidation. Package using `package-veroscript-jit.py`; independent backend versions do not relax ABI matching. iOS device and simulator prohibit dynamic loading/native emission; statically injected portable backends require their own validation.

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/en-US/jit-and-concurrency.md)

## Retained byte quotas

Zero `MaxNativeBytes` or `MaxRetainedPayloadBytes` permits no retained bytes of that category; these byte limits differ from the unlimited zero policy budgets.

Official backend archives are published under `Release/Modules/Execution/Jit/<target>/<version>/archives/`. Modules have no new standalone installers. The functional payload keeps runtime dependencies and legal notices and excludes docs, examples, tests, benchmarks and symbols. Keep source documentation in Resources/Docs and publish it through the integrated Resources documentation collection.

## Vendored dependency maintenance

AsmJit is owned by this module. Keep its source under `ThirdParty/asmjit`, retain the original [license](../../../../ThirdParty/asmjit/LICENSE.md), and record the upstream revision in [VERSION.txt](../../../../ThirdParty/asmjit/VERSION.txt). Do not move vendored source into Resources/Docs or copy it into Core. `VEROSCRIPT_ENABLE_ASMJIT=OFF` selects the portable implementation of a built backend; `VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` omits this backend module.

When updating the dependency, replace the vendored revision, retain its notices and code-generation database licenses, update VERSION.txt and the exact license reproduction in [the module notice](../../../Licenses/NOTICES.md), then reconfigure and run JIT and packaging tests. Preserve compiler/STL/runtime compatibility and validate both native-emission and portable configurations. The backend install component exports LICENSE.md and VERSION.txt to `licenses/asmjit/`; verify their bytes against the owning source files. The shared notice index is Resources/Licenses/README.md; dependency notices are owned by their consuming module.

## Controlled scalar blocks and partial loop coverage

Controlled short integer loops may use a native CFG with Core primitive operand blocks. Every bytecode instruction still checks and charges limits, cancellation and timeout; counts are not prepaid in batches. EnableScalarBlocks declines custom host execution checks and mutation observers before entry. Unsupported scalar representations resume ordinary services at the current instruction without duplicate checks/counts or function replay. Complex loops retain VM cost selection.

Type-compatible string += participates in ordinary CFG validation. An unoptimized text loop does not hide another loop with a native implementation in the same function. Controlled Mixed remains explicitly opt-in.

Small, straight-line integer receiver expressions can prefer the Core bounded call plan over a native call bridge. This is a cost choice after ordinary backend compilation/validation; failure, mutation observers and execution governance retain their ordinary VM paths. Larger bodies, calls and loops remain eligible for their independent backend plans.

`bPreferVmForNestedExecution` applies only to an ungoverned interpreter caller; standalone host calls and native callers retain their native entry.

See [Core JIT services and concurrency — Validation](../../../../../../../VeroScript/Resources/Docs/Developer/en-US/jit-and-concurrency.md#construction-dispatch).

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
