# Installer build and bundle contract

## Build products

The Installer app and tool target .NET 9 and consume `VeroScriptInstaller.Core`. The graphical app embeds its localization catalogs as resources and carries the bundle as self-extracting content in the single executable. The tool builds or inspects the bundle archive without the graphical host.

All commands below run from the repository root. Build and run the installer
regression executable with:

`VeroScriptInstaller/global.json` selects .NET SDK `9.0.100` with `latestFeature` roll-forward. Start the snippets from the repository root; their `Push-Location` makes SDK resolution use the Installer directory. Keep an installed .NET 9 SDK even when .NET 10 mobile workloads are also present; the C# mobile toolchain is separate.

```powershell
Push-Location VeroScriptInstaller
try
{
	dotnet build App/VeroScriptInstaller.csproj -c Release -warnaserror
	dotnet build Tool/VeroScriptInstaller.Tool.csproj -c Release -warnaserror
	dotnet run --project Tests/VeroScriptInstaller.Tests.csproj -c Release
}
finally
{
	Pop-Location
}
```

The Tests project is a console test runner; use `dotnet run`, not an assumed
`dotnet test` discovery result. Building App alone does not embed a release
payload; use a packaging script for the distributable installer.

## Full Windows release

The full pipeline supports Release for `win-x64` and `win-arm64`. It requires CMake, a supported
MSVC installation, .NET 8/9 SDKs for the complete product set, Node.js/npm, Web
test dependencies, and Emscripten for rebuilding WebAssembly. Prepare Web test
dependencies with `npm ci` in `Extensions/Platform/WebAssembly/VeroScriptWebAssembly`; browser smoke tests
also need an available browser (for example Playwright Chromium).

```powershell
.\Resources\Tools\Packaging\package-veroscript-all.ps1 `
  -ConfigurePreset windows-msvc-vs2026 -Runtime win-x64
