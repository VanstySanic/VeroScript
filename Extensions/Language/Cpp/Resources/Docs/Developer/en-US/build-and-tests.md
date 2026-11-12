# C++ build and test contract

## Targets

The C++ group is built through the module targets `VeroScriptCpp`,
`VeroScriptCppAot`, `VeroScriptCppFrontend`, `VeroScriptCppBindGen`, and
`VeroScriptCppTranspiler`. Each library target excludes its focused test sources;
tests link the public target they exercise.

## Test categories

The tests cover public-header compilation, engine/session integration, JIT
integration, AOT source and bundle output, frontend parse/merge/type mapping,
BindGen catalog/reference generation, and Transpiler source/map generation.
When a change alters a public result, generated artifact, diagnostic, or
supported-subset rule, the corresponding category must be extended rather than
relying on a single end-to-end test.

## Installation boundary

Public headers and generated libraries are install inputs. README files and
generated API references are documentation artifacts, not implementation
headers. A build-tree path must not be required by an installed consumer.

The independently distributed C++ package provides
`VeroScriptCppConfig.cmake` under its required `Runtime` module. Consumers add
both the matching Core SDK root and the C++ package's `Runtime` directory to
`CMAKE_PREFIX_PATH`, call `find_package(VeroScriptCpp <version> EXACT CONFIG
REQUIRED)`, and link `VeroScript::VeroScriptCpp`. Optional `Tools` and `Aot`
components expose their imported targets only when their sibling modules are
installed.

The C++ package records the SHA-256 identity of the Core library from the same
release build. Configuration fails if a same-version Core SDK from another
build is mixed with it. The release pipeline must compile and run
`Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal` using only staged package paths before creating the
archive or installer; repository include/library paths are not allowed.

## Memory integration gate

`VeroScriptCppMemoryIntegrationTests` covers configured Engine quotas, successive
Session calls, native memory services, hot-reload identity and quota rollback,
script cleanup before domain closure, and lease release after Session destruction.
`CppMinimal/memory.cpp` is the installed Runtime consumer. Run both alongside
`VeroScriptMemoryTests`, Session/debug/JIT tests and AOT rejection tests when
changing the memory contract. The optional backend argument exercises a real JIT
module. See [host memory rules](../../User/en-US/embedding.md).

## Cleanup and AOT regression gates

`VeroScriptCppSessionDebugTests` covers cleanup callback reentry during clear, replacement, hot-reload disable, callable release and destruction, including rejection of new retained roots and reuse after cleanup. `VeroScriptCppJitContractTests` checks cache invalidation for structured native registration. Run `VeroScriptCppAotHelloTests` with `VEROSCRIPT_AOT_REQUIRE_NATIVE=1` for a release gate: it compiles and executes generated C++, compares VM results, and checks increment/decrement side effects, lifetimes, generic scalar widths and overflow failures. `VeroScriptCppAotEmitTests` also verifies explicit rejection of unsupported user-type increment/decrement, including generic specializations.

## Module macro integration gate

`VeroScriptCppEngineIntegrationTests` covers host macro seeds, overlays, explicit/re-export/implicit visibility, source and file caches, dependency invalidation, hot reload, failed dependency preservation, physical import diagnostics, and stripped library macro interfaces. `VeroScriptCppAotHelloTests` compares generated native code with the VM for public variadic macros and caller expansion. `CppMinimal/main.cpp` consumes the exported macro metadata through staged SDK headers. Run these when module preprocessing or bytecode interfaces change.

## Annotation integration gate

`VeroScriptCppAnnotationTests` checks Engine validation/execution, Session source/file loading and hot reload, nonfatal diagnostics, dependency caches and uncacheable processors across VM/Mixed/real JIT. It also checks callback reentry rejection during source/file loading, separate compilation and reload, plus state preservation and reuse after cancellation. `VeroScriptCppAotEmitTests` ensures a raised generation budget survives without a second default preparation. Native `VeroScriptCppAotHelloTests` compares generated virtual bodies, separated generic bodies and declaration/implementation scopes. The SDK consumer checks installed headers, processor registration and Session/AOT APIs.

