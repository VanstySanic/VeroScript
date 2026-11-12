# Packaging and publication standard

Scope: [development of and contributions to VeroScript](README.md). This standard governs official collections, candidate builds and publication acceptance; it does not change external projects' licenses or packaging practices.

## Standard and implementation status

The shared registry and release scripts implement classified routes and the new format. Pure planning and static regressions do not replace successful real builds and payload-integrity checks. Record target execution and installer lifecycle results separately, including failures or checks not run. Accept only the new contract: no old-format compatibility, conversion, dual writes or migration. Remove owned old outputs and rebuild from source.

Update product registration, version resolution, pure planning, producers, installed payloads, manifests, verification, publication transactions, cleanup and CI together. Use explicit schemaVersion values; new readers accept only the new contract and safely reject old formats without compatibility branches. Product and format versions are independent; registered product versions are maintained separately. See [release layout](../../../../VeroScriptInstaller/Resources/Docs/Developer/en-US/release-layout.md) for implementation entry points. Old `migrate-layout` is not part of the new workflow.

## Directories and ownership

The formal root is `Release/` below the source root, independent of outer names such as `Script` or `VeroScript`. Moving a checkout preserves internal relative paths; rebuild caches affected by compiled paths. Candidates, build caches and acceptance logs belong in dedicated staging outside Release.

```text
Release/
  release-index.json
  indexes/<target>.json
  Runtime/<target>/<version>/
  CoreSDK/<target>/<version>/
  Interop/<target>/<version>/
  Resources/any/<version>/Resources/
  Modules/
    Execution/{Jit,Vm}/<target>/<version>/
    Network/{Json,Network,Http,WebSocket}/<target>/<version>/
    Network/Backend/{Boost,Curl}/<target>/<version>/
    Multimedia/{Platform,Graphics,Audio,Image,Font,Gui}/<target>/<version>/
    Multimedia/Backend/{Sdl3,Stb,FontBackend,RmlUi}/<target>/<version>/
  Extensions/
    Language/{Cpp,CSharp,Python}/<target>/<version>/
    Editor/VSCode/{VSCode,GuiVSCode}/<target>/<version>/
    Editor/JetBrains/{JetBrains,GuiJetBrains}/<target>/<version>/
    Platform/Mobile/{Android,iOS}/<target>/<version>/
    Platform/WebAssembly/<target>/<version>/
```

`<business-route>` includes the registered category path and business name. `<target>` denotes a distribution scope: a native/mobile target, `any` or `wasm32`, rather than the build host. `{A,B}` denotes separate directories.

Create only needed categories in a business version directory. Its manifest and checksums remain at the version root:

| Path | Purpose |
| --- | --- |
| `archives/` | Runtime distributions deployed by extraction |
| `sdks/` | SDK archives with development interfaces, build exports and required dependencies |
| `packages/` | Ecosystem packages: `.vspkg`, `.vsix`, plugin ZIP, `.nupkg`, wheel, sdist, AAR |
| `installers/` | Supported product installers and their chosen download containers |
| `symbols/` | The owning business's symbol distributions, including `.snupkg` |
| `Resources/` | Loose Assets, Docs, Scripts and Examples, exclusive to Resources |
| `release-manifest.json`, `checksums.txt` | Inventory and digests for this business, scope and version |

Classify by purpose and manifest role, not extension alone. Do not mechanically wrap ecosystem packages in another equivalent ZIP; an SDK needing extra headers or build resources may retain its complete archive. Unix containers preserve executable permissions and required library links. Default to one installer download representation; retain both bare and compressed forms only for distinct consumers. Distribution categories do not automatically change installed payload paths.

Runtime, CoreSDK, Interop and Resources belong directly to the root. Cpp, CSharp, Python, WebAssembly, four editor products and Android/iOS belong to Extensions; execution, network and multimedia modules belong to Modules. The package client remains part of Runtime/CoreSDK, without a new business. Registration must account for new products, dependencies, roles, capabilities and exclusion reasons. Completeness follows the selected target and release profile, not every registered business on every platform.

Directory spellings are fixed plurals: `archives`, `sdks`, `packages`, `installers`, `symbols`, `indexes`. Preserve source category spellings under Modules and Extensions, including Execution, Network, Multimedia, Language, Editor, Platform and Backend; keep business IDs and package names stable.

## Target scope and shared artifacts

Separate native distributions by OS and architecture, such as `win-x64`, `win-arm64`, `linux-x64`, `linux-arm64`, `macos-x64` and `macos-arm64`. Other registered architectures must state their supported profiles; registration alone does not qualify a complete release. `android` contains multiple ABIs. `ios` includes devices and simulators, with iPadOS using the same SDK. WebAssembly SDKs use `wasm32`; their host installers retain host targets.