```

For Windows ARM64, use `-Runtime win-arm64` with a matching preset such as `-ConfigurePreset windows-msvc-vs2026-arm64`, and install the MSVC ARM64 tools and Windows SDK. Full publication runs native execution checks, including the Python wheel matrix, so use a native Windows ARM64 host with all required ARM64 CPython interpreters. Cross-compiling native artifacts on x64 does not complete those checks. `win-arm64` denotes native ARM64, not ARM64EC or emulated x64.

Select the preset matching Visual Studio, or omit it for automatic selection. `-OutRoot` defaults to `Release`. See [business releases](release-layout.md) for independent versions, output layout, manifests and publication.

`-SkipBuild` skips rebuilding native and Web outputs, but still publishes the
managed apps/installers and validates the staged WebAssembly SDK. Only use it when those
existing native/Web outputs are known to match the intended source/configuration.
It is not a clean-build verification. The normal release contains:

- Archives and installers in the separate Runtime, CoreSDK, Interop, Cpp, CSharp, Python and WebAssembly version directories.
- Common native symbols under CoreSDK and loose resources under Resources.
- Per-business target manifests/checksums and collection indexes.

Artifacts use `Release/<business-route>/<target>/<version>/`, with `archives/`, `sdks/`, `packages/`, `installers/` and `symbols/` created as needed inside each version. Shared distributions use `any`; WebAssembly SDKs use `wasm32`. Build intermediates remain in `build/` and `.tmp/`. See [publication rules](release-layout.md) for locking, version cleanup and rollback.

## Windows native runtime dependencies

MSVC Release packaging installs the selected toolchain's redistributable DLLs through `vs_install_windows_runtime`. The `VeroScriptWindowsRuntime` component supplies the Runtime root; Interop, C++ tools and Python use their corresponding install components. Package assembly also places the DLLs beside native entry points in the SDK and managed runtime layouts. Use the toolchain's redistributable files, not DLLs copied from system directories; debug CRT and Universal CRT are not bundled.

The release verifier checks native architecture and requires `msvcp140.dll` and `vcruntime140.dll` beside each shipped native entry point, plus `vcruntime140_1.dll` for x64 targets. ARM64 uses its native CRT and excludes the x64 `vcruntime140_1.dll` supplied in the ARM64 redistributable component. It also requires `licenses/MicrosoftVisualCpp/NOTICE.txt` in that directory. Preserve the source `Resources/Tools/CMake/WindowsRuntimeNotice.txt` as a separate third-party notice; the project MIT license does not replace it. Missing dependencies fail publication, and missing MSVC redistributables fail Windows wheel configuration. Universal CRT remains an operating-system prerequisite.

## Python publication prerequisites

When Python is selected for a complete release, prepare the full CPython 3.11–3.15 interpreter matrix before invoking the pipeline. `-SkipBuild` / `--skip-build` still builds and validates the wheels and source distribution. See the [Python matrix contract](../../../../../Extensions/Language/Python/Resources/Docs/Developer/en-US/toolchain-and-packaging.md) for interpreter selection, preflight and the distinction between wheel ABIs and the SDK's single native ABI.

## Bundle input contract

The bundle manifest is the source for product identity, platform and
architecture, component IDs, relative file paths, hashes, and selectable product
components. The installer must not reconstruct release contents from
the current repository tree during installation or repair.

An installer bundle is an expanded directory containing `manifest.json` and
`payload/`. It is different from a portable runtime/SDK ZIP. Build, verify and
archive it with the Tool; `--source-*` names must match the selected profile:

```powershell
Push-Location VeroScriptInstaller
try
{
	$tool = 'Tool/VeroScriptInstaller.Tool.csproj'
	dotnet run --project $tool -c Release -- build-bundle --output ../.tmp/runtime-installer-bundle --version 0.1.1 --profile runtime --source-runtime D:/Packages/Runtime
	dotnet run --project $tool -c Release -- verify --bundle ../.tmp/runtime-installer-bundle
	dotnet run --project $tool -c Release -- pack-bundle --bundle ../.tmp/runtime-installer-bundle --output ../.tmp/runtime-installer-bundle.zip
}
finally
{
	Pop-Location
}
```

`D:/Packages/Runtime` above is a placeholder for a prepared runtime payload,
including the required CLI/LSP/DAP, standard library and localization files.
See [product definitions](../../../Examples/Products/README.md) for custom products and the
generic single-product publisher.

## Single-file bundle transport

`App/InstallerBundleContent.targets` splits `InstallerBundleArchive` into at most 32 content parts, each no larger than 1 GiB, named `VeroScriptInstaller.EmbeddedBundle.zip.0000` and subsequent numbers. The single-file publisher includes these parts with `IncludeAllContentForSelfExtract`; they are not separate downloads. Assembly metadata records `VeroScriptInstaller.BundleSha256` for the complete ZIP and `VeroScriptInstaller.BundlePartCount`.

`EmbeddedInstallerBundle` opens the extracted parts, rejects links and invalid counts/sizes, checks the complete ZIP SHA-256, and exposes a seekable stream without buffering the entire archive or copying it to another ZIP. On Linux the expanded bundle uses `AppContext.BaseDirectory/VeroScriptInstaller.Unpack`; elsewhere it uses `Path.GetTempPath()/VeroScriptInstaller`. Bundle extraction and manifest validation still apply. The publish target rejects a missing or empty single-file executable; these guards do not establish that a large installer has passed target acceptance.

Failures normally remove the owned release staging directory. `-KeepFailedStage` on the Windows full publisher or `--keep-failed-stage` on the Unix full publisher retains a failed stage and prints its path for diagnostics. Successful staging is still removed. Retained files are candidates; recheck source, configuration and hashes before any reuse.

## Verification and optional signing

An embedded bundle and an external bundle must pass the same manifest, path,
hash, component, and platform validation. A build change that changes the
archive layout, manifest fingerprint, or installation record schema requires
updating repair, relocation, uninstall, and package tests together.

Public API changes must also update the component payload contract. The current
Core component requires the umbrella, JIT, value and VM public headers; C++
requires its session header and CMake package; and the Windows Python runtime
requires its package entry point, type stub, `py.typed` marker and Interop DLL.
The final release verifier additionally checks the session-cache API markers in
Core/C++/Interop/Python archives and in both the loose and NuGet-contained C#
target-framework assemblies. It also requires every C# NuGet/symbol package,
plus both WebAssembly SDK variants and declarations.
These checks intentionally fail a package assembled from stale pre-API outputs.

The packaging pipeline runs `--verify-localization` and
`--verify-embedded-bundle` on the produced executable, including after the
optional signing step. Check exit codes: these switches perform self-checks,
not an interactive install or a manual GUI acceptance test.

Localization verification checks all six built-in catalogs, all eight product
profiles and their component resource keys, nonempty values and matching format
placeholders. It also exercises partial external overlays, Chinese language-tag
fallback and language switching for progress/results/common failures. Missing
built-in translations cannot silently pass by falling back to English.

Localization resource keys are part of the manifest fingerprint. Rebuild bundles
and installer executables together when changing them. Keep the original
installer for repairing an existing installation: a rebuilt same-version package
with a different fingerprint is intentionally rejected for repair. Normal update
mode supports installing the rebuilt package over the same version.

Signing is optional for the current release. Without a configured certificate,
Windows scripts warn and continue. To opt into mandatory signing later, configure
`VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT` and `VEROSCRIPT_TIMESTAMP_SERVER`,
then use `-RequireSignedInstallers` for the full pipeline or `-RequireSignature`
for a single-product script. Final checksums are generated after signing. The
full pipeline passes `-RequireSignedInstallers` to its final release verifier;
use that switch there too when verifying a staged release independently. It
requires every published installer to have an Authenticode status of `Valid`.

Build the full Windows release manually with
`Resources/Tools/Packaging/package-veroscript-all.ps1`.
For Linux/macOS, use `Resources/Tools/Packaging/package-veroscript-all.sh`
on a matching native host and validate the selected platform's business release directories. The standalone
Windows checks alone do not certify Unix output. See the
[release validation procedure](release-validation.md) for required checks and platform limits,
the [user guide](../../User/en-US/index.md) for graphical installation, repair and uninstall,
and [automation](automation.md) for Tool installation and relocation commands.

## Native Linux/macOS releases

Full releases and standalone components follow [business release rules](release-layout.md). Native Runtime/SDK archives preserve executable modes in `.tar.gz`; WebAssembly remains ZIP. Linux installers use `.tar.gz`; macOS installers use `.dmg` containing a standard `.app`. Installers are self-contained. Linux respects an absolute `XDG_DATA_HOME`. Python ABI and Linux compatibility depend on the recorded build environment.

```bash
bash Resources/Tools/Packaging/package-veroscript-all.sh --target linux-x64
python3 Resources/Tools/Packaging/verify-veroscript-release.py \
  --release-directory Release --target linux-x64
