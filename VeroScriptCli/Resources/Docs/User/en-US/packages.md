# Packages and distribution

VeroScript uses separate package formats for runnable applications and libraries imported by other scripts:

| Format | Purpose |
| --- | --- |
| `.vsapp` | Runnable application package. |
| `.vslib` | Binary library package imported by other scripts. |
| `.vsbc` | Bytecode cache file, not a distribution package. |
| `.vspkg` / `.zip` | ZIP installation distribution with a root `veroscript-package.json`; both suffixes are case insensitive. |

## Building an application package

Use `--emit-package` to select the output file:

```powershell
veroscript --emit-package DemoApp.vsapp main.vs
```

An application package normally contains its entry module and its module dependencies. Run it with:

```powershell
veroscript DemoApp.vsapp
```

## Building a library package

A library source should explicitly export its public API and normally does not provide an application entry point:

```powershell
veroscript --emit-package DemoLibrary.vslib package_library.vs
```

Other scripts import the library with ordinary `import` syntax:

```VeroScript
import "DemoLibrary.vslib";
```

Declarations without `export` remain private to the module and are not part of the package API.

## Package options

| Option | Purpose |
| --- | --- |
| `--package-file <file>` | Adds a source module to the package; repeatable. |
| `--package-version <N>` | Sets the package version. |
| `--strip-package-debug` | Removes source-file and debug line-table information. |
| `package inspect <package>` | Inspects a `.vsapp` or `.vslib` package. |

```powershell
veroscript package inspect DemoLibrary.vslib
```

## Source and binary packages

Import `.vs` source modules directly during development and compile libraries to `.vslib` for distribution. Both forms use the same module-visibility rules, but a binary package must remain compatible with the runtime and bindings that consume it.

## Installing extension packages

The optional package manager installs into the current project directory by default. `--path` selects another project; `--root` selects this CLI’s verified Runtime/Core SDK, and `--root --path` selects another verified component root. Relative paths use the command startup directory. Runtime packages use `Packages`; project/SDK packages use `.veroscript/packages`. Run/check/eval/REPL discover a project first, then use only the executing component. There is no user installation scope or implicit environment merge. AOT uses the same selectors within its supported capabilities.

Extension distributions use standard ZIP data with either `.vspkg` or `.zip` (case insensitive). `pack` defaults to `<name>-<version>.vspkg`; explicit `.zip` output is supported. `install` and `publish` validate the same archive contents and root `veroscript-package.json`. Renaming a corrupt or invalid archive does not make it installable. Download URLs need no archive suffix; hashes and offline cache reuse remain based on the actual bytes.

```sh
veroscript pack ./library
veroscript pack ./library --output ./library.zip
veroscript pack ./library --output ./library-0.1.1.vspkg
veroscript publish ./library-0.1.1.vspkg
veroscript install ./library.vspkg
veroscript install ./library.zip --path "./My App"
veroscript install ./library.vspkg --root
veroscript install ./library.vspkg --root --path "D:/VeroScript/SDK/Core/0.1.1"
veroscript --path "./My App" "./My App/main.vs"
```

[Install and manage extension packages](../../../../../VeroScriptPackageManager/Resources/Docs/User/en-US/index.md)

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](packages.md) · [fr-FR](../fr-FR/packages.md) · [ja-JP](../ja-JP/packages.md) · [ko-KR](../ko-KR/packages.md) · [zh-CN](../zh-CN/packages.md) · [zh-TW](../zh-TW/packages.md)
