# VeroScript tutorials

These pages are organized around completing tasks. Each tutorial starts with a small goal, grows the program step by step, and ends with exercises. Use the [docs](../../User/en-US/index.md) for language rules and complete API details.

## Before you start

You need:

1. The VeroScript command-line tool available through `veroscript --version`.
2. A working directory for script files.
3. An editor that can save plain text files.

Code-block labels identify the language or shell. Run `powershell` commands in PowerShell and `sh`/`bash` commands in the corresponding shell; adjust paths for your operating system. `text` blocks show expected output. Complete programs generally use `int main` and an explicit exit status; `void main` remains valid for short examples that do not need to return one.

`--check` validates the program without calling application `main`. Activated [script compile extensions](../../../../VeroScript/Resources/Docs/User/en-US/standard-library/compiler.md) can still execute under the compiler’s limits and capability policy.

## Recommended path

### Stage 1: run your first script

- [01: Your first script](getting-started/01-first-script.md)
- [02: Checking, running, and reading arguments](getting-started/02-cli-and-arguments.md)

### Stage 2: learn the language skeleton

- [03: Values, conditions, and loops](language/03-values-and-control-flow.md)
- [04: Functions and collections](language/04-functions-and-collections.md)
- [05: Structs, classes, and interfaces](language/05-structs-classes-and-interfaces.md)
- [06: References, reference returns, and lambda captures](language/06-references-and-captures.md)
- [07: Modules, imports, and the preprocessor](language/07-modules-and-preprocessor.md)

### Stage 3: build useful programs

- [08: Strings, files, and paths](standard-library/08-strings-files-and-paths.md)
- [09: Concurrency and events](standard-library/09-concurrency-and-events.md)
- [20: Read and copy a binary packet](standard-library/20-managed-memory.md)
- [22: Format application text and retain a snapshot](standard-library/22-application-i18n.md)

### Stage 4: ship and integrate

- [10: Building packages and distributing an application](toolchain/10-packages-and-distribution.md)
- [11: Debugging and AOT generation](toolchain/11-debugging-and-aot.md)
- [12: Host bindings and script integration](advanced/12-host-bindings.md)
- [13: Runtime reflection](advanced/13-reflection.md)
- [16: Interactive input in the WebAssembly SDK](toolchain/16-web-interactive-input.md)
- [17: Optional values, weak references, and shared struct handles](advanced/17-value-utility-types.md)
- [18: Choose and use the C++ toolchain modules](cpp/index.md)
- [Python source tools](python/index.md)
- [C# source tools](csharp/index.md)
- [Tooling workflow: connect an editor and debugger](toolchain/17-tooling-workflow.md)
- [19: Run a script in a mobile application](toolchain/19-mobile-embedding.md)
- [21: Install a local extension package](toolchain/21-package-manager.md)
- [23: Build a library and application](toolchain/23-application-build.md)
- [24: Collections, navigation and dialogs](toolchain/24-gui-collections.md)


### Stage 5: standardize a project

- [14: Use the `std` standard namespace](advanced/14-standard-namespace.md)
- [15: Configure a workspace and diagnostic localization](toolchain/15-workspace-and-localization.md)

## How to use these tutorials

Complete the first two stages in order. The Stage 3 tutorials are task-oriented and can be selected as needed; Stage 4 is intended for scripts that are ready to be delivered or embedded.

Every tutorial can be read independently, but do not skip `--check`: it finds syntax, type, and import problems before execution. Do the exercises, even if the change is small; modifying an input or adding a branch is enough to verify that you understood the example.

## References

- [Language basics](../../../../VeroScript/Resources/Docs/User/en-US/language-basics/index.md): syntax and semantics.
- [Standard-library reference](../../../../VeroScript/Resources/Docs/User/en-US/standard-library/index.md): function signatures and boundaries.
- [Toolchain](../../User/en-US/toolchain/index.md): CLI, packages, debugging, and host integration.
- [Runnable examples index](../../../../VeroScript/Resources/Docs/User/en-US/examples.md): the distributed examples grouped by topic.

<!-- docs-navigation -->

---

[Tutorials](index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
