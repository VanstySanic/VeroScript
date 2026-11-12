# VeroScript source layout

Source-tree developer reference. Use the localized User guides below for consumer tasks and public API contracts.

[Integrated documentation / 集成文档](README.md)

> **Integrated overview / 综合总览:** [English](Overview/en-US/index.md) · [Français](Overview/fr-FR/index.md) · [日本語](Overview/ja-JP/index.md) · [한국어](Overview/ko-KR/index.md) · [简体中文](Overview/zh-CN/index.md) · [繁體中文](Overview/zh-TW/index.md)

> **User documentation:** [English](User/en-US/index.md) · [Français](User/fr-FR/index.md) · [日本語](User/ja-JP/index.md) · [한국어](User/ko-KR/index.md) · [简体中文](User/zh-CN/index.md) · [繁體中文](User/zh-TW/index.md)

> **Developer documentation:** [English](Developer/en-US/index.md) · [Français](Developer/fr-FR/index.md) · [日本語](Developer/ja-JP/index.md) · [한국어](Developer/ko-KR/index.md) · [简体中文](Developer/zh-CN/index.md) · [繁體中文](Developer/zh-TW/index.md)

This repository contains the VeroScript language implementation, its native
embedding surfaces, developer tools, desktop applications, and shared runtime
assets. Module boundaries are intentional: code outside a module must use that
module's public API. A source-tree implementation target may use `Private/` only
through an explicit, reviewed CMake friend dependency; the core architecture
test keeps the existing VeroScript friend set allow-listed.

Within each module, public headers live directly under `Public/` and
implementation files under `Private/`. Subdirectories describe responsibilities
such as `Parser/`, `Compiler/`, `ExecutionSupport/`, `Internal/`, and `Tests/`; they do not repeat
the module's name or language. Include public headers by their filename, for
example `#include "VeroScriptEngine.h"`, using the module's CMake target.

`Shared/Private/` owns header-only support used by multiple native implementation
targets, including `VeroScriptFloatParsing.h`, `VeroScriptFloatFormatting.h`,
`VeroScriptJsonString.h` and `VeroScriptDarwinPaths.h`.
It is independent of Core's runtime
and public SDK. `vs_use_shared_private_headers(target)` adds this directory as a
PRIVATE include path; do not export it through a target's public interface or use
it to reach another module's `Private/`. Keep shared inputs in implementation
fingerprints and source-distribution inventories. Standalone generated code must
embed the support it needs rather than require this source-tree directory.

Each module keeps its vendored dependencies and original license files under
its own `ThirdParty/`. Application and Build use their own JSON copies; their
builds and release packaging do not borrow another module's third-party paths.

`Modules/Network` groups the independent Json, Network, Http and WebSocket modules.
Each concrete module owns its documentation, examples, localization and tests
under its own `Resources/`. Build helpers belong to the concrete owner's
`Resources/Tools/CMake/`. Json remains independently buildable and does not acquire
a networking dependency from this classification. `Modules/Network/Backend/`
contains the concrete Boost and Curl providers. The Boost backend owns its shared runtime, Boost and TLS
configuration; the Curl backend owns curl. Protocol implementations use the
foundation's explicit provider contract. The Network group and Boost backend
keep these cross-module headers in `Internal/`, exposed through explicit CMake
targets; selected provider headers install under the SDK's `Backend/` include
directory. This boundary is separate from the ordinary consumer API and private
implementation. Installed public SDK headers expose no Boost, curl or OpenSSL types.