`any` is the shared location for a distribution not split by one native target. It can contain platform-neutral files or one ecosystem package covering several targets, but must accurately declare `supportedTargets`, ABIs, TFMs, runtime dependencies and limitations. It does not mean every platform is supported. Android multi-ABI AARs and iOS XCFrameworks retain their platform scopes.

Store each shared business/version distribution once and reuse it through manifest references. The four current editor plugins, platform-neutral Resources, cleaned Python sdists and the identical Android/iOS C# NuGet belong in shared locations. Generate one canonical JS/WASM SDK; do not classify its native installer as universal. Mixed businesses may own both `any` and native directories: Python sdists use `any`, while wheels follow actual platforms and ABIs.

Merge artifacts only when business, version, purpose, support, dependencies and ABI contracts agree and final file hashes match. A shared filename or extension is insufficient. Establish canonical bytes under an explicit generation contract before manifests, signing and hashing. Owned text uses UTF-8/LF with required BOM and batch exceptions. Do not mechanically rewrite binaries, signatures, upstream originals or byte-sensitive fixtures. Formal distribution must not rely on cross-directory symlinks or hard links for shared references.

Standalone installers, wheels and offline SDKs still carry their declared runtime dependencies. Independent use takes precedence over disk deduplication; do not move required libraries into neighboring Release directories and assume downloaders have them.

Record actual payload targets in `supportedTargets` and permitted consuming release targets in `consumerTargets`. A wasm32 SDK may be referenced by native host indexes. Composed native/shared artifacts retain matching actual ecosystem versions.

## Independent versions and dependencies

Businesses and targets may version independently. `ReleaseVersions.json` supplies defaults, not one collection product version. Shared distributions have explicit versions too; a platform update must not silently replace content still referenced elsewhere. Indices pin selected versions and manifest hashes, never an implicit latest version. NuGet, wheels, sdists, plugins and binaries retain real versions; renaming files or inventing platform tags is prohibited.

Record native version, business version, public ABI, build identity and package-format version separately. Validate Cpp/Core, GUI add-on/base-plugin and module dependencies against actual versions, ABI, build identities or library hashes. Do not silently replace an externally published package identity with different bytes. Rebuilding an unpublished local candidate updates hashes and invalidates affected acceptance; its product-version policy must remain explicit.

One ecosystem package identity must resolve to one payload. Do not place differing NuGets or sdists with the same ID/version in platform folders and rely on directory names to distinguish them. Express multi-target differences through ecosystem TFMs, RIDs, ABIs or platform tags; if merging is impossible, explicitly define distinct distribution identities without violating existing package ID/version contracts.

Once a shared `any`/`wasm32` manifest is published into a managed collection, its business/scope/version identity is immutable, including optional symbols and declared support. Collect the complete shared inventory before sealing it; changes require a new business version. A clean rebuild may retain the configured version only after explicitly removing unpublished local outputs; deleting local files does not permit changing an identity already distributed to users.

`any`/`wasm32` manifests do not record the publishing host native version. C# native-version context belongs to each consuming target index business entry; shared identity depends on the package content and contract.

## Indices, manifests and references

`release-index.json`: schemaVersion **1**, product `VeroScript Release Set`; `indexes/<target>.json`: schemaVersion **5**; business `release-manifest.json`: schemaVersion **3**. Candidate `release-manifest.json`: schemaVersion **2**, product `VeroScript Release Candidate`. `checksums.txt` is per business/scope/version. Each indexed business records `manifests[]`, with explicit target, version, path and SHA-256; classification and artifact roles come from the shared product registry.

Root `release-index.json` is the collection entry point. It records layout/format versions, generation and relative paths/SHA-256 of `indexes/<target>.json`, without inventing a common business version. Target indices independently select releases; the shared index manages shared distributions.

One target's business entry may reference several manifests, such as Python's target-wheel and `any`-sdist manifests. Each reference includes explicit `target`, `version`, relative manifest path and SHA-256. Remove the old assumption of one same-target manifest per business. Different businesses and targets may select independent versions; shared and native components composed for one business in one target index must retain matching ecosystem/product versions and pass compatibility and completeness checks together.

Business manifests record business, distribution scope, actual version, build/source identity, `supportedTargets`, dependencies, roles and complete file inventory. Each file includes its classified relative path, size and SHA-256. Native records include OS, architecture, ABI, minimum OS/API, toolchain and slices; ecosystem records add Python ABI, TFM and other selectors. Generate `checksums.txt` from the same inventory. Target selection, paths, package metadata and binary identities must agree.

Resolve manifest files relative to their business version root and index manifest paths relative to Release. Reject traversal, absolute paths, escaping links, case/reserved-name aliases, duplicate entries, unknown files and reference cycles. Cross-scope references may address only managed manifests supporting that target. Complete validation follows the entire dependency/reference closure rather than inferring contents from directory names.

