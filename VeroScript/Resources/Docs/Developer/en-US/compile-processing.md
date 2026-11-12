# Compile processing and source metadata

Core provides native compile processors through `VeroScriptCompileProcessing.h`. Processors transform source, generate types and members, replace declarations or bodies, and attach annotations and comments. Generated code follows ordinary visibility, generic and type checks. Business rules belong to the extension.

## Registration and lifetime

Register `FCompileProcessorDesc` entries in `FCompileProcessorRegistry`, call `Freeze()`, and supply the registry through `FCompileOptions::CompileProcessing.Registry`. Each entry declares identity, version, configuration key, order, phases, subscriptions, allowed operations and a factory. Each compile task creates its own processor instances; an instance persists across that task's phases. Callback contexts and AST views are borrowed for the callback only. Capture a shared owner in the factory when native code or configuration needs a lifetime lease.

Matching subscriptions invoke each processor once per event. Processors run in `Order`, then `ProcessorId` order. Ordering does not legalize conflicting operations. Unknown annotations remain ordinary metadata. `FAnnotationProcessorRegistry` uses the same transaction pipeline: methods are added in Members, and defaults are completed in Bodies after user implementations have been bound.

## Phases and operations

| Phase | Input and operations |
| --- | --- |
| RawSource | Original text; EditSource may translate custom syntax or imports |
| ActiveSource | Preprocessed text; EditSource preserves imports and preprocessing boundaries |
| Types | Declaration outlines; add or replace types, interfaces, enums and aliases |
| Members | Registered types; add or replace members, functions and globals |
| Bodies | Bound implementations; CompleteBody, ReplaceBody and SetMetadata |

Types precedes alias and inheritance binding. Members precedes cross-file implementation binding and deferred body parsing. Bodies cannot change layout or signatures. Generated declarations and attached metadata do not recursively trigger processors; subsequent phases can query them.

`AddDeclaration` uses `FCompileScope` with module, namespace, optional type OwnerId and the current Snapshot; it has no TargetId. Replacements identify a current target and preserve declaration kind, name and generic arity. CompleteBody fills an absent body; pure virtual methods and deleted declarations cannot receive bodies, except the language's existing pure virtual destructor rule. The core rejects stale targets, conflicting edits and incompatible parent/child operations before applying a batch.

`FCompileMetadata` distinguishes omitted collections from explicitly empty collections. Replacement preserves unaffected declaration documentation and removes obsolete child metadata. EditSource ranges use UTF-8 bytes; mappings preserve origins, and unmapped generated text anchors to the operation origin. ActiveSource cannot introduce preprocessing directives or alter the resolved import sequence.

## Metadata, caching and packages

Serialized source metadata shares repeated paths and identifiers through snapshot-local string references. All values, identities, positions and source mappings are retained; the encoded-byte limit remains 64 MiB, with a separate 512 MiB limit on expanded string payloads. The reader also accepts the previous literal-string metadata form. Preparation fingerprints, bytecode and packages use the same codec; rebuild matching Core and native backends when the implementation identity changes.

`VeroScriptSourceMetadata.h` owns immutable document, declaration, comment and annotation snapshots. Annotation arguments retain flags, scalar kinds, nested groups, duplicates, text and ranges. `RegisterAnnotation` may register documentation without executable behavior. Reflection information objects expose annotations, comments, documentation, declarations and source metadata; metadata access does not grant executable reflection access.

CommentRetention selects Full, Documentation or NoComments. Annotation and independently registered documentation survive NoComments. Unavailable means source information is absent. Old immutable snapshots remain readable after hot reload.

`FCompileResult::PreparedProgram` retains the prepared declarations, origins, operations and dependency fingerprints. Revalidate with MatchesHost, MatchesProcessing and MatchesDependencies; also include source, overlays, macros, imports and normal compile options in the host cache key. Non-cacheable processors must not reuse prepared results across tasks. Direct AST preparation cannot recreate source transformations or clone processor state.

Open-generic packages save transformed templates, operations and producer metadata. Consumers replay Types, Members and Bodies without loading producer processors. Closed code uses package bytecode; generic substitution remains ordinary language substitution. Regenerate older development artifacts when the contract changes.

