# Build service maintenance

## Planning and execution

`FBuilder::Plan` freezes the graph, source/artifact identities, providers and launcher template without executing processors, factories or subprocesses. Boundary paths in `FBuildRequest` are absolute; configuration paths are resolved against the selected project root without changing the process directory. `FServices` retains host catalogs, callbacks and explicit toolchain/environment identity. Provider `Plan` must remain free of execution side effects.

`FBuilder::Execute` rechecks frozen inputs; changed inputs require a new plan. Track additional provider inputs through `FStepContext::TrackInput` without replacing an existing digest. Artifact references are interpreted only in declared input fields; provider `options` remain opaque JSON. Cache identities include configuration, upstream content, provider identity and output hashes. Custom caching requires an explicit deterministic provider/toolchain identity; dynamic processors and crypto/trust-sensitive compilation execute conservatively.

## Publication and target policy

Publish each target atomically on its volume with ownership receipts. Never overwrite foreign/modified output. A later target failure does not undo independent published targets. Keep artifact owners and deployment leases alive while consumers use content. `Deploy` and `LeaseDeployment` consume installed frozen environments without an install fallback.

Validate application content against the selected target platform/ABI, not the build host; actual native execution still needs target validation. Source applications retain portable compile defaults and runtime+compile environment usage, and reject development-only search paths. Runtime components and bootstrap templates are explicit verified inputs; changing template bytes invalidates a plan/cache. Keep native platform wrappers and signing separate from content assembly.

Processes receive executable/argument arrays without a shell. Restricted scripts expose declared input/output helpers and enforce limits; trusted native providers/processes are not an OS sandbox. Resource protection verifies existing artifact signatures before transformation. Re-encryption decodes an existing envelope once; changing signed content requires a new policy, while unchanged copies retain their signatures. Fail before publication when verification fails and never serialize secret keys into configuration or artifacts.

## Integration and verification

Keep `Public/VeroScriptBuild.h`, graph planning/execution, `Resources/Schemas/veroscript-build.schema.json`, CLI parsing and Application/package adapters consistent. The root build enables `VeroScriptBuildTests`; `Resources/Examples/hello` and `Resources/Examples/generated` exercise application/library/distribution outputs and script generators. Maintenance checks should cover stale plans, cache invalidation, owner leases, target selection, signature failures and atomic publication. CLI workflows and runnable examples are in the user guide; platform execution and signing require separate recorded validation.

<!-- docs-navigation -->

---

[User guide](../../User/en-US/index.md) · [Developer documentation](../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
