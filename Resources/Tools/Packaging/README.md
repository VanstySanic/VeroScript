# VeroScript packaging tools

The sole [packaging and publication standard](../../Docs/Standards/en-US/PACKAGING.md)
([简体中文](../../Docs/Standards/zh-CN/PACKAGING.md)) requires business/target/version
directories with `archives`, `sdks`, `packages`, `installers` and `symbols` categories.
Shared releases use `any`; WebAssembly SDKs use `wasm32`. Root `release-index.json`
references `indexes/<target>.json`, and target businesses may reference multiple
shared/native manifests. Collection versions never replace independent product versions.

Do not support, convert, dual-write or migrate old release formats. Remove obsolete
owned release outputs and unused build data, preserve sources, credentials, useful
toolchains and diagnostics, then rebuild from clean staging. Product versions stay
at `0.1.1`. Producers, planning and verification share classified business routes,
artifact categories and explicit shared-manifest references. Old-format readers
and the migration command are removed. Static contract checks do not replace
clean target builds, runtime/device acceptance, signing or installer lifecycle checks.

The tools locate the source root from `Resources/Tools/Packaging`, independently
of the working directory or outer checkout name. Keep internal relative paths
when moving a checkout. Use dedicated staging outside Release and refresh caches
that depend on compiled source paths. New publication uses the collection lock,
recoverable commits, explicit target/profile acceptance and reference-based cleanup.