`Extensions/Language/` groups Cpp, CSharp and Python integrations;
`Extensions/Platform/` groups Mobile and WebAssembly integrations; and
`Extensions/Editor/` groups VSCode and JetBrains integrations. `Modules/` groups
optional feature modules and independent execution engines. BytecodeVm and AsmJit
live under `Modules/Execution/`; each owns its third-party dependencies, if any.
The [foundation modules](foundation-modules.md) provide independent
Platform, Graphics and Audio contracts with a shared SDL3 implementation, Image and
Font services with Stb and FontBackend (FreeType + HarfBuzz) providers, and Gui with a
RmlUi adapter. They are enabled explicitly. The Gui SDK also has a standalone
entry in `Modules/Multimedia/VeroScriptGui`; its README and implementation guide
cover native/script bindings, markup, styles and implemented controls.
`Modules/Multimedia/` contains Platform, Graphics, Audio, Image, Font and Gui.
`Modules/Multimedia/Backend/` contains Sdl3, Stb, FontBackend and RmlUi.
Resources and third-party sources belong to each concrete module;
public resource contracts contain no third-party library types.

## Native modules

| Path | Responsibility |
|------|----------------|
| `VeroScript/` | Language front end, bytecode, execution contracts, generic JIT services, host bindings, packages, reflection, and hot reload. This is the dependency root. |
| `Modules/Execution/VeroScriptBytecodeVm/` | Independently built bytecode interpreter over the core execution contract. |
| `Modules/Execution/VeroScriptAsmJit/` | Optional native JIT implementation over the core execution contract. |
| `Extensions/Language/Cpp/VeroScriptCpp/` | High-level C++ engine and session API built on the core. |
| `Extensions/Language/Cpp/VeroScriptCppFrontend/` | Shared strict C++17 lexer, declaration AST, diagnostics, and type mapping for C++ tools. |
| `Extensions/Language/Cpp/VeroScriptCppBindGen/` | C++ declarations to Binding Catalog generator and CLI. |
| `Extensions/Language/Cpp/VeroScriptCppTranspiler/` | Strict C++ subset to validated VeroScript source transpiler and CLI. |
| `Extensions/Language/Cpp/VeroScriptCppAot/` | Optional C++ AOT emitter; depends directly on the core. |
| `Extensions/Language/Python/VeroScriptPythonAot/` | Optional standalone Python 3.11–3.15 source emitter; depends directly on the core. |
| `Extensions/Language/Python/VeroScriptPythonFrontend/` | Static Python declaration frontend shared by Python source tools. |
| `Extensions/Language/Python/VeroScriptPythonBindGen/` | Typed Python declarations to Binding Catalog generator. |
| `Extensions/Language/Python/VeroScriptPythonTranspiler/` | Strict Python-to-VeroScript source transpiler. |
| `Extensions/Language/CSharp/VeroScriptCSharpAot/` | Optional C# source and NativeAOT-oriented emitter; depends directly on the core. |
| `Extensions/Language/CSharp/VeroScriptCSharpFrontend/` | Shared semantic C# declaration front end for C# build-time tools. |
| `Extensions/Language/CSharp/VeroScriptCSharpTranspiler/` | Strict C# subset to validated VeroScript source transpiler and CLI. |
| `Extensions/Language/CSharp/VeroScriptCSharpBindGen/` | C# Binding Catalog, CLI, and NativeAOT-friendly registration generator. |
| `VeroScriptPackageManager/` | Optional package client: immutable installations, dependency solving, scoped resources, native contracts, registry transport and offline deployment. |
| `VeroScriptApplication/` | Application archives, protection, local runtime and launcher services. Owns its JSON, ZIP and hashing dependencies. |
| `VeroScriptBuild/` | Configurable build targets, dependency planning and artifact production. Owns its JSON dependency. |
| `VeroScriptTooling/` | Analyzer, formatter, workspace model, LSP, and DAP implementation. |
| `VeroScriptCli/` | Thin `veroscript`, `veroscript-lsp`, and `veroscript-dap` process entry points. |
| `VeroScriptInterop/` | Stable C11 process ABI for non-C++ hosts. |
| `Extensions/Platform/WebAssembly/VeroScriptWebAssembly/` | UI-free WebAssembly API and TypeScript bindings. |

The native dependency direction is:

