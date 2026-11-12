# Install and use VeroScript

Choose the package for your operating system and architecture. The Runtime runs scripts and includes CLI, LSP and DAP; SDK products let you embed VeroScript in a host application. Installers install selected components. Portable archives can be extracted to a directory of your choice.

## Choose a product

| Product | Use |
|---|---|
| Runtime | Run scripts; CLI/LSP/DAP and standard library |
| Core SDK | C++ core and Tooling headers, libraries and CMake package |
| Interop SDK | C ABI for native or foreign-language hosts |
| C++ SDK | C++ adapter; requires the matching Core SDK build |
| C# SDK | .NET adapter and matching native Interop runtime |
| Python SDK | CPython adapter and Python tools |
| WebAssembly SDK | WebAssembly runtime and TypeScript API for JavaScript hosts |

Runtime does not install an SDK. The installer offers the tools, AOT and symbols included in its package. Obtain documentation and examples separately from Resources. SDK destinations include the version by default, allowing parallel installations. Product versions can differ; follow each SDK’s dependency and ABI requirements.

In `Release/<business-route>/<target>/<version>/`, choose `installers` for desktop installers, `archives` for portable Runtime or module archives, `sdks` for development SDKs, `packages` for ecosystem packages and editor extensions, and `symbols` for debug symbols. Shared distributions use `any`; the portable WebAssembly SDK uses `wasm32`. Obtain optional Modules separately; they have no standalone installers.

## Install, update, and relocate

On Windows, launch the `.exe` installer. On Linux, extract the `.tar.gz` and run its program; on macOS, open the `.dmg` and launch its `.app`. Follow the wizard: choose functional components, choose the destination, configure file associations when offered, review your choices, then install. Use Next and Back to move between pages; larger component lists use additional pages without scrolling the window. Documentation and examples are not installation options. The installer is self-contained: the target machine does not need a separately installed .NET Runtime. Runtime PATH integration is off by default; open a new terminal after enabling it.

The installer checks its current component destination directories for valid records belonging to the same product. It enters update mode only when it finds a matching installation there, including a rebuilt package with the same product version. Updates preserve files created by users. A conflict between a new package file and a user file fails; move the conflicting file before retrying. Deselecting an installed optional component removes its managed files but preserves user files. Clearing the PATH option removes only entries the installation record owns.

When an existing installation is detected, its record supplies defaults only for component, PATH and supported file-association choices you have not edited. Update and uninstall confirmation pages explicitly identify removal of an owned PATH entry.

The installation root is the parent directory selected in the wizard, not a component directory such as `SDK/Core/<version>`. Built-in products use versioned component directories: a different product version can be installed alongside an older one even under the same root. The installer does not automatically scan, migrate or uninstall other versions. To replace an older version, install and verify the new one, then uninstall the old one using its original installer and record. To change the installation root, install at the new root, copy your own files, and uninstall the old installation from its record; do not drag managed component directories to relocate them.

## Windows native runtime files

Windows packages include the matching Visual C++ runtime DLLs beside the native programs and libraries. Extract the entire archive or use the installer, and keep these DLLs and their `licenses/MicrosoftVisualCpp/NOTICE.txt` with the corresponding program or native library when deploying it. Copying only a native program executable, Interop DLL or Python extension can leave required dependencies behind. The operating system must still provide the Universal CRT.

## Repair and uninstall

Every installed component root contains `.veroscript-install.json`, which records all component locations for the installation. Any valid record within a component may be used. The following example assumes the Runtime root is `D:\Apps\VeroScript`; replace it and the EXE path with your actual locations:

Choose one operation below. Replace the sample installer and record paths with your own. Each command opens its respective window and runs only after confirmation.

Repair only:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --repair-record $record
```

Uninstall only:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --uninstall-record $record
```

Repair must use the original product package: version, platform, architecture, and manifest fingerprint must match the installed record. Retain the original installer. Uninstall does not reread the embedded payload. Repair and uninstall modes cannot be specified together. Windows "Installed apps" registration and a Start-menu uninstall entry are not currently provided; use the commands above.

Uninstall removes only managed files, installer-owned PATH entries and association/icon values still owned by this installation; directories containing user files are retained. If a previous install or uninstall was interrupted, reading the record, repairing, or uninstalling first recovers the transaction. Do not delete `.veroscript-transaction-*.json`, staging directories, or backup directories by hand to bypass recovery.

## Use the Core SDK from CMake