Standalone documentation uses `package-veroscript-docs.ps1`, with default output
under `dist/docs/<Audience>/<version>/`. It exports independently of managed
product publication; full Resources releases carry the User view. See
[documentation publication](../../Docs/Developer/en-US/index.md#integrated-publication-views).

Plural spellings `archives`, `sdks`, `packages`, `installers`, `symbols` and
`indexes` are fixed even for a single item.
Business IDs and established proper names retain their registered spelling.

Full desktop builds stage all six C# package IDs and their matching `.snupkg`
files in the CSharp `any` distribution. Set
`VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY` to the already qualified canonical feed
when combining hosts or using the four-framework mobile-enabled runtime package.
The mobile collector uses the same complete inventory when this variable is set,
and rejects explicitly supplied C# inputs that differ from the canonical feed.
The same bytes populate the SDK/installer offline feeds; a host must not repack
an already published NuGet identity. SDK validation rejects offline copies that
differ from the canonical distributions. Offline feeds remain functional SDK
inputs; they are separate from optional authored documentation.

The root set has schemaVersion 1 (`VeroScript Release Set`), target indexes use
schemaVersion 5, and business manifests use schemaVersion 3. Each indexed business
has `manifests[]` with scope, actual version, Release-relative path and SHA-256.
Business leaves use `release-manifest.json` and `checksums.txt`. Shared manifests
do not inherit host native versions; C# native-version context is carried by
the consuming target index business entry. Native candidates
are temporary, use schemaVersion 2 (`VeroScript Release Candidate`), and cannot be
written directly inside managed Release. They are not a compatibility reader.

Execution/Network/Multimedia (including Backend) and Language/Editor/Platform
classifications mirror the source registry. Actual payload targets and permitted
consuming targets are recorded separately. Platform-neutral Resources, editor
plugins, Python sdist and unified NuGet use `any`; the portable WebAssembly SDK
uses `wasm32`. Native WebAssembly installers retain the host target. Shared
same-version manifests are immutable: collect optional symbols together with the
main package before sealing, or publish a new version. A source rebuild after
intentional full cleanup of unpublished local outputs may keep the configured
product version. An identity already distributed to users requires a new version
when its content changes, even after local cleanup.

Every publication uses the collection lock, restores all affected payloads and
indexes on failure, and commits the root digest index last. Cleanup validates
ownership and follows references and dependency pins across every target.
Unknown files or directories stop cleanup. macOS installers are standard .app bundles in verified .dmg downloads; Linux
installers use tar.gz downloads. Bare executables remain internal build/test
inputs when a wrapper is present. Canonical ZIP creation fixes timestamps and preserves bytes.

## Native module layout and JIT distribution

Native source modules export headers directly from `Public/`; implementations
and tests live under `Private/`. Packaging keeps the SDK's module-specific
`include/Script`, `include/Tooling`, `include/Cpp`, and tool header directories.
The Core SDK also carries the build-generated `VeroScriptJitBuild.h`. Source
publication audits compare its ABI digest with the normalized, sorted current
Core public headers and reject missing or stale contracts and extra headers.
Earlier versions in the current format are verified from recorded checksums; pinned dependencies remain protected.

The release registry currently defines 32 businesses: four main products, ten
extensions (including both GUI editor add-ons and the Android/iOS SDKs), and 18 optional modules.
Desktop publication selects 30 businesses; each mobile target has
its own completeness contract, rather than requiring desktop tools/installers.
`veroscript_products.py` owns the source inventory and product routes;
`ReleaseVersions.json` supplies independent product versions. Source preflight
rejects unregistered editor projects or public modules under `Modules`.
Jit is an independent functional archive,
with its own version in `ReleaseVersions.json`. Runtime and Core SDK do not
bundle the AsmJit runtime. With a module environment, an omitted JIT selection
inherits `services.jit.backend`; when unset, `auto` selects the first compatible
usable enabled candidate in ascending backend-ID order. Installing a compatible
Jit package makes its enabled backend available to that selection. Hosts without
a module environment must provide a backend factory or explicitly select its
library/directory. CLI `--jit-backend=auto` overrides the configured choice,
`--jit-backend=none` disables JIT, and IDs/files/directories allow explicit selection.
The native public ABI must agree; sharing a product version alone is insufficient.
Standalone Jit packaging checks the build-generated ABI against the current Core
public headers before staging. On the native target host, it also checks the
binary's descriptor ABI and factory entry points before verifying CLI loading.
Reconfigure and rebuild the backend and CLI after public API changes; an older
pair that can still load each other is not sufficient for a current-source package.

```text
python Resources/Tools/Packaging/package-veroscript-jit.py --build-directory build/windows-msvc-vs2026 --target win-x64
```

Full packaging creates the Jit archive under `Release/Modules/Execution/Jit/<target>/<version>/archives` alongside the other
businesses. Its archive includes the backend's runtime dependencies and license
notices. A selected or partial release is audited against its recorded business
selection; it does not imply that omitted businesses were rebuilt or published.

The official package name and archive prefix are `vs-asmjit`, for example
`vs-asmjit-0.1.1-win-x64.zip` or `vs-asmjit-0.1.1-linux-x64.tar.gz`.
The service backend ID is `asmjit`; native library names and ABI handshakes are
independent of distribution names. Official installable packages use the `vs-`
prefix. The official registry must reserve that prefix for authorized official
publishers; a prefix alone does not establish a package's origin or trust.

These ZIP/TAR release archives retain an enclosing directory for manual deployment.
Registry installation uses a separate ZIP distribution (`.vspkg` or `.zip`) with
`veroscript-package.json` at its root. Its manifest uses `name: vs-asmjit`, a
`jit` backend entry with `id: asmjit`, and the matching target JIT ABI. The Jit
packaging entry now produces both distributions; build the matching
`VeroScriptModulePackageTool` or provide `--package-tool <file>` (`--portable-only`
keeps manual archive packaging available in builds without a package client). The tool checks
the actual Core/backend contract and uses the public `pack` operation. Upload
the `.vspkg` through `veroscript publish`; keep the enclosing-directory archive
for manual deployment.

```text
veroscript install ./vs-asmjit-0.1.1-win-x64.vspkg --path ./MyProject
veroscript --path ./MyProject --jit-backend asmjit --backend jit ./MyProject/main.vs
```

Script-enabled foundation packaging also emits `.vspkg` files for Gui, SDL3,
RmlUi, Image, Font, Stb, FontBackend, Platform, Graphics and Audio alongside the
unchanged SDK archives. Data-only SDK packaging retains its existing behavior.
Installable packages omit SDK headers, CMake exports, static archives and debug
symbols, retaining runtime data, dynamic libraries, SONAME aliases, binding
catalogs and licenses. Dependencies name other installable packages; CoreSDK
and Cpp are build-time SDK contracts rather than installable runtime dependencies.

The current Gui native module forwards to the host's `veroscript.gui.tools`
service. Use a matching GUI-enabled Runtime/CLI or a host explicitly assembled
with GuiScript. Installing `vs-gui` does not add that service to a pure Core
host. SDL3, RmlUi, Stb and FontBackend remain independently optional providers;
select them through their service's options or project `services` configuration.
The package installation operation does not select providers. Release consumers
verify offline locked restoration, actual provider creation without windows,
headless GUI script execution/analysis, and disabled/uninstalled selection.
The CTest extension-package integration suites run in Release configuration,
matching the SDK packaging inputs and redistributed runtime libraries.

Build script steps use the VM configured in
`FServices::Compile.ScriptCompileExtensions`; the desktop build command explicitly
uses its product VM. The step still runs with the existing instruction, time,
permission and memory limits. A missing VM is a `script-vm` error. VM library
inputs participate in build receipts so changes invalidate cached generators.

`FCoreEnvironmentOptions::ScriptCompileExtensions` carries the trusted host policy
into installed compilation environments. Package entries supply their roots;
Core still intersects environment limits with each compile request. The CLI
passes explicit compiler deadlines/instruction limits into this frozen policy.

### Distribution groups and module policy

`veroscript_products.py` owns business routes and installer eligibility for Python
and PowerShell. Runtime, CoreSDK, Interop and Resources stay directly under each
collection root. Cpp, CSharp, Python, WebAssembly, VSCode and JetBrains belong to
`Extensions`, together with GuiVSCode, GuiJetBrains, Android and iOS. Jit, Vm, Json, Network,
Http, WebSocket, Boost, Curl, Platform, Graphics, Audio, Sdl3, Image, Font, Stb,
FontBackend, Gui and RmlUi belong to `Modules`. Each business
retains its independent version and completeness requirements.

The optional package client belongs to Runtime and CoreSDK; its native SDK,
schemas and link dependencies belong to CoreSDK. It has no separate release
business or installer. Staging records the published Core library hash after
symbol processing and the independent CoreSDK version in its CMake dependency
contract. External Core SDK consumer checks also link and run the package-manager
API when this component is included.

Official Runtime, SDK and extension products contain functional payloads, required runtime resources, package/API/ABI metadata and legal notices. Documentation, tutorials and examples are distributed through Resources. Packaging removes documentation/example/test/benchmark/history directories and README/CHANGELOG/CHANGES/CONTRIBUTING files, including localized filename variants; do not rely on these being present in SDK archives, NuGet packages, wheels or IDE plugins. Keep localization, icons, syntax, snippets and runtime dependencies. Consumer checks use source-owned example projects against the staged SDK, so example trees are not copied into distributions. CMake install contents are module-specific: some components omit authored manuals and examples, while modules such as Gui and Json still have explicit install rules. The final functional-payload filter applies to release archives regardless of these source install rules; publish authored guides and examples separately through Resources. The Tool build-bundle command also omits docs/examples components and filters auxiliary content in supplied payloads. A custom requiredFiles entry must name a retained functional file. Modules additionally exclude debug symbols and never produce new standalone installers. Mobile SDKs follow the same functional payload policy while retaining required device acceptance tools. This policy does not change arbitrary user packages handled by the package manager.

All Modules are functional packages and never provide standalone installers.

## Icon packaging contract

The authoritative four icon families are `Resources/Assets/icons/veroscript-icon.*`
and `Resources/Assets/file-associations/icons/veroscript-{source,app,package}.*`.
The Resources inventory discovers the current SVG/PNG/ICO/ICNS files and keeps
their original relative paths, alongside the separately maintained alternate
artwork. Do not regenerate icons during packaging: Node.js and `sharp`
are development tools only. The icon-input CI workflow validates their Git tracking,
format sizes, and the PNG frames embedded in ICO/ICNS containers.

Runtime owns the installed file-icon payload. Paths are relative to the installed
`runtime` component root, not the installer executable or a temporary bundle:

| Target | Installed files under `assets/icons/` |
|---|---|
| Windows | `veroscript-icon.ico`, `veroscript-source.ico`, `veroscript-app.ico`, `veroscript-package.ico` |
| macOS | The same four stems with `.icns` |
| Linux | The same four stems with `.svg` and `-256.png` |

These are required functional assets, not optional documentation. SDK and module
products do not acquire file-association features or extra icon payloads through
this change. The installer session owns registration, icon refresh and uninstall
cleanup; it must resolve icons from the installed component's recorded root.

The installer-owned association acceptance contract is:

- Offer `.vs` and `.vsapp` run-handler registration as explicit, independently
  selectable options, disabled by default. PATH remains a separate choice.
- Offer `.vspkg` icon registration separately, disabled by default. It adds only
  an extension icon when none is registered, without installing a package on
  double-click or replacing an existing opener. Windows may use the selected
  default application's icon instead.
- Offer setting the selected types as defaults separately, also disabled by
  default and available only when their Runtime handler is enabled. On Windows,
  request the user's choice through the supported system UI after installation
  succeeds; never write protected `UserChoice` data or report a requested default
  as applied without checking the actual system choice. See the
  [Windows app defaults platform](https://learn.microsoft.com/en-us/windows/apps/develop/windows-integration/default-apps-platform).
- Run handlers pass the quoted file path to the installed Runtime executable.
  The existing editor-oriented `.vs` template is not the Runtime run-handler
  definition. Registering a handler does not execute a script during installation.
- Record which per-user registration entries the installation created or owns.
  Turning an association off during an update, or uninstalling, removes only
  entries still owned by that installation. Preserve other applications' entries,
  later user changes, and registrations now owned by a newer installation.
- Compensate owned registration changes on failure/cancellation. Repair may
  restore previously enabled handlers without reclaiming a user's changed default.
  Older records without association ownership imply no association cleanup.
- Uninstall removes the owned run commands and icon references. Restoring a prior
  default application is subject to the operating system's default-app mechanism;
  it is not an unconditional registry rollback. Acceptance must exercise enabling,
  opting out, updating, cancelling and uninstalling with an existing competing app.

The Windows installer implements this contract in `FileAssociationRegistration`
and its optional wizard page. Run handlers use installation-specific ProgIDs,
`OpenWithProgids`, `Capabilities` and `RegisteredApplications` in HKCU. They never
write extension defaults or protected `UserChoice`. The default-app checkbox
requests Windows Settings after installation commits; the installer reports that
request, not an assigned default. Unchecking it cancels this operation's request,
without resetting a choice already made in Windows.

`fileAssociations` in `.veroscript-install.json` records the stable owner ID,
selected options, exact owned values and newly created keys. Updates retain the
owner ID during relocation. Changed values lose ownership and are not reclaimed
by later repairs; foreign owner markers protect the corresponding registration.
Cleanup compares Windows registry paths and names without case sensitivity,
checks both the marker and each value, deletes only matching entries,
and prunes only recorded keys that remain empty. Existing package icons are
preserved rather than overwritten. Transaction journals persist value changes
before registry writes so failures, cancellation and interrupted operations can
undo them with the installed files. Successful changes notify the Windows shell;
rollback notifies only after the original files and icon paths are restored.
Older records without this field never infer association ownership.

The installer tool exposes `--associate-vs`, `--associate-vsapp`, `--package-icon`
and `--choose-defaults` on `install`. These flags describe the desired options for
that installation/update; omitted association options are disabled. `repair`
preserves recorded choices and does not request default-app changes.
Run `VeroScriptInstaller/Tests` and `VeroScriptInstaller/UiTests` before packaging.
The Windows installer CI uses synthetic files and isolated registry/in-memory
stores. Actual Explorer/default-app acceptance remains a native candidate check.
Runtime icon staging and release verification alone never register handlers or
change defaults. Linux/macOS do not offer these Windows registration options.

Runtime staging copies only the target's required files. Installer packaging
checks the Runtime input and its generated bundle, including each icon's manifest
path, size and SHA-256. A stale Runtime archive must be rebuilt before repackaging
its installer. Source-aware release verification compares Runtime archive entries
and Resources against the current source bytes, even if outer hashes were renewed.
Historical collection verification without `--check-source` retains its recorded contract.

```text
python Resources/Tools/Packaging/veroscript_icons.py check --require-tracked
python -m unittest discover -s Resources/Tools/Packaging/tests -p test_veroscript_icons.py -v
```

Installer builds continue to compile the formal `veroscript-icon.ico` and
`veroscript-icon-256.png` through the App project. Every installer publication runs
`dotnet publish`; copying a new external ICO beside an old executable is insufficient.
The normal publication transaction regenerates manifests, checksums and release
indices after validation. Icon refresh alone does not change product versions.

Windows CLI, Runtime, launcher, LSP and DAP binaries embed the primary ICO through
the shared CMake resource helper. Source-aware packaging checks their embedded
icon frames in Runtime and SDK archives, including reused builds, and checks the
installer apphost independently of its bundle audit cache. These checks read PE
resources on every build host and reject stale or missing icons. Cross-building
a Windows installer selects its icon from the target RID.

VS Code product PNGs are copied from the maintained 256-pixel product resource
during extension builds. Source-aware VSIX verification compares the declared
icon directly with that resource, including from a clean checkout; PNG/ICO/ICNS
resources are compared byte for byte. JetBrains SVG resources retain their
existing source-aware JAR checks.

## Installer wizard validation

The desktop installer uses Next/Back wizard pages instead of a scrolling form.
Functional components are paged in bounded groups; documentation and examples are
not interactive installation options. Update detection fills only choices that
the user has not edited. Legacy records remain readable for repair and uninstall.
Before packaging, run the headless UI checks below. They exercise six languages,
layout, keyboard navigation, cancellation/rollback and temporary synthetic installation flows;
native candidate acceptance remains part of release validation.

Installer builds and tests require the .NET 9 SDK selected by
`VeroScriptInstaller/global.json`. Run direct `dotnet` commands from that directory.
Use `package-veroscript-installer.ps1` for Windows Runtime installers and
`package-veroscript-installer.sh` on native Linux and macOS hosts. Use the desktop
cross-target entry when the host cannot run the target payload.

```text
cd VeroScriptInstaller
dotnet run --project UiTests/VeroScriptInstaller.UiTests.csproj -c Release -p:SelfContained=false -p:PublishSingleFile=false
```
Official module payloads exclude documentation, tutorials, examples,
tests, benchmarks and debug symbols. Preserve required runtime resources, package
manifests, dependency/ABI metadata and legal notices, including `LICENSE.md`.
Module documentation and examples are published through Resources. This policy applies
to official module staging/publication, not arbitrary user resources handled by
the package manager. ZIP/TAR archives do not become `.vspkg` files by renaming;
package-manager distributions require a valid `veroscript-package.json` contract.

When adding a module, register it in `MODULE_BUSINESSES`, add its version and
artifact/functional validation, and use the shared business route. Do not add an
installer. Custom installer definitions with `ReleaseGroup: "Modules"` are
rejected. Historical Jit profiles remain readable for existing installations.

The documentation catalog uses `$module/` routes for module-owned inputs.
Python packaging is owned by `Extensions/Language/Python/pyproject.toml`; its metadata
provider and wheel test runner live in
`Extensions/Language/Python/Resources/Tools/Packaging`. Run
`python -m build Extensions/Language/Python` from the repository root. Python source
distributions include their native dependencies under `_native/` and can build
without the checkout. See the
[Python packaging guide](../../../Extensions/Language/Python/Resources/Docs/Developer/en-US/toolchain-and-packaging.md#python-package).

## IDE plugin distributions

Full desktop packaging registers VSCode, JetBrains, GuiVSCode and GuiJetBrains.
Each has its own version in `ReleaseVersions.json`. Keep VS Code package/lockfile
versions and JetBrains `pluginVersion` aligned with their own business version.
Artifacts are collected under `Release/Extensions/Editor/<family>/<business>/any/<version>/packages/`:

- VSCode: `VeroScriptVSCode-<version>.vsix`
- GuiVSCode: `VeroScriptGuiVSCode-<version>.vsix`
- JetBrains: `VeroScriptJetBrains-<version>.zip`
- GuiJetBrains: `VeroScriptGuiJetBrains-<version>.zip`

GUI add-ons depend on their matching base editor product and public tool API.
Selecting an add-on builds/tests its base dependency as well; only the selected
businesses are collected for publication. Native Runtime and providers remain
separate products. Packaged VS Code tests install the base and add-on together;
GUI checks also exercise the explicitly supplied GUI-enabled Runtime. GUI native
window/provider acceptance remains a target-host gate.

GUI candidates record the verified base version and artifact SHA-256 in
`ideDependencies`; business manifests retain this as `dependencies`. Publication
rejects a missing or different base before replacing any products. A GUI-only
publication requires that exact base to be published already; otherwise select
the base and add-on together. Rebuilding an archive can change its SHA-256 even
without a version change. Keep the base artifacts tested by each GUI add-on
until that add-on updates its dependency. A newer base can publish independently
under a different version; pruning preserves the base still pinned by the GUI.
Use `package-veroscript-ide.py` for GUI publication so paired acceptance is recorded.

Use the Node/npm versions declared by each plugin, JDK 25, and the pinned Gradle
wrapper. For joint JetBrains validation, set
`VEROSCRIPT_JETBRAINS_IDE_PATH` to the matching Rider installation (currently
2026.2.2). Set `VEROSCRIPT_JETBRAINS_VERIFIER_JAR` to a pinned official verifier
CLI, or make it available in the chosen `GRADLE_USER_HOME` cache. The packager
passes both local ZIPs to the [official Plugin Verifier](https://github.com/JetBrains/intellij-plugin-verifier#check-plugin),
checks both verdicts and the resolved base version, and retains reports under
`.tmp/veroscript-ide-verification-*`. Source-only preflight checks version metadata,
product coverage and declared dependencies without building or packaging:

```text
python Resources/Tools/Packaging/package-veroscript-ide.py --check-sources
python Resources/Tools/Packaging/package-veroscript-ide.py --check-sources --check-prerequisites
python Resources/Tools/Packaging/veroscript_business_release.py configuration
```

Full packagers also check required Node/npm, Java, the selected Rider SDK and
the joint verifier before starting the native build. `--check-sources` alone
remains a metadata check that does not require those build tools.

Preview the expected directory tree without building or writing to `Release`:

```text
python Resources/Tools/Packaging/veroscript_release_plan.py
python Resources/Tools/Packaging/veroscript_release_plan.py --target win-x64 --business GuiVSCode
```

The JSON describes the current implementation using registered versions, all native
and mobile target profiles, and the same business
routes as current publication. `null` denotes a file and `**` a resource
tree. It describes the full functional profile; wheel tags and optional artifacts
still require actual build results. The preview does not certify existing binaries.

To rebuild just the IDE businesses, use an extracted matching Runtime, an
optional compatible Jit module, and a dedicated empty staging directory:

```powershell
python Resources/Tools/Packaging/package-veroscript-ide.py `
  --runtime-directory C:/VeroScript/Runtime `
  --out-root .tmp/ide-candidate --target win-x64 --target linux-x64 --publish
```

Builds and required payload checks complete before publication. Runtime or device
acceptance gaps are recorded separately, as described below. Publication uses the
same verified business manifests, recoverable publication and obsolete-version
cleanup as native businesses, preserving other business releases.

## VM and application deployment contracts

The desktop Runtime ships its executable-relative `Vm/` backend; CoreSDK ships
`bin/Vm/`, the static VM SDK and its CMake execution-contract exports. The installer
requires these payloads. Interop and WebAssembly may assemble the VM statically;
validate their own product layout rather than copying desktop DLLs into every host.
The independent `Vm` business publishes `vs-bytecode-vm` manual archives and
installable `.vspkg` packages, without a product installer.

`VS_VEROSCRIPT_VM_VERSION` configures the VM product descriptor and CMake version.
Full packagers pass the registered Vm version; a stale binary/cache is rejected.
This product version is independent of Core and service ABI identities.
Application, Build and PackageRuntime remain components of Runtime/CoreSDK.
SDK finalization binds Application and PackageRuntime CMake dependencies to the
published CoreSDK version and the final staged Core library hash, including
when the optional package-management client is disabled.

Current SDK manifests declare Application, PackageRuntime and Build capabilities.
Release verification requires the corresponding headers, static libraries, CMake
exports and legal notices, and checks the finalized Core version/build pins.
When Application and PackageRuntime are both present, the ApplicationPackages
adapter's headers, library and exports are required too. Source audits compare
the shipped Application, Build and package-manager schemas with their originals.
Application runtime receipts must describe the expected executable, VM and
standard-library closure for their target. Verification streams the archive
members to compare every recorded size and SHA-256, including large binaries;
the final receipt is generated after symbol processing and license staging.
Receipt file contracts, executable permissions and the complete shipped runtime
resource inventory must agree with the native application reader.

## Python wheel matrix

Complete Python business publication builds a source distribution and a wheel
for every CPython minor version declared by `requires-python` in
`Extensions/Language/Python/pyproject.toml` (currently 3.11–3.15). The
`cibuildwheel` selection must describe the same versions. Each wheel is built
from that source distribution in a separate source/build directory, installed
into a fresh environment using its matching interpreter, and checked by all
installed-package suites selected by
`Extensions/Language/Python/Resources/Tools/Packaging/test-wheel.py` and `pip check`.

Install the matching native CPython interpreters before a complete build. The
packager discovers versioned `python3.x` commands and the Windows `py` launcher.
Use `-PythonExecutables` on Windows or repeated `--python-executable` arguments
on Unix to select explicit interpreter paths. Alternatively, set
`VEROSCRIPT_PYTHON_EXECUTABLES` to a path-separator-delimited list (semicolon on
Windows, colon on Unix). Missing versions, incorrect architecture, debug builds
and free-threaded interpreters are rejected before the native build starts.
The packager does not install interpreters automatically.

```powershell
./Resources/Tools/Packaging/package-veroscript-all.ps1 -PythonExecutables @(
    'C:/Python311/python.exe', 'C:/Python312/python.exe',
    'C:/Python313/python.exe', 'C:/Python314/python.exe',
    'C:/Python315/python.exe')
```

```bash
bash Resources/Tools/Packaging/package-veroscript-all.sh \
  --python-executable /opt/python311/bin/python3 \
  --python-executable /opt/python312/bin/python3 \
  --python-executable /opt/python313/bin/python3 \
  --python-executable /opt/python314/bin/python3 \
  --python-executable /opt/python315/bin/python3
```

The shared `build-veroscript-python-wheels.py` entry point also accepts repeated
`--python` arguments, a dedicated empty `--output`, and a tested
`--native-executable`. Use `--check-interpreters` to check the environment without
building. An optional dedicated `--work-directory` retains source copies,
environments and `python-wheel-matrix.json` for diagnostics. Full builds store
the resulting wheel/sdist files in the Python business before publication.
Complete publication rejects missing wheel ABIs, incompatible platform/native
identities, inconsistent wheel tags and missing source distributions. The
business manifest records `pythonWheelVersions`, so a copied collection can be
audited against the same matrix without the source checkout. Partial component
publication retains its partial status; historical releases retain their
recorded contract. Updating an owned wheel in an already complete matrix preserves
its staging route and complete matrix contract; incompatible updates fail before
the published collection changes.

The wheel matrix does not retag Linux binaries as `manylinux` or `musllinux`.
Host-built Linux wheels retain their actual native compatibility scope. Python
SDK/installer native modules retain the ABI of their main native build; install
the matching wheel when using a different CPython minor version.

Do not convert old business/version, platform-first or flat release collections.
Inventory and remove their obsolete owned outputs, keep necessary diagnostics,
and rebuild under the sole new standard. New readers reject old formats;
publication must not retain compatibility branches or dual-write old indices.

## Version cleanup

For new-format collections, compute the reference closure of every target index
and manifest dependency, including shared releases. Remove only unreferenced
managed versions; keep any version still selected by another target, Cpp or a GUI
add-on. A shared package becomes removable after its last reference disappears.
Use the collection lock, path/ownership checks and recoverable publication rules.
Unknown files stop cleanup. Cleanup failure after a successful publication does
not imply that publication was rolled back.

Old-format collections are discarded rather than migrated. Inventory and remove
their obsolete owned payloads without preserving old-package recovery copies;
keep necessary diagnostics, protected sources, credentials and useful toolchains.
The publication preflight checks the existing target indexes against the current
registry. When the required business set changes, use an empty dedicated output
and rebuild the collection from source.
The `prune` command implements these cross-target reference rules. Preview its
deletion list before executing cleanup; the selected target must already have an
index, and references from every target and shared index remain protected:

```text
python Resources/Tools/Packaging/veroscript_business_release.py prune --target win-x64 --dry-run
python Resources/Tools/Packaging/veroscript_business_release.py prune --target win-x64
```

## License notices

The root `LICENSE` is the source of the project's complete MIT notice. New
native runtime, SDK, language, symbol, and WebAssembly archives carry this
notice; Python distributions and NuGet packages include it with their package
metadata. Keep the notice when redistributing the software, including binaries.
An embedded notice with a readable license entry is suitable for a single-file
application. Project-owned source does not require modification labels.
Installer publication checks both `--license` and `--third-party-licenses`
against the complete source notices before publishing the executable.

Third-party components retain their own license terms. AsmJit source and header
distributions retain its zlib notices; this project's MIT license does not
replace those notices. See [third-party notices](../../Licenses/README.md).
Previously generated artifacts retain their existing license text; rebuild
packages to distribute the current MIT notice.

## Windows

Release packages deploy the selected MSVC toolchain's release redistributable
DLLs beside native executables and Interop/Python modules. The CMake
`VeroScriptWindowsRuntime` component supplies these files; packaging fails when
they are unavailable. Windows wheels use the same install rules. Each deployed
directory includes the Microsoft runtime notice. Debug CRTs are not distributed;
the Windows Universal CRT remains an operating-system prerequisite.

```powershell
./Resources/Tools/Packaging/package-veroscript-all.ps1
./Resources/Tools/Packaging/verify-veroscript-release.ps1 -Target win-x64
```

Windows ARM64 uses `win-arm64` (x64 is a different instruction set). Install the
MSVC ARM64 tools and Windows SDK, then build with an ARM64 preset:

```powershell
cmake --preset windows-msvc-vs2026-arm64
cmake --build --preset release-msvc-vs2026-arm64
```

VS 2022 and VS 2019 have equivalent `-arm64` presets. Cross-compilation on an x64
host can build native artifacts, but running them requires Windows ARM64.
The native acceptance entry runs runtime, Python, external SDK consumers and
installer checks on a Windows ARM64 host with ARM64 Python:

```powershell
./Resources/Tools/Packaging/package-veroscript-all.ps1 -Runtime win-arm64
./Resources/Tools/Packaging/verify-veroscript-release.ps1 -Target win-arm64
```

An x64 host can instead assemble all desktop products with
`package-veroscript-desktop-cross.py`, including ARM64 wheels and installers.
Those payloads are distributed with explicit outstanding target acceptance;
see "Cross-target desktop builds and acceptance" below.

The build target, SDK JIT definitions, PE machine and package target must agree.
ARM64EC and x64 emulation on ARM are separate targets and are not represented by
`win-arm64`.

## Linux and macOS

```bash
bash Resources/Tools/Packaging/package-veroscript-all.sh
python3 Resources/Tools/Packaging/verify-veroscript-release.py
```

Linux packaging requires GNU binutils (`objcopy` and `objdump`) and keeps the
native build outputs intact. Production copies of
libraries and tools have DWARF removed with `objcopy --strip-debug`, preserving
linkage symbols and relocation data. Matching ELF debug files are exported to
Symbols and referenced by GNU debuglink; place a `.debug` file beside its matching
binary or in that binary's `.debug` subdirectory for GDB to load it. Symbols also
retains the original static archives with full DWARF, allowing an embedding
program to be relinked for source debugging. The CoreSDK build identity hashes
the actual stripped production archive shipped in the SDK.

Use `--target` when auditing a release for a different host. Standalone packaging
entry points use the same business publisher. Explicit output arguments select
a dedicated staging directory and cannot overwrite a managed business release.
The standalone Runtime installer reads the selected version's `archives/`
download by default and produces a single `.tar.gz` on Linux or `.dmg` on macOS.

Before publishing a complete desktop release, run the coverage and source checks
for each target. These require all registered businesses, reject partial products,
and compare packaged public assets with the current repository:

```bash
python3 Resources/Tools/Packaging/verify-veroscript-release.py --target win-x64 --require-complete --check-source
python3 Resources/Tools/Packaging/verify-veroscript-release.py --target linux-x64 --require-complete --check-source
```

The PowerShell verifier accepts `-RequireComplete -CheckSource`. Ordinary audits
continue to support independent business releases and historical versions.
Source checks cover exported headers, resources, metadata and ABI contracts;
private implementation freshness also requires building from the intended source
revision and retaining build evidence.

The `materialize` command produces a temporary audit directory under a dedicated
`.tmp` or `dist` subdirectory. Its manifest retains business selections, partial
publication markers, and each business's build identity, including Cpp releases
that depend on a historical CoreSDK build.

## Json and network SDK packages

Official distribution names are `vs-json`, `vs-network`, `vs-http`, and
`vs-websocket`, `vs-boost`, and `vs-curl`. C++ targets and library filenames keep their `VeroScript*` names.
Each script-enabled native release includes two distinct artifacts:

| Artifact | Use |
| --- | --- |
| `vs-json-0.1.1-win-x64.zip` (or `.tar.gz` on Unix) | Relocatable C++ SDK |
| `vs-json-0.1.1-win-x64.vspkg` | Installable native script extension |

The `.vspkg` is produced by the actual package client, with
`veroscript-package.json` at its root. It includes the native library, its owned
dynamic dependencies, binding catalog, localization and licenses. It excludes
headers, static libraries, CMake exports and optional authored content.
Native entry IDs are `json`, `network`, `http`, `websocket`, `boost`, and `curl`. Backend entries have the provider role. Http and WebSocket
declare an exact `vs-network` contract dependency for compilation and execution. They carry no native runtime. `vs-boost` owns the single BoostRuntime and advertises only its built services; `vs-curl` owns the HTTP client and curl dependency. Backends are selected separately through project `services` or native typed factories. All five networking SDKs record `networkImplementationId`; backend service/context checks and publication dependency checks reject mixed foundation implementations. Boost metadata also records actual native TLS/compression capabilities and requires the corresponding notices.

The base source module is `Modules/Network/VeroScriptNetwork`. Its CMake package
is `VeroScriptNetwork` (`VeroScript::Network`, or `VeroScript::NetworkBindings`
with `COMPONENTS Bindings`), its C++ namespace is `VeroScript::Network`, and its
script namespace is `Network`. `find_package(VeroScriptNetwork)` loads only the
base SDK; request `VeroScriptHttp` and `VeroScriptWebSocket` separately for those
protocols. Both SDK and installable archives use the `vs-network` prefix.

Build `VeroScriptModulePackageTool` in a root build with
`VEROSCRIPT_BUILD_PACKAGE_MANAGER=ON` and `VEROSCRIPT_BUILD_PACKAGE_RUNTIME=ON`.
Combined builds discover it automatically. For separate module builds, pass
`--package-tool /path/to/VeroScriptModulePackageTool` from the matching Core
build. This tool reuses the real package manager and Core loader; it is a
packaging tool and is not included in distributed SDKs or Runtime packages.

With a matching package-enabled CLI, install local packages explicitly:

```sh
mkdir app
veroscript install ./vs-json-0.1.1-win-x64.vspkg --path ./app --offline
veroscript install ./vs-network-0.1.1-win-x64.vspkg --path ./app --offline
veroscript install ./vs-http-0.1.1-win-x64.vspkg --path ./app --offline
veroscript install ./vs-websocket-0.1.1-win-x64.vspkg --path ./app --offline
veroscript install ./vs-boost-0.1.1-win-x64.vspkg --path ./app --offline
veroscript install ./vs-curl-0.1.1-win-x64.vspkg --path ./app --offline
veroscript verify --path ./app
veroscript install --path ./app --locked --offline
```

Use the archives for the CLI's OS, architecture, toolchain environment and
Core implementation. Keep declared local archive sources available for future
locked installs. Installing Network first declares its local source explicitly;
adjacent archives are not an implicit package registry. The release gate checks
relocated SDK consumption, actual package installation, locked offline reuse,
native execution (including JSON object mapping), catalog-only analysis,
dependency failures and uninstall/reinstall before transactional publication.
Both artifact types appear in the business manifest and checksum inventory.

`package-veroscript-modules.py` stages Json, Network, Http, WebSocket, Boost and Curl.
It accepts a combined root build or separate `--json-build-directory` and
`--network-build-directory` paths, plus optional `--boost-build-directory` and `--curl-build-directory` overrides. `--business` is repeatable; selecting Http or
WebSocket also selects Network. Curl adds Http; Boost adds the contract packages for the services actually enabled in its build. Frontend selection alone does not add a backend. Full releases include both backend businesses. `--data-only` omits script registration. Otherwise,
bindings must have been built against a matching Core SDK; supply `--core-sdk`
for a root build, or use the Core SDK recorded by a standalone binding build.

```sh
python Resources/Tools/Packaging/package-veroscript-modules.py \
  --json-build-directory build/json --network-build-directory build/network \
  --data-only --out-root .tmp/module-sdk-release
```

Archives contain a CMake SDK, legal notices, a `veroscript-module.json` identity,
and the selected data/script profile. Boost exclusively owns the shared Boost network runtime; Network owns the
public foundation contracts. Protocol manifests pin the Network version and build identity; publication
preserves historical Network versions still required by a protocol.
Curl owns the HTTP-client backend and its curl dependency; imported curl remains
an explicit system dependency. Protocol SDKs retain backend-free consumption.
Optional Json components expose `VeroScript::HttpJson` and `VeroScript::WebSocketJson`.

Before updating the transactional collection, the tool unpacks archives into
a path containing spaces and non-ASCII characters, builds public data and JSON
adapter consumers, and runs selected static/dynamic script registration.
Module-only selections in `package-veroscript-all.py` and `.ps1` use this same path;
full native packaging stages all four modules. Script-enabled builds produce
installable `.vspkg` packages; data-only builds produce SDK archives only.
The modules do not produce standalone executable installers.
These native profiles do not certify unfinished browser/Node providers, mobile
host packages, AOT bridges, or the remaining implementation-plan features.

## Foundation, resource and GUI SDK packages

The independent foundation businesses and official archive prefixes are:

| Business | Package name | Public CMake package |
| --- | --- | --- |
| Platform | `vs-platform` | `VeroScriptPlatform` |
| Graphics | `vs-graphics` | `VeroScriptGraphics` |
| Audio | `vs-audio` | `VeroScriptAudio` |
| Sdl3 | `vs-sdl3` | `VeroScriptSdl3` |
| Image | `vs-image` | `VeroScriptImage` |
| Font | `vs-font` | `VeroScriptFont` |
| Stb | `vs-stb` | `VeroScriptStb` |
| FontBackend | `vs-font-backend` | `VeroScriptFontBackend` |
| Gui | `vs-gui` | `VeroScriptGui` |
| RmlUi | `vs-rmlui` | `VeroScriptRmlUi` |

Product versions are independently registered in
`ReleaseVersions.json`. Names use `<package>-<version>-<target>.zip` on Windows
and `.tar.gz` on Linux/macOS, for example `vs-sdl3-0.1.1-linux-x64.tar.gz`.
The release route is `Release/Modules/<category-route>/<Business>/<target>/<version>/`.
Native library names, CMake targets and script namespaces are unchanged.

Each archive retains one enclosing directory and contains its own native SDK,
MIT notice and `veroscript-module.json`. Public services do not include an SDL
runtime. Sdl3 exclusively owns the selected SDL runtime, its public dependency
SDK, any enabled native WebGPU runtime, and the corresponding notices. Its
manifest pins Platform and enabled Graphics/Audio packages by version and payload
identity. Graphics script bindings also require Platform bindings. Public native
Platform, Graphics and Audio SDKs remain usable without Core or SDL.
Official desktop Sdl3 archives require `SDL_SHARED=ON` so host and provider share
one SDL runtime; mobile/browser static builds use their separate SDK flow.

Configure and build the selected foundation targets first. Publish just these
modules from a matching root build using the existing entry point:

```sh
python Resources/Tools/Packaging/package-veroscript-modules.py \
  --foundation-build-directory build/visual --business Sdl3 \
  --core-sdk out/core-sdk --out-root dist/foundation-release
```

Selecting Sdl3 adds its enabled public service dependencies. Script SDKs are the
default; use `--data-only` for native contracts without script adapters. Root
builds must enable `VEROSCRIPT_VISUAL_SCRIPT_BINDINGS` for script packages. A
native build from `Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Tools/CMake/Standalone` uses
`--data-only`. Use a fresh build directory after changing the standalone source
entry path. `--skip-build` reuses binaries but still runs archive audits and
installed consumers. Separate JSON/network/foundation build directories can be
combined with explicit `--business` selections.

Foundation script SDK consumers select a matching test VM explicitly. Root builds
record it in `veroscript-foundation-Release.json`; the module publisher builds the
official VM module for Platform, Graphics, Audio and SDL3 consumer tests when
`VEROSCRIPT_BUILD_VM_BACKEND=ON`. An external test VM can be supplied through
`VEROSCRIPT_TEST_VM_LIBRARY` at CMake configuration. `--skip-build` requires that
library to exist already. Native SDKs do not acquire a VM dependency.

The Python and PowerShell all-business entry points include these ten modules;
module-only selections use this same staging and verification flow. They accept
`--data-only` / `-DataOnly` for module-only native
SDK selections. These switches do not apply to a mixed/full desktop release.
Full desktop packaging enables the foundation services and script adapters. Bindings require
the exact Core SDK library, public headers and generated ABI/implementation
header used by their build. SDL3 staging queries the actual native binary's
descriptor and records its full native-extension ABI and Core implementation
identity without creating a service.
It reads the generated Core header from CMake's `CoreBuildHeader` record, so
custom Core binary directories do not require a fixed `VeroScript/Generated`
subdirectory under the foundation build.
Publish that matching CoreSDK business first, or include it in the same full
release transaction. A standalone module publication does not replace CoreSDK.
`--candidate-directory` writes a verified, unpublished candidate to a new `.tmp`
or `dist` directory. Full Windows packaging uses this mode before publishing
CoreSDK and its dependent modules together; it does not publish an intermediate
module collection with unresolved Core dependencies.
Same-version dependency rebuilds are checked before replacing a release index;
required historical dependency versions remain retained, including indirect Core
dependencies. SDK CMake discovery also rejects mismatched dependency build pins.

Individual CMake install components are `VeroScriptPlatform`,
`VeroScriptGraphics`, `VeroScriptAudio`, `VeroScriptSdl3`, and the corresponding
`VeroScriptPlatformBindings`, `VeroScriptGraphicsBindings`,
`VeroScriptAudioBindings`. The aggregate `VeroScriptVisual` component remains
available for existing source hosts. Independent archives are consumed with:

```cmake
find_package(VeroScriptAudio 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Bindings)
find_package(VeroScriptSdl3 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::AudioBindings VeroScript::Sdl3)
```

Provide all required extracted prefixes through `CMAKE_PREFIX_PATH`; dependencies
are relative to their own prefix and survive relocation. Native discovery does
not load optional bindings. Packaged consumers build and run from unpacked paths
containing spaces and non-ASCII characters, covering services, controller/audio
state, graphics operations, and script adapters when selected. Native provider
registration is checked when that provider and a matching Core are included.
Linux staging strips DWARF from copied libraries without modifying build outputs.
Module archives contain no authored manuals, examples, tests or debug symbols;
Resources owns documentation and examples. SDK tests do not certify untested
Apple/mobile devices or every physical driver.

These native SDK archives under `sdks/` serve embedding hosts that register the
script adapters. Script-enabled desktop packaging separately emits runtime
`.vspkg` distributions under `packages/`, as described above; data-only and
mobile SDK profiles do not. Package installation deploys the runtime payload
and does not enable optional providers or create missing host services. Renaming
an SDK archive does not convert it into an installable package. Temporary
`veroscript-visual-*-sdk-*` archives are verification artifacts and are not formal
module releases.

## Mobile SDK tooling

The shared mobile host, Android adapter and iOS adapter are owned by
`Extensions/Platform/Mobile/VeroScriptMobile`, `VeroScriptAndroid` and `VeroScriptIOS`.
`package-veroscript-mobile.py` remains the low-level builder: it builds an Android
AAR or iOS XCFramework into an explicit, new staging directory. Formal mobile
publication uses `package-veroscript-mobile-release.py`. See the [Android guide](../../../Extensions/Platform/Mobile/VeroScriptAndroid/README.md)
and [iOS guide](../../../Extensions/Platform/Mobile/VeroScriptIOS/README.md) for the pinned
toolchains and target slices. Mobile targets `android` and `ios` describe complete
multi-ABI distributions: Android arm64-v8a/x86_64, iOS arm64 device and
arm64/x86_64 simulator. They do not imply that a desktop ARM64 binary runs on mobile.

```text
Release/
  release-index.json
  indexes/{any,android,ios}.json
  CoreSDK/{android,ios}/0.1.1/sdks/VeroScriptCoreMobileSDK-0.1.1-<target>.zip
  Interop/{android,ios}/0.1.1/sdks/VeroScriptInteropMobileSDK-0.1.1-<target>.zip
  Extensions/
    Platform/Mobile/Android/android/0.1.1/sdks/VeroScriptAndroidSDK-0.1.1-android.zip
    Platform/Mobile/iOS/ios/0.1.1/sdks/VeroScriptIOSSDK-0.1.1-ios.zip
    Language/Cpp/{android,ios}/0.1.1/sdks/VeroScriptCppMobileSDK-0.1.1-<target>.zip
    Language/CSharp/any/0.1.1/packages/VeroScript.CSharp.0.1.1.nupkg
    Language/CSharp/any/0.1.1/symbols/VeroScript.CSharp.0.1.1.snupkg
    Language/Python/any/0.1.1/packages/veroscript-0.1.1.tar.gz
    Language/Python/{android,ios}/0.1.1/sdks/VeroScriptPythonMobile-0.1.1-<target>-cp<minor>.zip
  Modules/
    Execution/Vm/{android,ios}/0.1.1/sdks/VeroScriptVmMobileSDK-0.1.1-<target>.zip
    <category-route>/<optional-module>/{android,ios}/0.1.1/sdks/<native-sdk>.zip
```

Every leaf also has `release-manifest.json` and `checksums.txt`.
Android/iOS, CoreSDK, Cpp, Interop, Vm, CSharp/Python and optional modules use
their existing independent business version keys, selected by each platform index.
Archive release versions and native-source versions are recorded separately;
NuGet metadata must match its actual package version. No mobile business produces
a product installer. ZIPs retain a canonical enclosing directory, functional
resources, AOT headers and licenses, and omit README/docs/examples. The optional
Android AsmJit AAR is published separately under
`Modules/Execution/Jit/android/<version>/packages/`
with `--with-asmjit`; iOS rejects that selection.

```sh
# Read-only preview: no build, staging or publication.
python3 Resources/Tools/Packaging/package-veroscript-mobile-release.py android --plan

# Publish existing, verified producer outputs with the canonical desktop/mobile NuGet feed.
VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY="$PWD/dist/csharp-canonical" \
python3 Resources/Tools/Packaging/package-veroscript-mobile-release.py android \
  --native-sdk dist/mobile/android \
  --native-cmake-sdk dist/mobile-native/android \
  --csharp-package dist/csharp-canonical/VeroScript.CSharp.0.1.1.nupkg \
  --python-sdk-directory dist/python/android-cp313 \
  --python-sdk-directory dist/python/android-cp314
python3 Resources/Tools/Packaging/verify-veroscript-release.py \
  --target android --require-complete --check-source
```

Prepare all six package IDs and their matching `.snupkg` files in that feed
before publishing the first desktop or mobile target. Use the same feed with
the corresponding iOS inputs and `ios` target for Apple publication. Without
`--native-sdk`, the selected native SDK is built using its real platform toolchain.
`--csharp-native-root` builds the complete mobile-enabled NuGet with both .NET 10
mobile workloads on macOS; `--python-target-sdk` builds repeated target descriptors
instead of collecting existing deployments.

The complete mobile baseline is **seven businesses**: Android or iOS, CoreSDK,
Cpp, Interop, Vm, CSharp and Python. The native module SDKs Json, Network, Http,
WebSocket, Boost, Curl, Platform, Graphics, Audio, Sdl3, Image, Font, Stb,
FontBackend, Gui and RmlUi are opt-in through repeated `--business` selections or
`--with-native-modules`. They use `Modules/<category-route>/<business>/<target>/<version>/sdks/` and
the same manifests, checksums, per-platform indices and publication transaction.
These are explicitly **native-data SDKs** with their existing C++ service APIs;
they do not advertise desktop script providers or `.vspkg` deployment on mobile.
Android AsmJit remains a separate optional AAR; native AsmJit is excluded on iOS.

`package-veroscript-mobile-native.py` stages raw multi-slice CMake SDKs, without
creating release archives. Its default selection is CoreSDK/Cpp/Interop/Vm;
repeat `--business` for a smaller selection or optional native modules. Reuse a
complete `--build-directory <slice>=<directory>` matrix, or build with the real
toolchains: Android NDK **28.2.13676358** and Ninja, or macOS/full Xcode for iOS.
The unified entry forwards `--native-build-directory`, `--native-ndk` and
`--native-dependency-prefix`. Reused builds use their recorded tools and cannot
consume tool/dependency overrides. Curl and TLS-enabled Boost source builds need cross-built
OpenSSL SDK prefixes for every selected slice; their required target SDK/runtime
dependencies remain explicit in `mobile-native-sdk.json`.
Boost keeps the source's mobile TLS default off; enable it explicitly with
`--native-network-tls` (raw producer: `--network-tls`) and matching dependency SDKs.
Reusing iOS build directories still runs installation and cross-link qualification
on macOS/full Xcode. For auditing on another host, reuse completed raw SDKs through
`--native-cmake-sdk`; build-directory inputs do not replace the target toolchain.
Recorded Android builds may use either NDK `ANDROID_*` aliases or CMake's
`CMAKE_ANDROID_*` keys. ABI, API and STL fields must agree, with shared libc++
explicitly recorded. Toolchain and dependency directories may contain their normal
symlinks; these directories are build inputs and are not copied into the SDK.
Owned staging trees and finished release payloads still reject links. The pinned
NDK version is checked before configuring a source build.

Unlike the separate desktop component archives, each mobile CMake SDK is an
explicitly self-contained profile: it bundles the matching native service
dependencies and records their names/native versions, capabilities, headers and
binary inventories. Cpp includes Core and the static VM. Interop includes those
libraries plus static C Interop. Vm includes the public execution contract without
Core objects. Optional module SDKs bundle their transitive native service SDKs,
preserving existing CMake exports and redistribution notices. The enclosing
business still has its own distribution version/build identity; it does not pin a
desktop CoreSDK release. Choose one runtime linkage profile per application:
do not combine these static runtime archives with the platform SDK's second copy
of Interop/Core in the same runtime integration.

Each SDK contains `slices/<slice>/{include,lib,...}`, root CMake package selectors,
`LICENSE`, functional resources where required, and `mobile-native-sdk.json`.
Android contains arm64-v8a/x86_64 archives, one matching shared libc++ per slice,
NDK notices and 16-KB-aligned shared libraries. iOS contains three thin native
archive slices (arm64 device, arm64/x86_64 simulator), not macOS libraries. These
CMake SDKs select one iOS architecture per build; the platform iOS SDK retains its
Objective-C XCFramework. Root selectors reject desktop systems, unsupported ABIs,
old OS targets and ambiguous device/simulator choices. A C++ consumer uses:

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Runtime)
target_link_libraries(app PRIVATE VeroScript::VeroScriptCpp VeroScript::VeroScriptBytecodeVm)
```

Native module SDKs preserve their platform library kinds: Android's SDL3 bridge
and Boost runtime are shared libraries; the bundled SDL runtime is static. The
iOS counterparts are static. Consumer qualification retains shared libraries even
when the smoke main needs no symbol, so unresolved shared dependencies cannot be
hidden by the linker's unused-library removal. Capability metadata describes the
providers actually bundled in each SDK, excluding unrelated targets in its build.

Source generation/BindGen/transpiler tools stay on the development host. The
mobile Core config imports Core without its desktop Tooling target; the C++
config only exposes Runtime and rejects required Tools/Aot components. Core's
generated public ABI/implementation identity travels with the mobile headers.
Before a raw native SDK is eligible for formal publication, the producer
cross-compiles and links a consumer for **every slice**, retaining every object
of the selected library to expose unresolved dependencies. Target binaries are
not executed. Publication checks the recorded consumer matrix/template identity,
archive objects, headers/resources, exact file hashes and dependency closure.
Actual device/emulator/application acceptance remains a separate required step.

The read-only plan includes a `mobileCoverage` entry for **every registered
business**, including why desktop Runtime, editor plugins, WebAssembly and
Resources do not become separate native mobile products. Adding a new language,
platform adapter or module without a release policy fails inventory validation.
For Python target builds, repeat `--host-python /path/to/python3.13` and
`--host-python /path/to/python3.14`; each target minor needs a matching normal
host CPython. Missing host interpreters or target slices fail before any producer
runs. Existing Python deployment inputs do not require these host interpreters.
The C# package retains the existing
`VeroScript.CSharp` identity and all four managed frameworks; a desktop-only NuGet
cannot satisfy a mobile release. Add `--csharp-symbols <matching.snupkg>` when
collecting an existing NuGet's symbols. A source C# build retains its generated
symbol package automatically. Symbols are a separate optional artifact in the
same business leaf, never a mobile installer or a runtime dependency. Validation
checks their package identity, `SymbolsPackage` declaration and Portable PDBs
for the same four frameworks, following the [NuGet symbol format](https://learn.microsoft.com/en-us/nuget/create-packages/symbol-packages-snupkg).
The shared main package and optional symbols are sealed together. Adding,
removing or replacing either file at the same version is rejected. A new version
without symbols does not inherit the previous version's symbol package; cleanup
retains the old version only while another index or dependency still references it.
Python's complete mobile matrix contains both
CPython 3.13 and 3.14 with every required ABI/device/simulator slice. Explicit
`--partial-python` permits fewer Python minors and marks that business partial;
`--require-complete` rejects it until the missing minor is published.

`--business` repeats to select a subset, `--out-root` selects a dedicated managed
collection, and `--candidate-directory` produces an audited candidate without
publication. Missing inputs fail rather than silently dropping a selected business.
Inputs for unselected businesses are rejected before file/toolchain inspection.
The frozen version configuration is passed to child producers; Android's optional
backend receives its independent version through Gradle and CMake. `nativeVersion`
must agree with `VeroScriptVersion.h`. Native business manifests retain their
native versions. Shared C# manifests omit the publishing host's native version;
each consuming target index carries its own C# native-version context, which is
restored when that target is verified or materialized.
The existing collection lock, rollback, independent versions, pruning, checksums
and index validation apply to mobile publications. Historical verification uses
recorded inventories and native structure without comparing today's source;
`--check-source` additionally checks current headers, catalogs, binding resources
and AOT contracts. This archive validation does not establish device acceptance.
Source-aware mobile auditing also checks current project/dependency notices and
the Python device acceptance tool. C# native assets retain the Android application
container's miniz, JSON and SHA-256 notices alongside the NDK notices.
The iOS static profile explicitly disables application/package/build-system
components, avoiding dependencies inherited from a previous CMake cache. Its
native SDK requires the iOS adapter header, Interop header and module map in every
XCFramework slice. ZIP auditing rejects noncanonical paths, Windows device/path
aliases, case-conflicting directories, links and special filesystem entries.

C# mobile native assets are assembled by
`Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py`; CPython mobile
extension slices use `Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py`.
Each checks its target SDK, functional resources, legal notices and native
dependencies without bundling documentation or README. Real target builds and application/device
acceptance are required in addition to host-side package tests.

## Documentation and tests

```bash
python3 Resources/Tools/Packaging/veroscript_docs.py check
python3 Resources/Tools/Packaging/veroscript_docs.py check-collection
python3 -m unittest discover -s Resources/Tools/Packaging/tests -p 'test_veroscript*.py' -v
```

Documentation checks and packages assemble a fresh collection from author originals
under repository `.tmp`, validate its links, then remove the temporary collection.
`Resources/Docs/_generated` is not an input or an output of this workflow.
`All` publication includes maintenance documents and Standards, which apply only to
development of and contributions to this project and add no license conditions.
`documentation.json` classifies Standards as `Developer` through `standardsAudience`.
The Resources release carries the integrated `User` publication without Standards;
User links cannot include Standards even through registered resource dependencies.
Runtime/SDK/extension archives and product installers contain functional payloads
and omit Standards, authored documentation and examples. Source copyright and license
notices remain intact. Publication verification rebuilds the expected contents
from current originals and rejects stale or edited exports. Refreshing the IDE's
standards reference is a separate `veroscript_docs.py sync-standards` operation.
Direct documentation exports use dedicated staging directories and reject managed
release trees, platform collection roots, and nonempty destinations. Publish release
documentation through the business packager so manifests and checksums stay current.

The detailed release contract is maintained in
[the installer documentation](../../../VeroScriptInstaller/Resources/Docs/Developer/en-US/release-layout.md).

## Resource and GUI SDK composition

Image and Font are independent CMake SDK packages. Stb provides `image/stb`.
FontBackend owns FreeType, HarfBuzz and SheenBidi and independently provides
`font/font-backend` and `text-layout/font-backend`. Root build options
are `VEROSCRIPT_BUILD_IMAGE`, `VEROSCRIPT_BUILD_FONT`, `VEROSCRIPT_BUILD_STB`
and `VEROSCRIPT_BUILD_FONT_BACKEND`. Install matching public service and backend
components together, or install all components. The `VeroScriptGui` component
bundles all four by default, including optional built provider modules and
script bindings, CMake dependency exports, the default font, and notices.
Font contains generic layout interfaces and coordination only. FontBackend
embeds SheenBidi 3.0.0 for Unicode 17.0.0 analysis, with its archive and
Apache-2.0/Unicode License V3 notices under `licenses/SheenBidi`. These notices
belong to `vs-font-backend`; `vs-font` retains the default font's notice.
`VEROSCRIPT_GUI_BUILD_RESOURCE_BACKENDS=OFF` supports hosts supplying other
providers. Formal module releases publish these resources independently as `vs-image`,
`vs-font`, `vs-stb` and `vs-font-backend`; full desktop packaging includes them.
They remain archive distributions and do not create standalone installers.

Relocated SDK consumers use `find_package(VeroScriptImage)`,
`find_package(VeroScriptFont)`, `find_package(VeroScriptStb)` and
`find_package(VeroScriptFontBackend)`. Script bindings additionally
require the matching Core SDK. Shared provider modules use its native extension
ABI; static providers use named query entries. Rebuild Gui providers/consumers
together using the Gui release version for GUI service matching. Preserve the installed project, font and
third-party notices with redistributed code and font assets.

### Gui and RmlUi formal releases

Gui and RmlUi product versions are independently recorded in
`ReleaseVersions.json`. Official archives include one enclosing directory:
`vs-gui-0.1.1-win-x64.zip`, `vs-rmlui-0.1.1-win-x64.zip`, or the corresponding
`*-linux-x64.tar.gz` / `*-macos-arm64.tar.gz`. Their routes are
`Release/Modules/Multimedia/Gui/<target>/<version>/sdks/` and
`Release/Modules/Multimedia/Backend/RmlUi/<target>/<version>/sdks/`.
The separately generated script-enabled desktop `.vspkg` files use `packages/`
beside `sdks/` in the same business version directory.

Gui depends on Platform, Graphics, Image and Font; RmlUi depends on Gui. Stb
and FontBackend depend on Image and Font respectively. Selecting a module adds its
transitive public service dependencies. Gui selects no particular GUI, image,
font, window or graphics backend. Select Stb/FontBackend and SDL3 explicitly when needed.
RmlUi owns the vendored RmlUi SDK and notices; FontBackend owns FreeType/HarfBuzz/SheenBidi and
notices. Each archive contains its own payload and pins dependency versions and
payload identities in `veroscript-module.json` and the generated CMake release
configuration. It does not copy another module's libraries.

Use a matching root build with `VEROSCRIPT_BUILD_GUI=ON`,
`VEROSCRIPT_GUI_SCRIPTING=ON` and `VEROSCRIPT_GUI_BUILD_RMLUI=ON` for script SDKs.
`VEROSCRIPT_BUILD_STB=ON` and `VEROSCRIPT_BUILD_FONT_BACKEND=ON` enable the default resource
providers. The default script profile includes GUI builders, ML/CSS compilers and
the GUI CLI forwarding module, and the selected backends' shared native provider
modules. These require the exact Core SDK; Gui additionally requires the matching
C++ Runtime SDK. Supply both installed prefixes, including the Core standard
library, for installed consumer tests:

```sh
python Resources/Tools/Packaging/package-veroscript-modules.py \
  --build-directory build/gui-release --business Gui --business RmlUi \
  --business Stb --business FontBackend \
  --core-sdk /path/to/core-sdk --cpp-sdk /path/to/cpp/Runtime \
  --out-root dist/gui-release
```

Publish the matching CoreSDK and Cpp businesses to that collection first, or
include them in the same full release transaction. `--candidate-directory`
produces an audited candidate without publishing. `--data-only` omits script
adapters and provider modules and requires neither Core nor Cpp. It can also use
`cmake -S Modules/Multimedia/VeroScriptGui -B build/gui-native
-DVEROSCRIPT_GUI_SCRIPTING=OFF` as its foundation build. The PowerShell all-business
entry supports `-Businesses Gui,RmlUi,Stb,FontBackend -CoreSdk ... -CppSdk ...`; both
all-business entry points include these modules in complete desktop releases.

A standalone script SDK build uses `Modules/Multimedia/VeroScriptGui` with
`VEROSCRIPT_GUI_SCRIPTING=ON`, `VEROSCRIPT_VISUAL_SCRIPT_BINDINGS=ON` and both
matching SDK prefixes in `CMAKE_PREFIX_PATH`. It builds against those installed
Core/C++ libraries instead of rebuilding Core. Native-only standalone builds
keep both script options off. Build evidence records the imported libraries and
their generated Core identity header for the same publication checks.

```cmake
find_package(VeroScriptGui 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Bindings)
find_package(VeroScriptRmlUi 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Bindings)
target_link_libraries(app PRIVATE VeroScript::GuiScript VeroScript::RmlUi)
```

Native consumers omit `COMPONENTS Bindings` and link `VeroScript::Gui`.
`VeroScript::GuiScript`, `VeroScript::RmlUi`, native library filenames and the
`VS::Gui` script namespace remain unchanged. The existing
`find_package(VeroScriptGuiFramework)` aggregate entry and `VeroScriptGui`
aggregate install component remain available for existing embedding hosts.
Formal staging uses the separate `VeroScriptGuiNative`, `VeroScriptGuiBindings`,
`VeroScriptRmlUi` and `VeroScriptRmlUiBindings` components. Image/Font/Stb/FontBackend
also separate their native and `Bindings` components. Whole-tree CMake installs
still include all built components.

The release gate checks current headers, relative exports, target architecture,
required third-party notices, native provider ABI/build descriptors, dependency
pins, and stripped Linux payloads. It builds and runs consumers from extracted
paths containing spaces and non-ASCII text: native services, GUI markup and
layout, shared-library linkage, script builders and native providers. A mismatch
in a same-version Image dependency must also fail GUI CMake discovery.
The legacy GuiFramework entry also checks the RmlUi provider's Core build when
the script SDKs are unpacked into separate prefixes.
Formal archives omit authored guides, examples, tests and debug symbols;
Resources remains their documentation/example distribution. These are native SDK
archives. The Resources publication includes the localized GUI guide and ships
complete GUI examples under `Resources/Examples/Gui`, including companion ML,
styles, scripts and images.
Embedding hosts consume these SDK archives through the existing script/service
APIs. CLI consumers use the separate runtime `.vspkg` distributions with a
matching GUI-enabled host; installing a package does not supply GuiScript to a
pure Core host or select optional providers.

## Cross-target desktop builds and acceptance

`package-veroscript-desktop-cross.py --help` describes assembly from a real target
build, explicit CPython SDKs, a compiler-evaluated ABI record and source-bound
native catalog references. Use empty, separate candidate and work directories
under source-owned `.tmp` or `dist`; a failed run uses a new staging pair.
The target compiler checks CPython header version, pointer size and the normal
release ABI before building each wheel. Reference wheel and sdist `Requires-Python`
metadata must cover the declared matrix. macOS wheel tags must cover the minimum
versions declared by the Mach-O extension and Interop libraries. Debug, free-threaded and trace-ref SDKs
are rejected rather than being labeled with normal CPython wheel tags.
32-bit metadata must be exported by its actual target tool. `--target-tool` also
accepts an explicit emulator command. Host build and archive verification are
independent from target runtime/device acceptance.

Successfully built, integrity-verified payloads are included in Release even when
acceptance has failed or has not run. Per-business `qualification` records bind
checks to exact manifest digests. `complete` means inventory completeness;
`qualificationStatus` summarizes checks and `qualified` requires a complete
inventory and all mandatory checks passed. Missing records are `not-recorded`.
Verification recomputes completeness from the registered target businesses and
actual manifests. Publishing unchanged manifest references preserves their bound
acceptance records; changed references require new records.
Static installer verification reads the published .NET apphost and hashes the
embedded content parts against the bundle digest. Cross assembly also compares
that digest to the verified staging ZIP. Actual installation enforces OS and
architecture matching. DMG validation on Windows/Linux requires
7-Zip through `VEROSCRIPT_7ZIP` or PATH. This is a build tool, not a runtime dependency.
