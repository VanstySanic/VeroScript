# VeroScript GUI for Visual Studio Code

The GUI companion for VeroScript. Install **VeroScript** first.

## Features

- Editing support for `.vswml` markup and `.vswcss` styles.
- Widget, property, event, binding and enum hints from native GUI metadata.
- Hover, outline, folding, matching tags, file links and six/eight-digit color editing.
- Explicit native GUI check, application compilation and preview tasks.
- UI translations for English, French, Japanese, Korean, Simplified Chinese and Traditional Chinese. Colors follow the selected VS Code theme.

The base extension continues to own `.vs`, LSP, DAP and Runtime selection. GUI editing starts no native process. Structural hints are informational; native compilation determines correctness.

## Quick start

1. Install the matching base and GUI VSIX files in desktop VS Code **1.106.0 or later**, base first.
2. Open a `.vswml` file to use editing hints. A minimal page is:

```xml
<Stack>
	<Text Text="Hello, VeroScript GUI!" />
</Stack>
```

3. For execution, select a complete matching GUI-enabled VeroScript Runtime with **VeroScript: Select Runtime**. Keep its `Vm/` module and native GUI providers available.
4. Use **VeroScript GUI: Check GUI**, **Compile GUI Application** or **Preview GUI** from the Command Palette. Compilation produces a standard `.vsapp` application container.

Preview launches a native application. A cold command can compile the complete GUI framework and take over a minute; use VS Code task controls to stop it if needed.

## Project configuration

```json
{
	"veroscriptGui.entry": "ui/main.vswml",
	"veroscriptGui.environmentPath": "${workspaceFolder}",
	"veroscriptGui.previewFrames": 2
}
```

`environmentPath` selects a prepared native package environment. Omit it when no explicit environment is needed. After changing Core/ABI builds, regenerate environments with matching native providers.

- `entry`: `.vs`/`.vswml` source, or `.vsapp` for check/preview. An active style file defaults to its same-name `.vswml` sibling.
- `assets`: files embedded during application compilation.
- `packageName`: generated application name.
- `schemaFiles`: custom `*.veroscript-gui.json` editing metadata; metadata does not register runtime bindings.
- `backends`: explicit `platform`, `graphics`, `gui`, `image`, `font` and `text-layout` selections. Empty settings inherit the native environment; `auto` selects a native candidate and `none` disables that service.
- `previewFrames`: `0` supplies no limit; positive frame limits apply to `.vswml` only. `.vs` and compiled applications own their execution loop.

Relative settings use the owning workspace folder, or the selected file directory without a workspace. Run/compiler settings inherit the base extension. Check/compile use VM; preview inherits the run backend. GUI package environment selection does not configure the base LSP or debugger.

## Tasks and scope

Tasks use `type: "veroscript-gui"`, `action: "check" | "compile" | "preview"` and `input`. Compilation also needs an `output` ending in `.vsapp`. The commands offer to save participating dirty sources; configured tasks follow VS Code's task save policy.

VSWCSS is the native GUI style subset. Embedded script is colored; this companion provides no separate GUI LSP, visual designer or debugger. Host-aware `.vs` semantics and debugging use the base extension with compatible host services and binding catalogs.

The [documentation index](https://github.com/VanstySanic/VeroScript/blob/main/Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/README.md)
provides user and developer guides in all six languages.

## License

MIT. The complete license is included in `LICENSE.txt`; `THIRD-PARTY-NOTICES.txt` describes the extension bundle. The native Runtime and GUI components are distributed separately with their own notices.