Cancellation is cooperative. Processors check CheckCancelled during long work; ReadDependency records present and missing resources. Core limits events, operations, generated text and metadata. Blocking native callbacks require host cooperation. Interop, tools and AOT must consume the same prepared result; JIT executes its compiled business code.

Examples: [native processors](../../../Examples/CompileProcessing/main.cpp), [script extensions](script-compile-extensions.md).

## Annotation name matching

Use `FAnnotationDefinition::CaseSensitive = false`, the last argument of `FHostBindingRegistry::RegisterAnnotation`, or script `annotation_definition.case_sensitive = false` to opt in per definition. Binding catalog JSON uses `caseSensitive`; C Interop uses `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE` in `name_flags`, C# uses `CaseSensitive`, and Python uses `case_sensitive`. Native extension descriptors carry the same policy. Name folding is ASCII and locale independent. Active registrations whose accepted names overlap are rejected, including different spelling variants when either registration ignores case. The definition identity and all processing/environment fingerprints include the policy; bytecode and libraries preserve the producer definition, including open generic templates. A consumer registration does not change an already compiled annotation’s policy. Rebuild development artifacts after a metadata contract change.

## Integration ownership

`FCompileResult::PreparedProgram` is the shared preparation result. C++ Session exposes `SetCompileProcessing`; code-generation hosts pass `FCodegenOptions::CompileProcessing`; Tooling analysis and protocol options carry the same configuration. Native Interop hosts can create an opaque processing service through `CreateCompileProcessingService` in `VeroScriptInteropCompileProcessing.h`. These are existing integration points, not a future Core-only plan.

Supply registries and native owners explicitly. A plain CLI does not discover arbitrary native processor factories automatically; script extensions and explicitly selected native modules use their configured loader paths. Consumers must not rerun processors for an already prepared program or recreate declarations by parsing comments. JIT executes the resulting business code. Platform and application acceptance remains a separate responsibility.


## Scoped declarations and block generation

Declaration views include `ScopeId`, `ParentScopeId`, `bLocal`, `bNested` and their semantic owner. `FCompileProcessingContext::Blocks` is a borrowed, read-only block/statement-anchor view valid for the callback. Add local types or aliases in Types using `FCompileScope::ScopeId` and `InsertionBeforeId`; use the block’s end anchor for an empty block or append. Module and owner fields retain their existing meaning.

Script processors use `context.blocks()` and `context.block_scope(scope_id, before_id)`. Block and anchor records expose getter methods (`scope_id()`, `statements()`, `id()`, `range()`); source ranges retain their normal value fields. Do not construct scope records or arbitrary offsets to bypass snapshot validation.

Order at a shared anchor is deterministic. A declaration is visible from its insertion point. Replacing a containing body/declaration conflicts with operations on its old anchors. Completing/replacing a body rebuilds its local declaration catalog. Generated descendants undergo normal checks without another processor round; generic instantiation substitutes prepared declarations without rerunning processors. Scope identities, generated operations, child catalogs and ownership travel through VSBC and libraries. Rebuild development artifacts when the layout identity changes.

## Declaration-append batches and owner references

In `Private/Processing/VeroScriptDeclarationProcessing.cpp`, the Members-phase `DeclarationAppendsOnly` path requires every operation to be `AddDeclaration`, with empty `TargetId` and `ScopeId`. It permits namespace-level helpers with no `OwnerId` interleaved with members of class/struct/interface owners already present in the validated declaration snapshot. This does not relax phase, snapshot, writable-module or conflict checks, or make a newly generated owner a target in the old snapshot.

`ReuseOwners` borrows that snapshot only while the storage addresses of `P.Classes`, `P.Structs` and `P.Interfaces` remain unchanged. A generated helper body can append local types and grow these containers; after storage changes, rebuild references with `Collect(P)`. Do not retain declaration/AST pointers across such growth. Review mixed namespace helpers and members, generic owners, virtual/interface calls and local declarations; `FInterleavedAppendGenerator` in `Private/Tests/VeroScriptCompileProcessingTests.cpp` provides source scenarios. Their presence is not a test result.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](compile-processing.md) · [fr-FR](../fr-FR/compile-processing.md) · [ja-JP](../ja-JP/compile-processing.md) · [ko-KR](../ko-KR/compile-processing.md) · [zh-CN](../zh-CN/compile-processing.md) · [zh-TW](../zh-TW/compile-processing.md)