```text
VeroScript
|- VeroScriptCpp ---- VeroScriptInterop
|                 `-- VeroScriptWebAssembly
|- VeroScriptCppAot
|- VeroScriptCppFrontend -- VeroScriptCppBindGen
|                      `-- VeroScriptCppTranspiler
|- VeroScriptPythonAot
|- VeroScriptCSharpAot
|- VeroScriptCSharpFrontend -- VeroScriptCSharpTranspiler
|                         `-- VeroScriptCSharpBindGen
`- VeroScriptTooling

VeroScriptCli combines the core, C++ session, AOT, tooling and optional package-management modules.
```

## External-language adapters

The [mobile module group](../../Extensions/Platform/Mobile/README.md) contains the shared
host layer and the independent Android and iOS adapters.

| Path | Responsibility |
|------|----------------|
| `Extensions/Language/Python/VeroScriptPython/` | CPython 3.11–3.15 API over `VeroScriptInterop`, including execution, packages, hot reload, typed values, callbacks, containers, reflection, retained closures, persistence, and Python-backed host classes. |
| `Extensions/Platform/Mobile/VeroScriptMobile/` | Embedded VM sessions and runners over the public C ABI; explicit resource and thread ownership. |
| `Extensions/Platform/Mobile/VeroScriptAndroid/` | Android Java/JNI AAR and Prefab integration. |
| `Extensions/Platform/Mobile/VeroScriptIOS/` | Objective-C/Objective-C++ static XCFramework integration. |
| `Extensions/Editor/VSCode/VeroScriptVSCode/` | VS Code editor, tasks and DAP client. |
| `Extensions/Editor/JetBrains/VeroScriptJetBrains/` | Rider Kotlin/JVM editor, run configurations and DAP client. |
| `Extensions/Language/CSharp/VeroScriptCSharp/` | NativeAOT-compatible .NET 8/9 API over `VeroScriptInterop`, including host bindings, packages, containers, callbacks, and serialization. |

Language adapters depend only on the stable C ABI and must not include C++
implementation headers or reproduce compiler/runtime behavior.

## Applications and shared assets

| Path | Responsibility |
|------|----------------|
| `VeroScriptInstaller/` | Cross-platform install/repair/uninstall core, graphical app, bundle tool, and tests. |
| `VeroScript/Resources/Scripts/stdlib/` | Script standard-library modules shipped with the runtime. |
| [`VeroScript/Resources/Benchmarks/`](../../VeroScript/Resources/Benchmarks/README.md) | Opt-in runtime and host performance tools, fixed inputs and historical baselines; excluded from release packages. |
| `<module>/Resources/Localization/` | Diagnostic catalogs owned by each core, SDK, or tooling module. |
| `<module>/Resources/Assets/` | Module-owned icons, fonts, shader sources and other static media. |
| `<module>/Resources/Licenses/` | Module-specific notices; original dependency licenses remain with their vendored sources. |
| `<module>/Resources/Tests/` | Test inputs, integration fixtures and resource-oriented test projects. |
| `Extensions/Language/Python/Resources/Localization/` | Messages shared by Python tools; each tool owns its specific catalogs. |
| `VeroScriptInstaller/App/Resources/Localization/` | Installer UI catalogs. |
| `VeroScript/Resources/Scripts/examples/` | Supported, release-tested script examples. |
| `Modules/Execution/VeroScriptAsmJit/` | Optional concrete JIT backend, explicitly injected into Core. |
| `Modules/Execution/VeroScriptAsmJit/ThirdParty/asmjit/` | Pinned, offline dependency owned by the backend module. |
| `<module>/README.md` | Source-module purpose, relationships and links to its guide originals. |
| `<module>/Resources/Docs/README.md` | Six-language User/Developer selector and collection publication entry. |
| `<module>/Resources/Docs/User/<locale>/` | Canonical language, SDK and tool user documentation. |
| `<module>/Resources/Docs/User/<locale>/Reference/` | Detailed module reference pages, published through the module's User manifest. |
| `<module>/Resources/Docs/Developer/<locale>/` | Module architecture, maintenance, tests and release contracts. |
| `Resources/Docs/` | Authored shared User/Developer entry guides, Overview, Standards and Tutorials. |
| Repository `.tmp/docs-collection-*/` | Temporary complete collection assembled and validated during publication, then removed. |
| `Resources/Assets/` | Shared product icons and platform file-association templates. |
| [`Resources/Docs/Standards/`](Standards/README.md) | Canonical general and language coding standards for repository-owned source code. |
| `Resources/Tools/Packaging/` | Shared packaging, verification, signing, deployment, documentation and dependent build tools, with their tests. |
| `Resources/Tools/CMake/` | Repository-wide CMake helpers and shared package-version generation. |
| `<module>/Resources/Tools/CMake/` | Module-owned CMake helpers and SDK configuration templates. |
| `<module>/Resources/Tools/Packaging/` | Module-specific packaging and distribution validation scripts. |
| `Modules/Execution/VeroScriptAsmJit/Resources/Tools/Android/` | The backend's Android AAR packaging project. |
| [`Resources/Docs/Tutorials/`](Tutorials/README.md) | Ordered learning material by culture. |
| Version control and ignored validation outputs | Previous document revisions and exact validation receipts; excluded from All/User publication. |

