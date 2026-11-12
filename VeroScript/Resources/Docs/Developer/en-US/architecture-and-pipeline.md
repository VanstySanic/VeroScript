# Core architecture and compilation pipeline

[Replaceable VM backends](vm-backends.md)

The fixed bytecode identifier is `VSBC`; its format version follows the product version, currently `0.1.1`. Rebuild artifacts from earlier development builds.

The core owns the language implementation and is the dependency root for the
native adapters. Its public areas cover lexer, parser, AST, preprocessor,
compiler, bytecode, VM, code generation, JIT, module loading, packages,
debugging, host binding, reflection, serialization, concurrency, mutation, and
locale selection.

## Pipeline

The implementation resolves source files, overlays, imports, and package inputs;
tokenizes and preprocesses while retaining source locations; parses the public
AST; performs semantic analysis and type/generic/visibility checks; emits
bytecode and debug metadata; links module/package information; then executes
through the VM or a selected JIT policy.

Each stage returns structured diagnostics. A consumer must not infer success
from a stream message or from a partially populated bytecode object.

## Module graph and loader inputs

`VeroScriptModuleLoader` resolves the main module, ordinary imports, implicit
imports, stdlib modules, and package/template module graphs before merging the
program. Import paths and stdlib roots are normalized and de-duplicated. Source
overlays replace disk reads by normalized path, which lets an editor or a Web
host compile an in-memory graph without writing files.

Preprocessor `-D` definitions and structured predefined macros are normalized
before they affect the source fingerprint. Host type names are supplied before
parsing so declarations such as `Counter c = Counter(1);` are classified with
the host binding catalog. Circular-import and project-wide-access policies are
explicit loader settings, not hidden global compiler state.

When a stdlib root or module resource is introduced, the loader can merge a
sibling `localization/` tree. The locale merge is keyed by canonical catalog
IDs and retains the `en-US` fallback; a resource package must therefore deploy
its stdlib and localization directories together.

## Public/private boundary

`Public/` contains supported headers. `Private/` contains compiler,
parser, VM, JIT, and internal data structures. The public-header tests enforce
that installed headers do not depend on absent private files. External tooling
and adapters use public contracts. Source-tree targets that need compiler AST or
runtime internals must receive a private include path explicitly in CMake and
are treated as reviewed friend dependencies, never as SDK consumers.

## Static declarations, generics, and captures

Static fields are registered as module-owned globals and initialized through
module initialization. A static access may use a type receiver or an evaluated
object receiver; the latter still evaluates the receiver for side effects
before reading the backing global. Generic type instantiations substitute the
field type and register their static storage independently.

The compiler records lambda captures and generic bounds in the AST and carries
their source locations into bytecode diagnostics. Static type restrictions,
capture validity, type-argument bounds, and inheritance rules are compiler
semantics. A frontend, AOT emitter, or tooling consumer must use the recorded
declaration identity and not reconstruct these rules from display names.

## Scoped analysis control and dependency metadata

`FAnalysisControlScope` owns cancellation and profiling callbacks for synchronous analysis on the calling thread. Nested scopes restore the previous control; callback captures must remain valid throughout the scope, and each `FAnalysisStage` must finish before its control scope. `CheckAnalysisCancellation()` throws `FAnalysisCancelled` when the predicate returns true. This is separate from syntax diagnostics and does not cancel VM execution. A cancelled lexer can be tokenized again without retaining a partial stream.

`FAnalysisStage` reports elapsed milliseconds on that same thread; its name must outlive the stage. Observer exceptions are ignored so they cannot replace cancellation or parser exceptions during unwinding. Nested stage times overlap and must not be summed as independent work.

`VeroScriptModuleLoader::GetLoadedFileStamps()` exposes modification time and size captured before physical source or package reads. In-memory inputs and overlays have no disk stamp. The returned reference is valid until the next load or loader destruction. Cache consumers reject `bValid == false` and recheck disk metadata before reusing an AST, including changes during parsing. These stamps are metadata checks, not byte hashes; document versions, options and overlays require their own invalidation.

## Lengths and indices

Collection lengths and linear indices use `int64`. Native storage stays `size_t`; conversions check both the script range and the platform range before narrowing. C# containers retain CLR limits. The bytecode layout magic is `0x43425356` (`VSBC`) for these semantics. Earlier cached bytecode with 32-bit length results must be recompiled from source. Rebuild C++ hosts and generated AOT outputs against the updated headers and standard library.

## Backend selection

<a id="core-vm-ownership"></a>