Inventory covers payload artifacts and loose resources, excluding its own manifest and `checksums.txt` to avoid self-referential hashes. Parent indices pin manifest hashes; the root entry pins index hashes. Record or verify file types, executable permissions and safe relative library links as appropriate to each format, rather than comparing regular-file bytes while losing executability. Hash validation does not replace publisher identity or the actual signing policy.

Indices distinguish publication of selected components from a complete platform release. Record the profile's required businesses, missing entries, partial products and unverified gates; component success does not certify an entire platform. Do not fabricate a successful index for a target without formal artifacts; keep its build/acceptance gaps in candidate reports.

## Functional payloads and source distributions

Runtime, SDK, Extensions, Modules and installers contain functionality, runtime resources, metadata and legal notices, without README, manuals, tutorials, examples, tests, benchmarks or history. Resources provides integrated User documentation and related resources. Standards are developer/contributor material in All only, excluded from User and functional packages. Preserve licenses, NOTICE, required localization, icons, syntax, snippets and runtime data. Development-only icon generators such as Node.js/Sharp do not become Runtime/SDK dependencies.

The VS Code VSIX root `extension/readme.md` is extension Details metadata. This specific presentation file may be included when it matches the maintained module README text and links to packaged resources or online guides. Other README files, manuals and Developer/Tutorials trees remain excluded from functional packages.

All Modules, including Jit and future modules, omit standalone product installers. Symbols are separate distributions owned by the corresponding business. Installers omit documentation/example choices and retain the bounded-page wizard and ownership rules. Deselection/uninstall of optional associations, icons and PATH changes removes only settings owned by that installation.

An sdist is an explicit buildable-source exception: include the minimal sources, headers, build scripts, metadata and legal notices needed to build after independent extraction; omit unrelated explanations and examples. Exclude and audit `.cxx`, `.gradle`, `.externalNativeBuild`, CMake caches, objects, compiled outputs and machine-local paths. Neither `.gitignore` nor a coincidentally clean host checkout is sufficient. Inspect actual archive members and rebuild consumers from an isolated sdist where needed.

Installing a module, enabling a package and selecting an execution backend/service provider are distinct operations. Host configuration follows the shared omitted/auto/none/explicit selection rules; auto may choose compatible enabled installed candidates. Publishing or installing must preserve the host’s backend configuration and disabled selections, and must not silently add optional Jit/providers to the base Runtime/CoreSDK payload.

Icons come from the official Assets directories and automatic resource inventory. New files enter version control and CI inputs; do not fix the inventory to a historical count. Compare SVG/PNG/ICO/ICNS and other raw or binary resources individually with their sources. Installed icon references resolve to stable product-payload paths. Rebuild referencing programs/installers after embedded icon changes; replacing an external ICO does not rebuild an EXE. Associations, refresh and uninstall continue under installer ownership contracts.

Source-aware verification also checks embedded Windows CLI, Runtime, launcher, LSP, DAP and installer EXE icon frames, including reused builds. The product PNG declared by VS Code is checked directly against the maintained resource.

## Android, iOS and module SDKs

The main Android SDK integrates Core/Interop, shared Mobile adaptation, JNI, AAR, standard library, required resources and licenses. The main iOS SDK integrates its static adapter and XCFramework. Shared Mobile adaptation is not another product users must install. Main SDKs do not thereby include every optional module.

`Modules/<category-route>/<business>/<android|ios>/<version>/sdks/` is an optional native CMake development distribution, not another main Android/iOS SDK or a desktop `.vspkg`. Preserve required headers, CMake exports and declared dependency closure; distinguish bundled/external dependencies and prevent conflicting or duplicate linking within an application. An existing main AAR does not justify deleting independently required C++ APIs or module development support.

A complete mobile release covers platform adaptation, CoreSDK, Cpp, Interop, Vm, CSharp, Python and selected optional modules. Ordinary applications consume the main SDK and needed modules, without downloading every development kit. Android currently includes arm64-v8a/x86_64. iOS includes device-arm64, simulator-arm64 and simulator-x86_64. Validate real binaries and consumer links per slice; equal CPUs do not imply equal OS or device/simulator ABI. Explicitly exclude unavailable native JIT on iOS rather than simulating support by renaming.

Select C# multi-target packages by actual TFMs, native libraries and resources; select Python by its declared CPython matrix and mobile slices. Match .NET, Xcode, NDK and target dependencies. Record SDK compilation version separately from minimum runtime OS.

## Cross-building and acceptance

