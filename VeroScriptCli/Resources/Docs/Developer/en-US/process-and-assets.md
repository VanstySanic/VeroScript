# CLI process and asset contract

## Entry points

`VeroScriptCli` is a thin process layer for `veroscript`, `veroscript-lsp`, and
`veroscript-dap`. The ordinary main dispatches compile/check/run and package
operations. REPL and watch code add interactive source and file-change
orchestration. LSP and DAP mains delegate language behavior to Tooling.

The CLI must not duplicate compiler, VM, debugger, or workspace semantics.
Subcommand names, options, environment variables, exit codes, diagnostic IDs,
and output formats are compatibility tokens and are not localized.

## Asset resolution

Resolution distinguishes source roots, SDK roots, `bin` directories, standard
library directories, and localization directories. An executable and its
runtime assets must come from a compatible installation. Environment-variable
and `PATH` fallback is discovery behavior, not permission to mix installations.

## Process behavior

Stdout, stderr, diagnostic line handling, input streams, process cancellation,
and exit-code propagation must remain stable for IDE, SDK, and shell consumers.
LSP/DAP framing belongs to Tooling transport implementations; the CLI only owns
process startup and termination.

## Unicode process boundary

The Windows `wmain` entry point converts UTF-16 arguments to UTF-8 once before calling `VeroScript::Cli::Run`. Keep the owning strings and the argument-pointer array alive for the complete call. File operations use the UTF-8 path helpers. `VeroScriptUnicodeCliTests` covers non-ASCII source and import paths, script arguments, and package execution under VM, mixed and JIT-preferred modes. Terminal display and pipe decoding remain the consumer’s responsibility.

## Registration and dispatch contract

Core exposes `VeroScriptCommandLine.h`; CLI owns the registry, argument parser and dispatch. Native `Create` acquires `INativeExtensionHost::GetService("veroscript.cli", "0.1.1", error)`, casts the matched service to `ICommandLineExtensionService`, and registers an owned `FCommandLineModule` descriptor synchronously. Registration closes before dispatch; a failed declaration invalidates the batch. A combined-role module must tolerate an absent CLI service in non-CLI hosts. Each module must declare at least one command or option; declaring any option requires a Configure callback.

IDs and names are case-sensitive ASCII letters/digits/underscores/hyphens, at most 64 characters, with no leading/trailing hyphen. Core commands/options are reserved. The registry accepts at most 256 modules and 256 commands plus options per module; descriptions/usage/value labels reject control characters and are limited to 8192 bytes. Options default to requiring a value and being non-repeatable; a present flag supplies one empty string and an absent option has no map entry. Use `--name=value` when the value resembles another option.

Configure callbacks validate/store state once in module-ID order before ordinary execution or command dispatch; only the owning module’s values are supplied. They must not execute scripts. Help skips configuration and command execution, while native loading/registration still occurs. Run callbacks receive intact UTF-8 arguments, language tag, streams and the selected environment. Keep borrowed context/state lifetimes bounded by dispatch; do not retain strong references back to the environment owning the registration. Join workers before native Destroy returns. The host retains code, catches callback exceptions and validates exit codes in 0..255. Blocking native commands own their cancellation.

[NativeModule source example](../../../Examples/NativeModule/README.md) demonstrates matched-SDK compilation and package integration. `VeroScriptCliModulesTests` and `VeroScriptCliRegistryTests` cover parsing, name conflicts, Unicode, lifetimes, enable/disable and argument boundaries.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](process-and-assets.md) · [fr-FR](../fr-FR/process-and-assets.md) · [ja-JP](../ja-JP/process-and-assets.md) · [ko-KR](../ko-KR/process-and-assets.md) · [zh-CN](../zh-CN/process-and-assets.md) · [zh-TW](../zh-TW/process-and-assets.md)