Core owns programs, values, logical frames, language operations, governance and dispatch. The complete bytecode interpreter belongs to `VeroScriptBytecodeVm` in `Modules/Execution/VeroScriptBytecodeVm`. Its backend ID is `vs-bytecode`; its package name is `vs-bytecode-vm`. Pure Core selects neither a VM nor a JIT implicitly. Runtime explicitly assembles the official dynamic library in `Vm/`; Interop, Web and mobile static products explicitly inject their VM. JIT: `Modules/Execution/VeroScriptAsmJit`; Core: `Private/JitSupport`.

## Public macros

See [Preprocessor — Public macros](../../User/en-US/language-basics/preprocessor.md#exported-macro-contract).

VSBC 0.1.1 also appends the validated public macro table. Source caches and `.vslib` libraries retain it; `.vsapp` writes an empty table. Rebuild older binary packages as well as bytecode caches. Module preprocessing pauses at emitted import paths and resumes after the target interface is ready; module preparation uses a bounded heap stack.

## Compile-time annotation processing

A frozen native processor registry is passed through `FCompileOptions.AnnotationProcessing`, C++ Engine/Session, Tooling or `FCodegenOptions`. Bare `@Name` equals `@Name()`; ordered flags, named scalar arguments and nested groups are preserved. Unknown annotations remain metadata. One declaration stage binds source definitions before adding method bodies and auxiliary methods; ordinary checking, virtual dispatch and generic substitution then apply. Cross-file definitions require both the declaration and implementation modules to be loaded. Generated members and open templates survive package export without the producer processor. Tooling exposes read-only `GeneratedDocuments`; embedding tools must display these virtual documents. Native processors are registered by the host, not automatically loaded by standalone tools. C# and Python attach a native service handle through Interop. VSBC 0.1.1 includes structured annotations and generated operations; rebuild older caches and binary packages. The native integration example is in `VeroScript/Resources/Examples/AnnotationProcessing`.

The CLI regression covers two libraries with compiled attribute specializations and stripped application output.

## Host overloads

See [Host bindings and binding catalogs — Host overloads](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/bindings.md#host-overload-resolution).

## Shared template services

`VeroScriptTemplate.h` owns the ordered Type/Value parameter schema, exact typed constants, bounded pure evaluation, argument encoding and declaration/instance snapshots. Compiler-owned specializations leave input ASTs unchanged. Defaults bind in their declaration context. Instance keys include enclosing and own arguments; VM, JIT and Core AOT execute the resulting concrete bytecode and layouts.

`ModuleMeta::EnumerateTemplateDeclarations` returns owned open-declaration snapshots without executing bodies. Function and type snapshots carry `TemplateInstance`, separating own from enclosing arguments. Type arguments use canonical type names; value arguments use exact `@value:` encodings decoded by `DecodeTemplateArgument` after removing that prefix. Values never pass through double. Snapshot strings and schemas live with their returned owner; processing callback views remain valid only during the callback.

Native processing views expose `TemplateParameters`; script `declaration_info.template_parameters` exposes the same ordered kinds, defaults and enclosing flags. Libraries retain open methods even on ordinary exported owners, together with required private dependencies and replay records. Closed application loading does not run processors. Changes invalidate the internal layout/build identities. Rebuild source/AST consumers against matching Core headers and libraries. Independent source exporters diagnose unsupported method/value templates and references before output; Core AOT uses the selected concrete instances.

`FTemplateCallTypeQuery` in `VeroScriptTemplateQuery.h` queries signatures on a prepared immutable program using Core inference, defaults and overload selection. It does not compile bodies or run processors/native calls. Callers filter visibility and access. Returned type spelling is for lookup, not display or serialized identity. The program and bindings must outlive the query; each query belongs to one analysis thread and propagates cancellation.

## Runtime optimization boundaries

Bounded integer call plans require execution without debugging, hot reload or governance, and preserve call-depth limits. Arguments and value captures must be int. Failed integer-field probes commit no writes and return to the ordinary VM, preserving partial effects on errors. Mutation observers disable field plans. Plans do not cross module or program generations; borrowed-module caches are cleared at execution boundaries. BigInt hashes are cached atomically on demand in immutable payloads; numeric copies still create distinct wrappers and retain custom protocol paths.

FScriptObject host bases use GetHostBase()/SetHostBase(). Ordinary objects allocate no auxiliary host/copy-construction record, and copied records detach before mutation. Rebuild C++ SDK consumers and JIT backends against matching headers and Core identity; the C ABI is unchanged.

Object type identity uses `FScriptObject::GetTypeName()/SetTypeName()`. Names share immutable storage through a bounded name cache and remain valid after the VM/module is released. A borrowed name reference expires when that object changes its type name; changing the name invalidates its runtime layout hint. Callable signature caches retain immutable schemas across reentry and validate the active module, generation and signature; argument, reference and return checks remain authoritative.

## Field defaults and static dependency preparation

`RegisterStaticMethodDeclarations` publishes callable signatures before ordinary field layouts. Closed generic structs/classes/interfaces register static storage and publish their owner descriptors and method declarations before `CompileOwnFieldDefaults` binds per-instance defaults. Preserve namespace, access context, type/value substitutions and field metadata; reacquire descriptors by index after callbacks that can grow the tables. Parser self-type lookup uses lexical declaration identities while preserving explicit enclosing arguments, root qualification and value bindings.

`FStaticInitializationDeferral` prevents module initialization code from entering a per-instance field buffer. `bStaticFieldInitializationStarted` separates early layout from the static-init phase; `PendingGenericStaticFieldInits` keeps owners pending until required. `FlushPendingGenericStaticFieldInitializers(RequiredType)` removes only the requested owner and retains unrelated order. Initializer code is emitted once per backing global, rather than embedded in each instance default or emitted again for each runtime static read.

`Private/Compiler/VeroScriptStaticInitializationDependencies.cpp` prepares dependencies from the detached, lowered initializer bytecode. `PrepareStaticInitializationDependencies` follows selected calls/constructors, field defaults, copies and local destruction; it tracks callable provenance, captures and reference writes with state revisions. Stored closures, retained allocations, nullable fields and empty containers must not acquire destruction/execution dependencies merely from their declared types. Unknown callback targets are not an exhaustive runtime call graph, and native callback side effects are not executed by this analysis.

Compile the consumer expression into a temporary buffer, prepare its dependencies, then append it with matching debug positions and global-init ranges. Keep required generic initialization outside short-circuit branches and per-field buffers. Analysis checks cancellation, snapshots descriptors before recursive preparation and uses active/revision guards rather than executing business code. Preserve normal unrelated initialization order; do not claim arbitrary cyclic dependency resolution.

Review field defaults calling later static declarations, nested self types, aliases, const/reference access, selected constructors, callable/reference writes, copy/destructor lifetimes and unrelated owners. Validate Core VM, selected JIT and Core AOT paths separately; standalone source export is a different contract. The source scenarios are in `Private/Tests/VeroScriptStaticMemberTests.cpp`, the backend differential suite and the C++ Core AOT suite. Their presence does not establish a test result.

## Decimal conversion implementation

`Shared/Private/VeroScriptFloatParsing.h` shares `FromCharsDecimal` between the parser and scalar text services. Detect floating `std::from_chars` support; the fallback converts directly to the destination width, bounds the significant prefix with a sticky digit, uses a C numeric locale and nearest rounding, and restores caller state. Nonzero subnormals are valid; nonzero input rounding to zero reports range failure without assigning output. `ParserDetail::ParseFloatLiteral` separately retains the representable-double underflow exception for `f` literals. Keep midpoint, negative-zero, long-mantissa, locale and failure-preservation cases distinct; source test presence is not platform acceptance.

Core binding-catalog JSON and Tooling LSP/DAP JSON also use this conversion. `vs_use_shared_private_headers` supplies private include paths; the shared header is not public SDK API.

`Shared/Private/VeroScriptJsonString.h` provides JSON string escaping for binding catalogs, Interop and native language tools without a Core runtime dependency. The append operation writes string contents without outer quotes or UTF-8 validation; callers preserve their existing control-character escape style and supply non-aliasing input. This private build input must accompany source distributions and participate in each consumer's implementation fingerprint.

<a id="floating-formatting-support"></a>

`Shared/Private/VeroScriptFloatFormatting.h` supplies `ToCharsDecimal` and `ToCharsGeneral` to package macro serialization, application i18n, Tooling JSON, Json and GUI numeric controls. Use the floating `std::to_chars` overload when available; the compatibility path uses a C numeric locale, preserves the original float/double width for round-trip selection, and restores caller errno and rounding state. This is private implementation support, not a public SDK API. Include it in Core/target-AOT, Json binding and GUI compilation fingerprints and source distributions; validate the fallback separately from standard-library charconv.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](architecture-and-pipeline.md) · [fr-FR](../fr-FR/architecture-and-pipeline.md) · [ja-JP](../ja-JP/architecture-and-pipeline.md) · [ko-KR](../ko-KR/architecture-and-pipeline.md) · [zh-CN](../zh-CN/architecture-and-pipeline.md) · [zh-TW](../zh-TW/architecture-and-pipeline.md)