```

`target`: `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`.

For desktop cross-target assembly, including `win-x86`, use `Resources/Tools/Packaging/package-veroscript-desktop-cross.py --help` and supply a real target build with matching dependencies. Build/archive verification and target runtime acceptance are recorded separately.


## Documentation packages and integrated views

See [Documentation maintenance — Validation and publication](../../../../../Resources/Docs/Developer/en-US/index.md#integrated-publication-views).

Complete releases publish the User view through Resources at `Resources/Docs/`, alongside `Resources/Scripts` and `Resources/Assets`. Current Runtime/SDK/extension archives and installers omit authored documentation and examples. Read `release-layout.md` in the Installer Developer guide for the payload policy.

Shared entry guides, Overview, Standards and Tutorials remain under Resources/Docs; detailed originals stay in each module’s Resources/Docs. Publication assembles and verifies a temporary `.tmp/docs-collection-*` collection, then removes it. No generated collection is retained in source Docs.

## Embedded license notices

The Core project embeds the complete repository `LICENSE` and `VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt`. App and Tool accept `--license` or `--third-party-licenses` as a single standalone argument and exit before GUI or bundle initialization. These notices are original resource text, not localization catalogs; do not translate or shorten them. Windows and Unix publishers compare both command outputs with the source notices before publishing an installer. Update the [notice sources and maintenance record](../../../Licenses/README.md) with dependency changes, rebuild the executable, and rerun both notice checks. An older installer retains its previous embedded notices.

Custom product authoring uses the Tool with --definition; the business PowerShell publisher requires a configured VeroScript business name. InstallerProductDefinition.ReleaseGroup (JSON releaseGroup) equal to Modules rejects new installer/bundle creation. See the product-definition guide for a complete custom-payload example and the separate module archive policy.

- [Release layout and publishing rules](release-layout.md)

<a id="publication-paths"></a>

`veroscript_business_release.guard_tree` rejects linked publication roots and entries. On macOS only, it permits root-owned `/tmp`, `/var` and `/etc` as ancestors when their resolved targets are exactly `/private/tmp`, `/private/var` and `/private/etc`. This permits ordinary temporary publication paths without allowing a linked output root or links within the published collection. Preserve the ancestor/root distinction when changing path checks.

## Standalone installer downloads

Use `package-veroscript-installer.ps1` for a Windows Runtime installer and `package-veroscript-installer.sh` on a matching Linux or macOS host. With the default managed output, the source Runtime archive is read from `Release/Runtime/<target>/<version>/archives/`. A prepared expanded payload can be supplied through `-RuntimePackageDir` or `--runtime-package-dir`. Download names follow `VeroScript<business>Installer-<version>-<target>` with `.exe` on Windows, `.tar.gz` on Linux and `.dmg` on macOS. Linux publishes one archive containing the executable and macOS publishes one disk image containing the app. Follow [installer download validation](release-validation.md#installer-download-validation) for the embedded payload audit.

## Windows executable icon inputs

Windows builds of `veroscript`, `veroscript-runtime`, `veroscript-launcher`, `veroscript-lsp` and `veroscript-dap` embed `Resources/Assets/icons/veroscript-icon.ico` through the source CMake helper `vs_use_product_icon`. CLI target declarations opt in with `PRODUCT_ICON`. These executable targets require the RC language and a Windows resource compiler. The root project discovers and enables RC when available. A parent project that reuses the source helper must enable RC for its Windows executable targets. Library-only profiles can configure without RC.

ICO changes require rebuilding the affected executables. `VeroScriptProductIcon.rc.in` records the icon SHA-256 so generated RC input changes also trigger Visual Studio incremental rebuilds. Keep C/C++ flags scoped away from RC. The Installer app separately consumes the ICO and the 256px PNG through its project resources, and the macOS installer bundle uses ICNS. The [asset guide](../../../../../Resources/Assets/icons/README.md) owns generation and format details. `python Resources/Tools/Packaging/veroscript_icons.py check --require-tracked` checks maintained source files. Release audits can read Windows PE icon groups and compare their frames with the maintained ICO on any build host, without launching the target executable. Record actual shell presentation and target-platform acceptance separately.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](build-and-bundle.md) · [fr-FR](../fr-FR/build-and-bundle.md) · [ja-JP](../ja-JP/build-and-bundle.md) · [ko-KR](../ko-KR/build-and-bundle.md) · [zh-CN](../zh-CN/build-and-bundle.md) · [zh-TW](../zh-TW/build-and-bundle.md)
