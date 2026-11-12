# VeroScript Core SDK examples

[`I18n/`](I18n) demonstrates an immutable resource provider and typed message
resolution through independent application contexts using Core and the Bytecode VM SDK.

> **User documentation:** [English](../Docs/User/en-US/examples.md) · [Français](../Docs/User/fr-FR/examples.md) · [日本語](../Docs/User/ja-JP/examples.md) · [한국어](../Docs/User/ko-KR/examples.md) · [简体中文](../Docs/User/zh-CN/examples.md) · [繁體中文](../Docs/User/zh-TW/examples.md)

`EmbedMinimal/` consumes installed Core and Bytecode VM SDKs. SDK consumer projects are
deliberately independent from the repository's root build so the
packaging smoke tests detect missing headers, libraries, CMake targets, or
transitive link dependencies.

- `EmbedMinimal/` links `VeroScript::VeroScript` and
  `VeroScript::VeroScriptBytecodeVm`, compiles source and runs
  it through Mixed/VM by default. Pass a compatible JIT backend file or directory
  as its first argument to request JIT. Both Core SDK packaging entries exercise it
  against the staging SDK without copying examples into the final product.
  Its `templates.cpp` consumer also checks mixed type/value templates, method
  templates, immutable signature queries, template metadata and VM execution.
- [`CppMinimal/`](../../../Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal) belongs to the C++ module and consumes the separately packaged Core and C++ SDKs through
  `VeroScript::VeroScriptCpp` and verifies session-level JIT cache reuse plus
  the version/build-identity handshake between the two packages.
- [`InteropMinimal/`](../../../VeroScriptInterop/Resources/Examples/InteropMinimal) belongs to the Interop module and consumes `VeroScript::VeroScriptInterop` through
  the stable C11 ABI.

The full-release Core SDK contains the language core and tooling; C++, C# and
Python integrations and their AOT backends are separate products. The standalone
`package-veroscript-sdk.ps1` follows the same split; legacy combined SDK layouts
are not supported.
Generated C# and Python applications do not link the
VeroScript runtime; the targets are used only by native tools that perform
generation.

The packaging scripts configure these source projects against the staged SDK. Final product archives omit examples;
obtain these source projects separately when building a consumer. Product implementation code does
not belong in this directory.

## Build an external Core SDK example

From the repository root, supply the installed Core and matching Bytecode VM SDK
prefixes, each containing `include/` and `lib/`. If both are installed into one
prefix, provide that directory once:

```sh
cmake -S VeroScript/Resources/Examples/EmbedMinimal -B build/core-sdk-example -DCMAKE_PREFIX_PATH="/absolute/path/to/CoreSDK;/absolute/path/to/BytecodeVmSDK"
cmake --build build/core-sdk-example --config Release
ctest --test-dir build/core-sdk-example -C Release --output-on-failure
```

Packaging validation and published SDK consumers use the source project above;
final SDK archives do not contain an `examples/embed-minimal` directory.
Set `VEROSCRIPT_TEST_JIT_BACKEND` to a compatible backend file or directory during
configuration to register the additional JIT test. Core SDK and backend binaries
must match the host platform, architecture and supported backend ABI.

The exported CMake targets provide include paths, C++17 and system link
dependencies. Other build systems can configure these directly. For example,
a Windows/MSVC C++17 execution host uses Core's `include/Script` and
`lib/VeroScript.lib`, plus the Bytecode VM SDK's `include/Vm` and
`lib/VeroScriptBytecodeVm.lib`, with a compatible compiler/runtime configuration.
Use public header filenames
such as `VeroScriptCompile.h` and `VeroScriptVm.h`. SDK directories are build
inputs and do not need to be added to `PATH`.

The path-taking examples convert Windows wide command-line arguments to the
public API's UTF-8 path convention. Their CMake projects also select the Unicode
entry point for MinGW builds.

Applications that import source modules deploy the standard library and supply
its search root or virtual source resources. Localization catalogs are deployed
and loaded by the host. A chosen JIT backend is another application deployment
input. An application should resolve these resources from its own deployment
layout rather than depend on the developer's SDK installation directory.

## Release coverage

Passing a minimal consumer is necessary, but is not a complete release sign-off.
`EmbedMinimal` deliberately does not request C++17 itself. This verifies that the
imported SDK targets propagate the minimum language standard. The consumer also
rejects concrete-backend and test-hook macros leaking into Core's public compile
settings. Its CMake configuration first verifies that the installed Core package
rejects a higher same-major minimum version, then requires the actual package version.
`InteropMinimal` also compares
that package semantic version with the leading semantic-version field reported by
the loaded DLL, preventing a package from advertising a version its runtime does
not implement.

The packaging checks also run a standard-library example through the packaged CLI
with repository and environment search paths disabled. This covers automatic
discovery of `assets/stdlib`; the Core implementation also applies the sibling
`assets/localization` layout and Tooling uses the same standard-library root.
Production packages additionally reject producer builds with
`VEROSCRIPT_ENABLE_JIT_TEST_HOOKS=ON`. Current public build and resource requirements
are maintained in [Core build and runtime assets](../Docs/Developer/en-US/build-and-assets.md).

Before release, test a consumer that links the imported Core and Bytecode VM targets, compare
its public configuration with the producer, and run standard-library examples from
an extracted SDK without repository paths, environment overrides, or cached bytecode.
Verify both standalone and full-release packaging entries. Record the exact source
revision, build configuration, platform, artifact hashes, and untested conditions.

- [ModuleEnvironment](ModuleEnvironment/README.md): resolved module scopes with an in-memory module.
- [AnnotationProcessing](AnnotationProcessing/): host-registered annotation processing.
- [CompileProcessing](CompileProcessing/): native compile processors and generated declarations.
- [ScriptCompileExtensions](ScriptCompileExtensions/README.md): script-registered annotations,
  generated builder types and reusable compiler packages.
