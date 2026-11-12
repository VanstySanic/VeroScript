# VeroScript CLI technical documentation

`VeroScriptCli` is a thin process layer. It provides the `veroscript`,
`veroscript-lsp`, and `veroscript-dap` entry points without reimplementing the
compiler, VM, C++ embedding API, or language-service semantics.

## Process responsibilities

The CLI owns argument parsing, command dispatch, process-level streams, REPL and
watch orchestration, source/file selection, environment and SDK asset lookup,
exit-code policy, and the LSP/DAP process mains. The core, C++ session, AOT, and
Tooling modules remain the source of language behavior.

Command options, subcommand names, environment variables, exit codes, diagnostic
IDs, and output formats are compatibility surfaces. They must not be translated
or changed solely to improve prose.

## Runtime assets

Standalone processes must locate the version-matched standard library and
localization catalogs. Asset resolution must remain relocatable and must not
silently use stale build-directory copies. A missing or incompatible asset is a
startup/configuration failure, not an empty-language fallback.

## Protocol entry points

The LSP and DAP mains delegate protocol behavior to `VeroScriptTooling`. The CLI
must preserve stream framing, request ordering, cancellation, stderr/stdout
separation, and process exit behavior expected by those services.

## Command and asset details

The ordinary main dispatches compile/check/run and package-oriented operations;
the REPL and watch process add interactive source and file-change orchestration.
`VeroScriptLspMain` and `VeroScriptDapMain` are protocol-only process entry
points and delegate language behavior to `VeroScriptTooling`. The CLI must not
create a second implementation of Session, debugger, or workspace state.

The CLI path resolver must preserve the distinction between a source root, an
SDK root, a `bin` directory, the standard-library directory, and the
localization directory. A discovered executable and its assets must be from a
compatible installation; environment-variable and `PATH` fallback is discovery
behavior, not a license to mix versions.

## Detailed pages

- [CLI process and asset contract](process-and-assets.md)

## Source of truth

Use `Private/`, the CLI CMake target, process tests, Tooling public APIs, and
packaging/SDK smoke tests. Update this document with changes to command
dispatch, asset lookup, stream behavior, environment variables, exit codes, or
LSP/DAP process boundaries.

## Project initialization

`Private/VeroScriptCliProject.cpp` implements `project init` as local C++17 file
creation, without loading package environments or invoking compilation. It uses
exclusive file creation to preserve existing files across concurrent calls and
reserves the `project` command in the native CLI registry. The
`VeroScriptProjectInitCliTests` process test covers paths, repeat execution,
conflicts, concurrent creation and write-failure recovery. It is registered only
for native builds; when build services are present it also builds
and runs the generated application and the documented project example. Keep the
project/build templates aligned with their existing `0.1.1` schemas; initialization
does not manufacture dependency locks or install state.
UTF-8 path conversion supports both C++17 and C++20; the packaging CMake regression
compiles the standalone initializer in both modes with warnings treated as errors.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