## Project transpilation and file transactions

`VeroScriptCppTranspilerTests` checks final module boundaries, implementation scope, virtual dispatch, defaults and original-file diagnostics. `VeroScriptCppTranspilerNativeTests` compiles and executes the C++ → VeroScript → C++ project pipeline. `VeroScriptCppFrontendTests` covers explicit rejection of unsupported syntax, normalized paths, hard-link identities and collision protection before writes. Batch parsing, CLI input protection and output transactions share `FFilePathSet`; unavailable physical identities retain the previous alias checks.

## Terminal host integration gates

`VeroScriptCppConsoleTests` checks terminal capabilities, exact text output and `print` grouping, permission denial, host callback failures, style/cursor restoration after runtime errors and instruction exhaustion, repeated Session calls, and plain output streams across VM, Mixed, and a real JIT backend. Native/VM comparisons in `VeroScriptCppAotHelloTests` check color and cursor argument evaluation order, including side effects before validation failure. The staged SDK's `CppMinimal/main.cpp` verifies the public host interface and restoration after execution. Run these gates when the terminal contract changes.

The native gate also checks entries with and without command-line arguments: adapter failures, standard and nonstandard exceptions, and failed diagnostic streams must return failure and complete cleanup.

Worker gates also check that standard and nonstandard exceptions during terminal guard construction fail the task, and that terminal restoration completes before task completion is reported.

## AOT evaluation and reference gates

Native/VM comparisons in `VeroScriptCppAotHelloTests` cover argument order and conversions for ordinary, generic, extension and constructor calls, value snapshots, ref parameters, short circuit operators, compound assignment, and global callable initialization. Function values are evaluated after arguments, matching the VM; instance receivers are evaluated first. Indexed references must remain valid when later arguments grow or rebind a container, and invalid index conversion must fail before subsequent argument side effects. Pure scalar expressions remain direct; captured value arguments are moved into calls to avoid an extra copy.

Assignment regressions also check that the result remains a value after a saved receiver is destroyed. Simple indexed writes restore the saved container to its original variable or field; nested linear indices and qualified globals resolve their current storage path. RHS rebinding must preserve the same aliases and object identity as the VM. Bounds checks for simple indexed assignment occur after RHS side effects, while reference-path index conversion occurs before them. Scalar indexed writes without conversions remain direct to avoid copying a native array.

The `CppMinimal` consumer deploys the matching Core SDK's standard library and localization assets beside the executable on every build, including builds that do not relink. The deployment must repair missing files and refresh changed SDK assets under both Visual Studio and Ninja/Make generators.

See [C++ AOT — Reference returns and evaluation lifetime](../../User/en-US/aot.md#reference-return-lifetime).

Persistent indexed aliases and indexed reference returns require managed storage paths. Persistent references obtained from temporary value receivers also require managed owners. These cases are explicitly rejected by AOT; use the VM backend.

Implicit struct fields receive their script field defaults, including generic field types and fixed arrays. Storage initialization does not execute the struct constructor body.

The native gate checks retained local field aliases, zero-argument receiver lifetimes, single evaluation of reference assignment targets, and numeric reference overflow and narrowing. Emission tests require explicit rejection of cleanup-bearing receivers and returned local aliases. Member, destructor, static field and open-template definitions follow complete type layouts and global accessor declarations; the native gate covers forward global access and constrained template members and static fields.

The gate also checks direct and nested field aliases, references in for initializers, member and captured receivers, generic fields and default struct field storage. Emission tests reject persistent indexed storage and temporary value receiver aliases. Recursive cleanup checks snapshot type names before lookups that may register generic specializations.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](build-and-tests.md) · [fr-FR](../fr-FR/build-and-tests.md) · [ja-JP](../ja-JP/build-and-tests.md) · [ko-KR](../ko-KR/build-and-tests.md) · [zh-CN](../zh-CN/build-and-tests.md) · [zh-TW](../zh-TW/build-and-tests.md)