Separate source/configuration preflight, real builds, candidate assembly, static validation, target execution, publication and final verification. Produce real cross-target artifacts when compilation is supported; lacking a local execution environment does not imply inability to compile. Successfully built and integrity-verified packages may enter Release even when runtime acceptance has not run or has failed. Preserve those states explicitly; a successful build does not establish runtime acceptance.

Record build-host and target identities separately. Cross-building uses target compilers, sysroots, headers and complete dependencies; executed generators use host versions. Qualify native programs, C# AOT, Python wheels, installers and third-party libraries individually instead of merely changing RuntimeIdentifier, names or tags. Desktop ARM64 does not inherit mobile ARM64 acceptance.

Acceptance distinguishes `passed`, `failed`, `not-run` and `not-applicable`, recording reasons, environment, toolchain, configuration and evidence digests. Track cross-compilation/linking, emulators, physical devices, desktop graphics, installation lifecycle, signing/notarization, C# AOT/trimming and Python applications independently. Real builds and payload, architecture, source, dependency, inventory and SHA-256 verification remain mandatory before distribution. Runtime/device gaps do not exclude successfully built payloads from the distribution. Never turn a skipped, failed, unsigned or missing-toolchain check into a pass. Each business `qualification` uses schema version 1 and binds checks to the exact paths and digests in `manifests[]`; payload changes invalidate old records. Target `complete` means required inventory completeness, while `qualificationStatus` summarizes acceptance. `qualified` is true only for complete inventory with all required checks passed. Missing evidence is `not-recorded`, never inferred success.

macOS installers use `.dmg` containing a standard `.app`, the official ICNS, Info.plist and executable payload. Linux uses an executable-preserving `.tar.gz`; Windows uses `.exe`. SDK archive formats are independent from installer formats. Finish code signing before imaging and verification; ad-hoc signing is distinct from Developer ID and notarization. DMG auditing on another OS requires the build tool 7-Zip (`VEROSCRIPT_7ZIP` or PATH), without adding a product runtime dependency.

Desktop cross assembly uses `Resources/Tools/Packaging/package-veroscript-desktop-cross.py`. The target compiler evaluates official header expressions to produce ABI strings; never copy host ABIs. When a target cannot execute, neutral declaration catalogs may be reused only with identical source inventories and matching exports from at least two native platforms. A 32-bit target must export its own catalog. Executable target tools/emulators integrate through `--target-tool`; catalog queries do not establish full runtime acceptance. Host installer tooling may build, hash-verify and archive foreign-target payloads; actual installation still requires the matching OS and architecture.

Complete signing, notarization and other byte-changing post-processing before recording final distribution hashes and binding acceptance evidence; validate internal payload inventories separately. Pre-signing hashes do not certify signed files. Recheck final directories, archives, reference paths and embedded payloads after publication; any byte change invalidates the corresponding evidence.

## Publication, cancellation and cleanup

Publication and cleanup share the collection lock. Verify candidates, support, reference closure and provenance before preparing managed files, manifests and indices. Commit business content, target indices and the root entry through a recoverable transaction, with the root entry last. Restore changed content and references on failure. Readers use a lock or generation/hash recheck and retry during an inconsistent update; do not claim one atomic collection-wide exchange.

Cancellation stops only this task's build descendants and transfer services, leaves unfinished candidates unpublished and formal artifacts intact, retains necessary diagnostics and restores temporary environment settings. Before resuming, check source, configuration, toolchain and candidate hashes; do not reuse stale acceptance.

Cleanup follows the reference closure of every target index and manifest dependency. Default to removing unreferenced old versions, retaining versions still selected by another platform, Cpp or a GUI add-on. A shared package becomes removable only after its last reference disappears. Unknown/unmanaged files or failed path checks stop cleanup; age and filenames do not establish ownership. Delete only verified dedicated temporary directories, never sources, user files or external dependency directories.

Before removing old-format outputs, verify ownership, protected sources, process use and reusable toolchains. Delete old Release artifacts and obsolete candidates/caches, retaining necessary diagnostics without old-package recovery copies. Do not keep the old collection alive by resolving old index dependencies. Rebuild, validate and publish from clean staging under the new rules, preserving the registered product versions. Protect valid versions still referenced within a new-format collection; that reference protection is not old-standard compatibility.

## Implementation gates

The new implementation must provide pure planning shared with producers, shared references/version selection, classification/ecosystem validation, cross-scope dependency/lock/rollback/reference-cleanup tests, rejection of contaminated sources and old formats, target capabilities/acceptance records, clean rebuilding and failure recovery. Until producers and verifiers satisfy these requirements, publish no new artifacts and do not certify the new layout with old planner output.

A standards-only update starts no compilation, installer generation or release packaging. Synchronize six languages, navigation and existing-implementation guides; run these temporary documentation checks without publishing Release artifacts:

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```