Add the extracted SDK root to `CMAKE_PREFIX_PATH`:

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScript)
```

The Core SDK imported target supplies include directories, the C++17 requirement and system link dependencies. Use the imported target when configuring your host.

The Core SDK provides `VeroScript::VeroScript` and `VeroScript::VeroScriptTooling`. Its imported targets propagate the public-header compile requirements. The JIT backend is a separate module; Core does not export `VeroScript::asmjit`. C++/AOT libraries belong to the separate C++ product. For an installer-deployed Core SDK, add `<install-root>/SDK/Core/<version>` to `CMAKE_PREFIX_PATH`; `<install-root>` is the destination selected in the wizard. Obtain examples separately; they are not included in the portable SDK or current installer.

## Use the C++ SDK from CMake

The C++ SDK is a separate product, but its static library and the Core SDK share one public C++ ABI. Add both the matching Core SDK root and the C++ product's `Runtime` directory to `CMAKE_PREFIX_PATH`:

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScriptCpp)
```

`VeroScriptCppConfig.cmake` requests the exact Core SDK version recorded when the C++ package was built and verifies the Core-library SHA-256 build identifier. That Core version may differ from the C++ product version. Use the Core dependency recorded in the C++ release manifest; matching version numbers alone cannot establish build compatibility. Add `<cpp-package>/Runtime` for a portable package, or `<install-root>/SDK/Cpp/<version>/Runtime` for the default installer layout. Request `COMPONENTS Tools Aot` when those optional components are installed.

Core SDK tools discover packaged `assets/stdlib` and `assets/localization` relative to `bin`. To run an example from a separately obtained Resources distribution, replace the absolute path below with its actual location:

```powershell
.\bin\veroscript.exe --bc-cache=off 'D:\Packages\Resources\Scripts\examples\standard_library_algorithms.vs'
```

## Offline documentation and examples

Obtain the matching Resources distribution separately. Open `Resources/Docs/README.md`, choose a language and follow relative links to the reference and tutorials. A standalone documentation ZIP uses `docs/README.md` inside its extracted package. Resources also supplies script examples under `Resources/Scripts/examples`. SDK archives and current installers do not bundle documentation/examples components. Online source references require network access; examples may require the matching SDK and dependencies.

## Installer language

The installer provides English, French, Japanese, Korean, Simplified Chinese and Traditional Chinese. Choose a language in the window, or pass `--language` at startup. It initially follows the system UI language and uses English when no supported language matches. This controls the installer UI; use CLI `--lang` or SDK language APIs separately for script diagnostics. In this Windows example, replace the installer path and version with your downloaded file:

```powershell
& 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe' --language en-US
```

## Linux/macOS installation

On Linux, extract the matching `linux-x64` or `linux-arm64` `.tar.gz` installer and run its program. On macOS, open the matching `macos-x64` or `macos-arm64` `.dmg` and launch the installer `.app` inside it. A graphical desktop is required. Repair and uninstall accept the same record switches as on Windows. The examples below use Linux; on macOS, set `installer` to the original `<installer>.app/Contents/MacOS/VeroScriptInstaller` and omit `chmod` for an app on a read-only disk image. Replace all sample paths with your own. Preserve executable permissions when extracting portable Runtime/SDK archives.

Choose one operation below. Replace the sample installer and record paths with your own. Each command opens its respective window and runs only after confirmation.

Repair only:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --repair-record "$record"
```

Uninstall only:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --uninstall-record "$record"
```

## License notices

The project’s full [MIT license](../../../../../LICENSE) is included in this documentation. Installer dependency notices are available through the downloaded installer. Run either notice option on its own; it prints the original notice and exits without opening the installation window. Replace the example path with your downloaded file. The notice text retains its original language; `--language` selects the installation UI separately.

The example temporarily uses UTF-8 so notice characters display correctly in Windows PowerShell 5.1, then restores the terminal’s original encoding.

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$noticeEncoding = [Console]::OutputEncoding
try {
	[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
	& $installer --license | Out-Host
	& $installer --third-party-licenses | Out-Host
}
finally
{
	[Console]::OutputEncoding = $noticeEncoding
}
```

<a id="file-associations"></a>

## Windows file associations

When a Windows installer includes Runtime and the corresponding icons, selecting Runtime enables optional file-association choices. A new installation leaves them off. Register `.vs` and/or `.vsapp` to run with that Runtime, or request only the `.vspkg` icon. Opening an associated `.vs` or `.vsapp` executes it; the `.vspkg` option neither runs nor installs a package.

Registration adds a per-user handler and icons. Choose the default application yourself in Windows Settings; registering a handler does not assign the system default. The optional settings action requests that page after a successful install, and the result page reports if it could not be opened or registration was incomplete. Windows may display the icon of the default application you selected.

Updates reuse recorded association choices unless you edit them. Clearing an option, repair, rollback and uninstall preserve unrelated registrations and later user edits; only unchanged values owned by this installation can be removed or restored. Other platforms and packages without the required Runtime files do not offer these choices.

The `.vspkg` icon is added only when no icon is already registered. Clearing the default-app option only cancels this operation’s Settings request; it does not reset a choice already confirmed in Windows. Uninstall does not edit protected default-app records or guarantee restoration of a previous default. Old records without association ownership perform no association cleanup.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
