# VeroScript for Visual Studio Code

Edit, run and debug VeroScript in desktop Visual Studio Code.

## Features

- Syntax coloring, contextual snippets, a file icon and optional brace placement on a new line.
- Completion, hover, signature help, navigation, diagnostics, semantic coloring and formatting through VeroScript LSP.
- Run/check tasks, VM debugging, program-mode host attachment and supported live-domain attachment.
- Separate language-service contexts for workspace folders, nested roots and standalone files.
- UI translations for English, French, Japanese, Korean, Simplified Chinese and Traditional Chinese. Editor colors follow the selected VS Code theme.

Basic syntax coloring and snippets work without a Runtime. Language services, execution and debugging use separately installed native tools.

## Quick start

1. Install this VSIX in desktop VS Code **1.106.0 or later**.
2. Extract a matching VeroScript Runtime for the extension host platform. Keep the directory complete, including CLI, LSP, DAP, `Vm/`, `stdlib/` and `localization/`.
3. Run **VeroScript: Select Runtime** from the Command Palette and select the directory containing those programs and resources.
4. Open a folder, save a `.vs` file and use **VeroScript: Check File**, **Run File** or **Debug File**. F5 uses the VeroScript debugger when selected.

```veroscript
void main()
{
	print("Hello, VeroScript!");
}
```

In WSL, SSH or container workspaces, install the Runtime inside that extension host. Keep Core, VM, JIT and native providers from a compatible build; equal version strings alone do not establish native ABI compatibility.

## Useful settings

```json
{
	"veroscript.importPaths": ["${workspaceFolder}/scripts"],
	"veroscript.run.backend": "vm",
	"veroscript.bracesOnNewLine": true
}
```

Use `veroscript.runtimePath` for the extracted Runtime directory. Compiler settings include `stdlibSearchPaths`, `importPaths`, `implicitImportPaths`, `defines`, `predefinedMacros` and `bindingCatalogPaths`. Relative paths use the owning workspace folder, or the source directory without a workspace. Tasks and debug configurations can override their supported options.

After replacing Runtime binaries, run **VeroScript: Restart Language Server**. **Show Output** and **Show Environment** help inspect the selected toolchain. If terminal tasks are restricted in an empty window, open the script directory as a workspace.

## Debugging and hosts

**Debug File** launches the native VM debugger. **Attach to Host** connects to a compatible TCP DAP host and loads a new script session. **Attach to Domain** connects to a host-prepared live domain that explicitly advertises this capability. The host owns its VM, execution and cancellation; domain attachment does not take over an arbitrary process or running JIT VM.

Host APIs need a compatible host and matching binding catalogs. Metadata enables editing hints; it does not register executable host callbacks in the local Runtime.

## GUI companion

Install **VeroScript GUI** alongside this extension for `.vswml` and `.vswcss` editing, plus native GUI check, compile and preview tasks. GUI execution additionally needs a matching GUI-enabled Runtime and its native providers.

The [documentation index](https://github.com/VanstySanic/VeroScript/blob/main/Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/README.md)
provides user and developer guides in all six languages.

## License

MIT. The complete license is included in `LICENSE.txt`; bundled JavaScript dependency notices are included in `THIRD-PARTY-NOTICES.txt`. The native Runtime is distributed separately with its own notices.