`VeroScript/Resources/Scripts` owns the standard library and pure-script examples.
Each module owns its user and developer documentation. `Resources/Docs` owns
shared documentation entrypoints, tutorials, overview pages and coding standards. Shared platform assets belong to
`Resources/Assets/`. Runtime packaging reads these source trees and assembles localization from each
module’s `Resources/Localization/` directory. Complete releases publish documentation
and tutorials under `Resources/Docs`, scripts under `Resources/Scripts`, and shared
assets under `Resources/Assets`.

Source resource ownership and installed paths are distinct. For example,
JetBrains icon sources live under `Resources/Assets/Icons/`, while Gradle maps
runtime icons to `Icons/` and Marketplace icons to `META-INF/` in the plugin JAR.
Packaging copies only the resources needed by each product; it does not install
an entire source `Resources/` tree containing examples, tests and build tools.

## Integration examples

| Path | Responsibility |
|------|----------------|
| `VeroScript/Resources/Examples/EmbedMinimal/` | Standalone consumer used to validate the Core SDK. |
| `VeroScriptInterop/Resources/Examples/InteropMinimal/` | Standalone C11 consumer used to validate the Interop SDK. |
| `Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/Embed/` | Repository-integrated native embedding example and self-test. |
| `Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal/` | Standalone consumer used to validate the C++ SDK. |

Native and SDK consumer examples belong to their module's `Resources/Examples/`;
script-only `.vs` examples remain
under `VeroScript/Resources/Scripts/examples/`.

## Source conventions

Native libraries follow the same layout:

- `Public/` contains supported headers only.
- `Private/` contains implementations and internal headers.
- `Private/Tests/` contains focused regression executables.
- semantic subdirectories such as `Compiler/`, `Parser/`, `Vm/`, `Jit/`, and
  `Internal/` partition implementation responsibilities.
- `Resources/` contains module-owned documentation, localization and examples;
  the core's `Resources/Scripts/` contains runtime scripts deployed by CMake.
- auxiliary build and packaging tools live under `Resources/Tools/<purpose>/`;
  CMake test drivers remain with their tests, and compiled tool implementations
  remain under `Private/`.

Language and platform integrations retain required ecosystem layouts, including
Python package and `tests/` trees, .NET project and `Tests/` trees, and Android
Gradle entrypoints and `src/` trees. Their sample inputs belong to
`Resources/Examples/`. Entry files such as `CMakeLists.txt`, `pyproject.toml`,
`.csproj`, `package.json`, and Gradle settings stay at the appropriate project root.

