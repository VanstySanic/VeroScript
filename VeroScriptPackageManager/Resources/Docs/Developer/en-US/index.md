# Package-manager maintenance

The client owns acquisition, ZIP handling, resolution, receipts, transactions, publication and environment composition. Core owns resolved module/resource/native contracts and stays independent of HTTP/archives/solving. Servers own accounts, authorization, immutable artifact storage, policy and indexing.

## Build and ownership

Desktop defaults to VEROSCRIPT_BUILD_PACKAGE_MANAGER=ON; embedded/mobile/Wasm/Python-only builds default off. Keep pinned static curl, miniz, nlohmann/json and PicoSHA2 ownership and licenses local. VEROSCRIPT_PACKAGE_MANAGER_USE_SYSTEM_CURL or an existing CURL target requires CURL 7.85+ and matching deployment dependencies. The bundled curl source is pinned by SHA-256.

The VeroScriptPackageManager install component exports headers, libraries, schemas, dependency identities and legal notices. Examples and README are not installed; publish the six-language documentation through Resources. With bundled curl, install its build component too; Core SDK packaging performs both steps. Official packaging then finalizes the dependency on the published Core SDK version and the SHA-256 of the actual staged Core library, including after stripping; checking only the native product version or an earlier build hash is insufficient. The external Core SDK consumer also creates the HTTP transport so that package-manager ZIP/HTTP link dependencies are exercised. Preserve the source API’s English diagnostic message and structured code contract independently from package display snapshots.

## Transactions and lifetime

Stage source snapshots and generated data, then publish immutable content and commit requirements/lock/state/ownership with recoverable journals. Precommit revalidates all planning inputs and storage identity; preserve external edits and reject stale plans. Recovery and rollback must not inherit an already cancelled token. A reader must reject an incomplete transaction.

Every generation keeps a cross-process file lease. clean checks receipts, all target configurations, ownership, contents and live leases; preserve changed/foreign files. Recovery may collect published orphan generations only with complete owned receipts and lease metadata. Hosts separately manage abandoned staging. Callbacks are synchronous borrowed access; avoid reentry into the same environment. Native implementations retain snapshot/module leases through final destructors.

Transaction replay validates the entire journal (unique allowed destinations, string/null data and per-record budgets) and current file bytes before writing. Each file must equal its journal `old` or `new` value; other content returns `recovery-conflict` and retains the journal and external edit. Replay checks again at each write and skips values already restored. If rollback cannot proceed, `recovery-required` preserves the pending transaction. Back up external edits, restore a matching recorded value, finish recovery, then reapply the edits. `JournalRecoveryTests` covers partial commits, rollback, missing files, malformed logs, duplicate paths and edits during application.

## Hook compilation

`PackageInstallProcessing.cpp` uses Core's public compile, analysis-control and VM services. `FLimits.HookConstantEvaluationLimits` is passed to `FCompileOptions.ConstantEvaluationLimits` for every preflight/execution compilation; it is host policy, not package JSON or a lockfile field. Work, call-depth, expression-depth and memory limits must be positive; zero cache is supported. Keep compilation under the package operation's deadline/cancellation while preserving the caller's analysis observer and cancellation diagnostic. Runtime instruction, time, memory and numeric policies remain independent. `ConstantHookTests` covers source and binary-library imports, generic discarded branches, exact uint64 constants, limits, one-shot cancellation, uncached retry and transactional uninstall.

## Verification

Build VeroScriptPackageManagerTests and run VeroScriptPackageManagerCliTests plus VeroScriptPackageManagerSdkTests. Cover immutable sources, offline caches, target solving, optional usage, archives and names, scoped credentials, language/format validation, typed/secret inputs, hook budgets, interrupted transactions, concurrent edits, cleanup leases, native analysis/static/dynamic registration, explicit JIT and deployed closures. Run the installed SDK consumer against a mismatching core as a negative case; platform/device acceptance remains separate.

