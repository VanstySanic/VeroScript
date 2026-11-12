# Toolchain

The VeroScript toolchain covers checking and running scripts, package creation, debugging, AOT generation, binding catalogs, host integration, and SDKs for browser and cross-language hosts.

## Documentation map

- [Examples index](../../../../../VeroScript/Resources/Docs/User/en-US/examples.md): runnable examples organized by language, standard-library, and toolchain topics.
- [C++ SDK](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/index.md): native embedding, host bindings, AOT, C++ front-end, BindGen, and Transpiler.
- [Command-line tool](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md): execution, checking, import paths, macros, backends, and environment variables.
- [Packages and distribution](../../../../../VeroScriptCli/Resources/Docs/User/en-US/packages.md): `.vsapp`, `.vslib`, package versions, inspection, and distribution rules.
- [Debugging](../../../../../VeroScriptTooling/Resources/Docs/User/en-US/debugging.md): DAP over standard input/output, TCP debugging, and editor sessions.
- [Tooling module](../../../../../VeroScriptTooling/Resources/Docs/User/en-US/index.md): analyzer, formatter, language server, workspace queries, and debug transport.
- [Language Server Protocol](../../../../../VeroScriptTooling/Resources/Docs/User/en-US/lsp.md): transport, lifecycle, capabilities, synchronization, configuration, and localization.
- [WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/en-US/index.md): the TypeScript wrapper, language-catalog discovery, virtual files, the C ABI, and browser boundaries.
- [Python SDK](../../../../../Extensions/Language/Python/Resources/Docs/User/en-US/index.md): the CPython session, values, host bindings, packages, reflection, and localization boundary.
- [C# SDK](../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/index.md): the managed .NET session, values, host bindings, packages, reflection, and ownership rules.
- [Interop SDK](../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md): cross-language sessions, value handles, bindings, packages, callbacks, and thread rules.
- [Diagnostic localization](../../../../../VeroScript/Resources/Docs/User/en-US/localization.md): dynamic culture directories, BCP 47 matching, custom catalogs, SDK boundaries, and fallback behavior.

Keep toolchain commands and language versions compatible. Use `veroscript --version` to inspect the installed version; select the diagnostic language with `--lang` or `VERO_LANG`.

[Bytecode VM](../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/en-US/index.md)


<!-- docs-navigation -->

---

[User documentation](../index.md) · [VeroScript](../index.md)

Language: [en-US](index.md) · [fr-FR](../../fr-FR/toolchain/index.md) · [ja-JP](../../ja-JP/toolchain/index.md) · [ko-KR](../../ko-KR/toolchain/index.md) · [zh-CN](../../zh-CN/toolchain/index.md) · [zh-TW](../../zh-TW/toolchain/index.md)