CMake recursively discovers source files under `Private/`, exposes only public
include roots, groups files by their on-disk tree in generated IDE projects, and
rejects implementation `.cpp` files placed under `Public/`.

Large implementation files should be split when a stable responsibility boundary
exists, not solely to meet a line-count target. The current compiler, parser, VM,
JIT, tooling queries, and AOT emitter already use responsibility-based partitions;
large milestone and integration tests remain single executables so their setup and
failure context stay local.

Generated directories such as `build/`, `dist/`, `.verocache/`, and `.codex-*`
are never source or release inputs.

## Examples and release checks

[`VeroScript/Resources/Scripts/examples/README.md`](../../VeroScript/Resources/Scripts/examples/README.md)
indexes every supported example and records the recommended source style. Before
publishing, examples are compiled with bytecode caching disabled, native tests are
run through CTest, installer console tests are run through `dotnet run`, and the
packaging smoke tests verify the copied SDK/runtime layouts.

Minimal SDK smoke tests are not a complete release gate: public compile requirements
and standard-library discovery must also be checked against extracted packages.
Current SDK requirements are maintained in [Core build and runtime assets](../../VeroScript/Resources/Docs/Developer/en-US/build-and-assets.md); release gates are in [release validation](../../VeroScriptInstaller/Resources/Docs/Developer/en-US/release-validation.md).

Detailed publishing and integration procedures belong to their guide originals:

| Area | Maintained guide |
| --- | --- |
| Core and Interop | [Build and assets](../../VeroScript/Resources/Docs/Developer/en-US/build-and-assets.md) · [ABI and lifetime](../../VeroScriptInterop/Resources/Docs/Developer/en-US/abi-and-lifetime.md) |
| C++ tools and SDK | [Build and tests](../../Extensions/Language/Cpp/Resources/Docs/Developer/en-US/build-and-tests.md) |
| Python tools and packages | [Toolchain and packaging](../../Extensions/Language/Python/Resources/Docs/Developer/en-US/toolchain-and-packaging.md#python-package) |
| C# tools and packages | [Toolchain and packaging](../../Extensions/Language/CSharp/Resources/Docs/Developer/en-US/toolchain-and-packaging.md) |
| WebAssembly | [Build and artifacts](../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/en-US/build-and-artifacts.md) |
| Editors | [Shared editor maintenance](Developer/en-US/editors.md) |
| Multimedia providers | [Shared maintenance](Developer/en-US/multimedia.md) |
| Installers | [Build and bundle](../../VeroScriptInstaller/Resources/Docs/Developer/en-US/build-and-bundle.md) |

Source examples have their own local entries:

- [Core SDK examples](../../VeroScript/Resources/Examples/README.md)
- [C++ embedding and SDK examples](../../Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/README.md)
- [Interop SDK examples](../../VeroScriptInterop/Resources/Examples/README.md)

## Documentation assembly

[Documentation sources](README.md) link directly to the maintained originals. Shared reading paths, cross-module maintenance, Tutorials and Standards belong to `Resources/Docs`; module-specific User contracts and Developer implementation notes belong to the owning module's `Resources/Docs`.

The [documentation maintenance guide](Developer/en-US/index.md) owns the six-language rules, manifests, link boundaries, validation commands and All/User publication procedure. Publication validates a temporary collection assembled from current originals and removes it afterwards. Source Docs directories contain no persistent generated collection. Full Resources releases carry the User view, including Tutorials, at `Resources/Docs`.

## Application artifacts and build graph

`VeroScriptApplication` owns ZIP application content, inventory/trust, materialization, native envelopes and the minimal runtime/bootstrap. `VeroScriptBuild` owns strict graph planning, providers, receipts and output publication. Both are top-level host services. The full `VeroScriptPackageManager` and its separately linkable read-only `VeroScriptPackageRuntime` keep distribution installation and frozen environment consumption distinct. Core only exposes optional bounded artifact readers/decoders and immutable module/resource owners. It does not link the ZIP, HTTP, crypto or build services.