```sh
cmake -S . -B build/package-manager -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVEROSCRIPT_BUILD_PACKAGE_MANAGER=ON
cmake --build build/package-manager --config Release --target VeroScriptPackageManagerTests VeroScriptCli
ctest --test-dir build/package-manager -C Release --output-on-failure -R VeroScriptPackageManager
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

[Registry protocol](registry-protocol.md) · [User](../../User/en-US/index.md) · [Schemas](../../../Schemas) · [Examples](../../../Examples)

## Dependency-list files

`PackageRequirements.cpp` expands positional install arguments and `FRequest.RequirementFiles` before environment mutation. Preserve UTF-8 validation, include-cycle detection, source/starting-line diagnostics and the MaxManifestBytes/MaxMetadataBytes/MaxFiles/MaxDependencyDepth/MaxEdges budgets. Merge repeated constraints only with compatible source/settings. Retain input hashes and recheck them before commit; a concurrent edit returns requirements-changed. File contents cannot enable hooks or alter registry policy. Cover CLI short/long options, C++ requests, nested and quoted local paths, constrained registry solving and locked/offline replay. Protocol fields and diagnostic code values remain stable; file entries are user input and are not localization catalogs.

## Dependency-list service

`FRequest.RequirementFiles` exposes the same install-only service as CLI `-r` / `--requirement`. Top-level relative paths require `Environment.WorkingDirectory`; nested paths use their containing file. `Private/PackageRequirements.cpp` parses inputs before management writes and bounds UTF-8 reads, includes, entries and aggregate metadata through `FLimits` / `FWork`. Source hashes are checked before the install commit; unchanged lists need not be shipped with a locked project because canonical dependencies and sources are stored in `veroscript-project.json` / `veroscript.lock`. Explicitly using `-r` again requires the selected lists and local archive sources to remain available. No core, registry endpoint or format-version change is needed. Include the [dependency-list cases](../../User/en-US/index.md) in library, CLI and installed-SDK acceptance.

## Environment and output integrity

Read-only selection rejects an interrupted first installation even when no project/state record exists. A leftover `committed` journal is readable only after validating its identity, destinations and every current file against `new`; readers never remove it. Budget the longest journal phase before the first write.

Online refresh treats artifact `signatures` as immutable lock metadata. `pack` and `deploy` publish staged output atomically without replacing an existing destination. Files may use a safe hard-link fallback; directories require native no-replace rename support. Unsupported operations fail without an overwrite fallback.

Composition uses the snapshot’s verified dependency edges, including deployment graphs with compile-only dependencies omitted. Processed packages require matching configuration and the complete selected dependency context. Native loaders without installation processing require matching immutable identities only for the external library providers they actually reference. Otherwise return `combine-processing` and install the desired graph together. Unrelated script dependencies do not invalidate a loader.

Deployments retain each package’s configuration in both its package value and the project snapshot, including when reused as an overlay. Deployment output directories accept a trailing separator. `list` and `show` reject an interrupted first installation even before state creation. Before commit, verify every written and retained management record without further host callbacks, keeping the original deadline. Rollback preserves external edits; conflicts with written records retain the recovery journal.

## Application and build integration

`VeroScriptPackageRuntime` exports frozen environment loading, resource/native registration and deployment consumption without curl, solving, installation, hooks or publication. The full manager links it. Core's optional artifact decoder can be supplied through `FClientOptions.ArtifactDecoder` for archive trust and through `FCoreEnvironmentOptions.ArtifactDecoder` for protected library imports. Native keys and callbacks are never serialized into locks. Signed archive consumption without a matching trust service fails. A configured required-signature policy also rejects unsigned input. Cache hits are subject to that same verification before extraction.

Deployment exports runtime usage by default; source applications explicitly retain runtime and compile usage. Deployment publication and consumers cooperate through `AcquireDeploymentDirectoryLease`; publishers must hold an exclusive lease for replacement, and consumers retain shared leases. Native bindings/resources retain their immutable environment owners. `Build::FServices` supplies both `Deploy` and `LeaseDeployment` callbacks, never invokes install as a fallback.

### Registry backend changes for signed distributions

Existing endpoints remain unchanged: search, package/version details, artifact download, upload allocation/PUT, and append publication. Add optional artifact `signatures` with the common nonempty array of at most 16 records. Each record binds kind `distribution`, the final archive SHA-256, canonical decimal byte length, algorithm and public key ID/version. The same array is sent in upload allocation and publication, returned in version artifact records, persisted in recovery/lock records, and preserved through mirrors. No private keys are sent. Omitted signatures are different from an invalid null/empty array.

The server must calculate archive length/digest itself, validate the record structure and subject binding, preserve records exactly, and keep an existing artifact's digest/length/manifest/target/requirements/signatures immutable. Same-version append still requires identical package metadata and disjoint targets, atomically rejects overlap, and uses the existing idempotency key. Recovery confirmation includes signatures; dropping or replacing them is not a successful commit. Authentication/publisher authorization remains independent of client signature trust. Unsupported backends must reject the optional field clearly rather than silently discard it. Installation distribution containers remain standard ZIP; application containers are not implicitly accepted by the registry.

CLI: `veroscript install name --application-options /absolute/trust-options.json` configures the optional native service. The host options use the Application schema and absolute paths; `requireSignature` is a consumer policy. Local archives use `<archive>.sig.json`. Direct URL installs do not guess a second download URL for signatures; use registry artifact metadata or download the archive and its explicitly supplied sidecar locally. Install hooks remain separately authorized.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md) · [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
