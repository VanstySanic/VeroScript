# Application service maintenance

## Architecture and boundaries

`VeroScriptApplication` sits above Core. Core encodes compiled images and `.vslib` libraries; Application owns standard ZIP `.vsapp` containers, descriptors, inventories, preparation and content lifetimes. Keep ZIP, native crypto adapters and GUI integration outside Core. `VeroScriptApplicationPackages` adapts the HTTP-free `VeroScriptPackageRuntime` to explicit frozen environments; opening an application must not solve or install packages.

`BuildApplication` returns sealed content files; `WriteArchive` serializes the outer ZIP. Its raw-image overload must preserve baked image bytes and metadata. `FApplication::Open` validates stored content. `ReadEntryPackage` inspects compiled metadata without loading providers or running environment factories. `Prepare` retains the environment and byte owners; `FPreparedApplication::CompileSource` applies owned portable source defaults. Session loading and reloading must use that common path.

## Integrity, ownership and policy

Keep inventory checks, path normalization, local ZIP headers, file modes, size/expansion limits and content hashes consistent across directory, ZIP and single-file forms. Reject extra files, traversal, case collisions and special files. Writable caches retain ownership receipts and cooperating process leases; foreign directories fail and consumers retain leases for materialized native content. Reload stages code and environment together and rolls both back on failure; changed native catalogs require a fresh application lifetime.

Protection services authenticate stored content before Core receives plaintext. Signature verification uses consumer trust and independent-key thresholds; missing required services must fail without plaintext fallback. Reassembly of signed content requires an explicit new signature policy. Retain native crypto module owners, never serialize secret callbacks/keys, and keep content signing separate from platform signing. Source entries retain compile defaults without importing development-only filesystem paths.

## Integration and verification

The root build controls the optional module; `CMakeLists.txt` exports Application and its package adapter separately. Maintain matching Core identities, SDK exports, schemas in `Resources/Schemas`, Interop/language adapters, CLI and runtime delegation together. Root-enabled `VeroScriptApplicationTests` and optional `VeroScriptApplicationPackageServicesTests` cover container/preparation and environment integration. Review corruption, trust failures, owner lifetimes, rollback, source compilation, relocation and directory/ZIP/single-file equivalence when changing these paths. Platform bootstrap/signing and actual device execution need their own recorded validation; static document checks do not establish those results.

`BuildApplication` also accepts an already baked Core application image, preserving its bytes and host metadata. `FApplicationServices::PreparePackage` configures compiled-entry host services after the environment is ready and before Session loading; image inspection does not invoke it. The optional `ProvidersReady` callback in `ConfigurePackageApplicationServices` supplies registered providers, their retained environment and an owning backend selector to embedding adapters. The selector preserves host option, application service and project default precedence, including bundled deployment defaults and explicitly disabled selections. GUI uses these hooks to prepare the standard `.vsapp` container without adding GUI or ZIP dependencies to Core. `PreparePackage` prepares bindings and resources only; it must not execute the entry program.

<!-- docs-navigation -->

---

[User guide](../../User/en-US/index.md) · [Developer documentation](../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
