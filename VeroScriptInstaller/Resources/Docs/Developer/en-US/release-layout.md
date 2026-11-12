# Business releases and independent versions

This page follows the sole [packaging and publication standard](../../../../../Resources/Docs/Standards/en-US/PACKAGING.md). Do not support or convert old layouts. Remove old Release outputs and obsolete candidates, then rebuild under the new rules. Product versions are defined by the release registry.

Use business-first `Release/<business-route>/<target>/<version>/`. Classify version contents by purpose into `archives/`, `sdks/`, `packages/`, `installers/` and `symbols/`. Shared packages use `any`, WebAssembly SDKs use `wasm32`, and native programs retain their real OS/architecture. Root `release-index.json` points to `indexes/<target>.json`; a target business may reference shared and native manifests together. The standard defines complete fields, ownership, payload and reference cleanup.

The shared registry and release scripts implement classified routes, format checks and acceptance bookkeeping. Distribution requires a successful real build and passing payload/inventory audits. Record target runtime and installer lifecycle outcomes separately, including failures or checks not run. Pure planning and static checks alone provide no build or runtime acceptance evidence.

Clean only verified owned outputs and reproducible temporary data without active users, protecting sources, credentials, useful toolchains and disks. Format changes do not migrate old collections; updates of valid new collections still require locking, recoverable commits, reference closure and hashes. Standards-only work runs documentation checks without actual packaging.

```text
Release/
  release-index.json
  indexes/<target>.json
  <business-route>/<target>/<version>/
    archives/
    sdks/
    packages/
    installers/
    symbols/
    release-manifest.json
    checksums.txt
```

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

Record actual payload targets in `supportedTargets` and permitted consuming release targets in `consumerTargets`. A wasm32 SDK may be referenced by native host indexes. Composed native/shared artifacts retain matching actual ecosystem versions.

## Planning and implementation entry points

Run from the repository root. `configuration` reads registered versions, routes, product coverage and format versions; the plan uses the same routing functions without building, staging or publishing artifacts. Repeat `--target` and `--business` to narrow the plan. Its `indexSelections` can reference both native and shared manifests; optional entries and wheel tags still need actual producer outputs.

For source inputs and build commands, see [build and bundle](build-and-bundle.md). For final inventory verification and its limits, see [release validation](release-validation.md). The commands below inspect metadata only; standards-only changes stop after the documentation checks above.

```text
python Resources/Tools/Packaging/veroscript_business_release.py configuration
python Resources/Tools/Packaging/veroscript_release_plan.py --target win-x64 --business Python --business Resources
```

<a id="release-qualification"></a>

## Inventory and acceptance records

Target-index `complete` describes the required business inventory. `qualificationStatus` summarizes recorded acceptance; `qualified` additionally requires complete inventory and satisfied required checks. Runtime or device failures and checks not run do not block distribution of built, integrity-verified payloads. Read the recorded status before claiming target readiness; absent records mean `not-recorded`.

Each business entry can carry `qualification` schemaVersion 1, bound to its `manifests[]` paths and SHA-256. `--records` supplies a JSON object mapping published business IDs to objects with `checks`. Each check has a real `status` and nonempty `reason`; `required` defaults to true and `evidenceSha256` is optional. A successful `build` and an explicitly required `target-runtime` check are mandatory. Use actual results, including `failed` or `not-run`; payload changes invalidate earlier records. The recorder updates the selected index and root digest under the collection lock.

```text
python Resources/Tools/Packaging/veroscript_qualification.py --repo-root . --release-directory Release --target win-x64 --records .tmp/acceptance-records.json
```

## Existing collection preflight

Before publication, existing target indexes must record `requiredBusinesses` exactly as defined by the current product registry. If that set has changed, choose a new empty dedicated output under `.tmp` or `dist` and rebuild the collection from source. The default `Release` output is checked by the same preflight. Plan previews include all registered desktop and mobile targets, including `win-x86`, unless `--target` narrows the selection.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](release-layout.md) · [fr-FR](../fr-FR/release-layout.md) · [ja-JP](../ja-JP/release-layout.md) · [ko-KR](../ko-KR/release-layout.md) · [zh-CN](../zh-CN/release-layout.md) · [zh-TW](../zh-TW/release-layout.md)
