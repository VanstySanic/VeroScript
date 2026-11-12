# Bytecode VM maintenance

Core owns language operations, values, logical frames, governance, cleanup and dispatch contracts. This module owns the complete interpreter in `Private/VeroScriptBytecodeVmExecution.cpp`, member execution and diagnostics. The static library and dynamic module link the header-only `VeroScriptExecutionContract`, not a second Core binary. `BytecodeVm::CreateFactory` receives services from the actual owning Core.

`FVmInvocation` receives already prepared Core frames. Preserve its domain/context/program generation, call token and frame boundary; do not prepare arguments or enter the frame twice. Set `bEntered` before the first semantic operation. `NotEntered` is valid only while no execution has entered. Continuations, instrumentation, governance and incomplete-cleanup/reset behavior remain Core contracts.

Each active context has an instance; synchronous reentry uses its active instance. The official backend supports idle rebinding in the same domain/thread. Unbind stale programs before replacement and retain the factory/module while instances exist. The allocating factory destroys its instances; the query descriptor supplies factory destruction in the allocating module.

The dynamic entry is `veroscript_vm_query`. Validate product `CoreVersion`, descriptor size, `BuildVmModuleAbi()`, ID/version and capabilities before factory use. Product encoding is major:8/minor:8/patch:16; it is distinct from the C ABI version encoding. Generated implementation identity tracks module sources. Public-header/compiler/STL/CRT/platform/layout matching is required even when product version strings match.

`VS_VEROSCRIPT_VM_VERSION` is the numeric `major.minor.patch` CMake cache setting for the independent VM product; its default is `0.1.1`. It supplies the generated `VEROSCRIPT_VM_VERSION`, factory/module version descriptors and installed CMake package version. Full packagers pass the registered `Vm` business version and reject a stale cache or mismatched artifact. Keep this product version separate from `CoreVersion`, service ABI and implementation/header fingerprints; changing the version does not make incompatible binaries usable.

The standalone CMake project consumes an installed `VeroScriptExecutionContract`; root integration also supplies the actual Core for tests. `VeroScriptVmBackendStaticTests` and optional `VeroScriptVmBackendDynamicTests` exercise both forms. `Resources/Tests/Contract.cpp` and `Package.py` cover contract/package consumption. Changes require checking capabilities, fallback-before-entry, reentry, governance, cleanup/reset, owner lifetimes and installed consumers. iOS/Web build static assembly; desktop results do not establish device/platform validation.

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/en-US/vm-backends.md)

<!-- docs-navigation -->

---

[Developer](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
