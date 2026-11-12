# VeroScript integrated overview

[Source code on GitHub](https://github.com/VanstySanic/VeroScript)

This overview connects the language, tools, host SDKs and maintenance guides by task. Follow the module references for detailed semantics, APIs and limits; maintain cross-module relationships and reading paths here.

## Cross-platform deployment

| Deployment | Target configurations | Guide |
| --- | --- | --- |
| Desktop Runtime and SDKs | Windows (x86, x64, ARM64); Linux (x64, ARM64); macOS (x64, ARM64) | [Installation and distribution](../../../../VeroScriptInstaller/Resources/Docs/User/en-US/index.md) |
| Mobile embedding | Android, iOS (experimental adapters) | [Mobile SDK](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/en-US/index.md) |
| Browser and JavaScript hosts | WebAssembly | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/en-US/index.md) |

Choose a release package for your operating system and CPU architecture. Package availability and validation status vary by product and release; consult its platform notes for dependencies, available backends and deployment requirements.

## Choose a reading path

| Goal | Start here | Continue with |
| --- | --- | --- |
| Write and run scripts | [Introduction](../../../../VeroScript/Resources/Docs/User/en-US/introduction.md) → [Language basics](../../../../VeroScript/Resources/Docs/User/en-US/language-basics/index.md) | [CLI](../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md), [Standard library](../../../../VeroScript/Resources/Docs/User/en-US/standard-library/index.md), [Examples index](../../../../VeroScript/Resources/Docs/User/en-US/examples.md) |
| Install, update or distribute | [Installation and distribution](../../../../VeroScriptInstaller/Resources/Docs/User/en-US/index.md) | [Packages](../../../../VeroScriptCli/Resources/Docs/User/en-US/packages.md), [CLI reference](../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md) |
| Embed scripts in a host | Select an SDK below | [Interop boundaries](../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md), [Toolchain and integration](../../User/en-US/toolchain/index.md) |
| Edit, diagnose and debug | [CLI reference](../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md) | [LSP](../../../../VeroScriptTooling/Resources/Docs/User/en-US/lsp.md), [Debugging](../../../../VeroScriptTooling/Resources/Docs/User/en-US/debugging.md), [Diagnostic localization](../../../../VeroScript/Resources/Docs/User/en-US/localization.md) |
| Develop or maintain the implementation | [Developer reading path](#developer-reading-path) | [All Developer guides](../../Developer/en-US/index.md) |

Confirm the installed components and versions before choosing an execution backend, host bindings or package configuration. Commands and SDK examples require the corresponding components; reference documentation does not replace build and runtime acceptance checks.

## Select a host SDK

| Host | User reference | Maintenance entry |
| --- | --- | --- |
| Native C++ | [C++ SDK](../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/index.md) | [C++ modules](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/en-US/index.md) |
| C ABI or another language | [Interop SDK](../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md) | [ABI and lifetime](../../../../VeroScriptInterop/Resources/Docs/Developer/en-US/abi-and-lifetime.md) |
| CPython | [Python SDK](../../../../Extensions/Language/Python/Resources/Docs/User/en-US/index.md) | [Python modules](../../../../Extensions/Language/Python/Resources/Docs/Developer/en-US/index.md) |
| .NET / C# | [C# SDK](../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/index.md) | [C# modules](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/en-US/index.md) |
| Browser or JavaScript | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/en-US/index.md) | [WebAssembly module](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/en-US/index.md) |

Consult the chosen SDK's contract for values, handles, callbacks, threading and disposal. Supported source subsets, AOT restrictions and target environments are defined by each module's documentation; a language name alone does not establish compatibility.

## Developer reading path

1. Read [Core architecture and pipeline](../../../../VeroScript/Resources/Docs/Developer/en-US/architecture-and-pipeline.md) and [Execution and state](../../../../VeroScript/Resources/Docs/Developer/en-US/execution-and-state.md) for compilation, execution and state boundaries; continue with [JIT and concurrency](../../../../VeroScript/Resources/Docs/Developer/en-US/jit-and-concurrency.md) when working on backends or concurrency.
2. Follow host boundaries through [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/en-US/index.md), [C++ sessions](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/en-US/session.md) and the relevant language adapter guides.
3. Follow tool boundaries through [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/en-US/index.md), [Tooling protocols](../../../../VeroScriptTooling/Resources/Docs/Developer/en-US/architecture-and-protocols.md).
4. Before release, review [Build and assets](../../../../VeroScript/Resources/Docs/Developer/en-US/build-and-assets.md), [Installer build and packaging](../../../../VeroScriptInstaller/Resources/Docs/Developer/en-US/build-and-bundle.md) and [Release validation procedure](../../../../VeroScriptInstaller/Resources/Docs/Developer/en-US/release-validation.md).

Previous validation results apply only to their recorded sources, artifacts and environments. Follow the [Release validation procedure](../../../../VeroScriptInstaller/Resources/Docs/Developer/en-US/release-validation.md) for product builds, integrity checks and runtime acceptance records; retain evidence in ignored output directories.

## Documentation and publication

Use the [User entry](../../User/en-US/index.md) for the complete module map. Documentation ownership, six-language maintenance, validation commands and All/User packaging are maintained in the [documentation maintenance guide](../../Developer/en-US/index.md).

Published document links and anchors work offline; source-code references and external sites require a network connection. Use the User collection for usage and tutorials, and All for maintenance guides and standards. Neither collection includes History.

[User documentation](../../User/en-US/index.md) · [Developer documentation](../../Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
