# VeroScript GUI in VS Code

Install both **VeroScript** and **VeroScript GUI**, base first. Matching local builds must expose base tools API 1. This companion owns `.vswml` and `.vswcss`; `.vs`, LSP, DAP, Runtime selection and compiler settings remain with VeroScript. It follows the VS Code theme.

Editing provides widget, property, `bind:` and `on:` hints, enum values, resource paths, hover, outline, folding, tag highlights and six/eight-digit color editing. Original names and Mark aliases are case-sensitive. `bind:`/`on:` do not require `xmlns`. VSWCSS is the native subset, not general browser CSS. Structural messages are informational; the native compiler decides correctness. Inline script is colored, without a separate GUI LSP or semantic analysis of embedded expressions.

Built-in hints include ListView/ListItem, TreeView/TreeItem, TableView/TableColumn/TableRow, ComboBox, TabView/TabItem, Menu/MenuItem and Dialog, including inherited properties and events. SelectionColor / selection-color supports markup color editing; Popup Placement offers below, right and center. Execution requires matching rebuilt GUI providers and script libraries; keep native providers and consumers from matching builds. [GUI framework](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/en-US/Reference/Framework.md).

Use **VeroScript GUI: Check GUI**, **Compile GUI Application**, **Preview GUI**, **Select GUI Entry** and **Show GUI Output**. Preview launches the native application, not a webview. Configure a matching GUI/scripting-enabled Runtime with `veroscript.runtimePath` and install the native providers required by your application. Editing itself starts no native process.

Explicit GUI commands may compile the complete framework on a cold run and take over a minute. The task runs asynchronously; use VS Code's task controls to stop it when needed.

## Workspace settings

`veroscriptGui.entry` selects `.vs`/`.vswml` or a compiled `.vsapp` for check/preview. Empty uses the selected file; a style file uses its same-name `.vswml` sibling. Use an explicit entry for shared styles. `assets` embeds files during compilation, `packageName` names the result, and `schemaFiles` supplies extra editor hints. These arrays replace their defaults.

`backends` accepts `platform`, `graphics`, `gui`, `image`, `font` and `text-layout`. Omitted or empty extension values inherit the native environment; an explicit backend ID selects that backend, `auto` requests native candidate selection, and `none` disables that service. Disabling a required preview service reports a native error. `environmentPath` explicitly loads a CLI package environment with `--path`, after backend choices. It does not configure the base LSP or debugger. Run arguments/environment/cwd/compiler flags inherit base settings. Check/compile use VM without a JIT module; preview inherits the run backend. `previewFrames` is 0 for no supplied limit or a positive safe integer for `.vswml` only. `.vs` owns its loop.

The standard native Context factory falls back to `rmlui` only when the constructor argument, invocation and application/environment defaults supply no choice; explicit `auto` and `none` are preserved. An empty extension setting omits the override, whereas an explicitly empty backend value stored in the native environment disables the service. VM/JIT execution options remain with the base extension and are not GUI service roles.

Paths and processes belong to the workspace extension host, including remote sessions. Relative GUI settings/tasks resolve against the owning workspace folder, or selected file directory without a folder. Supported variables: `${workspaceFolder}`, `${file}`, `${fileDirname}`, `${env:NAME}`. File variables require a source file; workspace variables require a folder. Native document resources instead resolve beside their declaring file. Opaque resource IDs stay opaque.

## Tasks and metadata

Tasks use `type: "veroscript-gui"`, `action: "check" | "compile" | "preview"`, and `input`. Compile additionally needs `output` ending in `.vsapp`. Optional fields are `assets`, `name`, `frames`, `args`, `backends`, `environmentPath`; arrays replace inherited values. `args` applies to preview. Commands ask to save dirty participating sources; configured tasks follow VS Code's task save policy. Save dependencies outside configured source scopes separately. Native errors link file/line; UTF-8 byte columns are not displayed as UTF-16 positions.

Custom `*.veroscript-gui.json` files receive JSON schema validation. Add their paths to `schemaFiles`. Use `schemaVersion: 1`, `types` with `name`, optional `mark`, `bases`, `properties`, `events`; members need `name` and `type`, with optional `mark`, `bindable`, `style`, `values`, `description`. This metadata cannot register runtime bindings or processors. Ambiguous tag aliases are suppressed.

```json
{
	"schemaVersion": 1,
	"types": [
		{
			"name": "Demo::Card",
			"mark": "card",
			"bases": ["VS::Gui::Widget"],
			"properties": [
				{ "name": "Title", "mark": "title", "type": "string", "bindable": true }
			]
		}
	]
}
```

For GUI-aware `.vs` completion/debugging, configure a compatible host and binding catalog through the base extension. There is no automatic GUI processor startup, GUI debugger or visual designer. Files above 2 Mi UTF-16 code units stop local structural analysis.

Completion works in unfinished headers and CSS declarations at EOF, preserves whitespace before `=`/`:`, and replaces numeric dimensions without appending duplicate digits. Closed headers do not offer attributes, and class/ID selector names do not offer widget types. CSS URL links follow native case and escape rules. XML comments inside script/style bodies retain their structural scope.

GUI compilation requires native Application support and produces the standard `.vsapp` application container. The same artifact works with GUI check/preview and ordinary CLI inspect/check/run when the host supplies GUI support and the required providers. Recompile earlier raw GUI `.vsapp` artifacts. Backend choices remain explicit; the extension does not automatically select RmlUi.

Ordinary CLI `--check` can validate the generated `.vsapp` as a standalone container or use `--path` to select a matching package environment. GUI check uses `environmentPath` to select that environment. Use the repaired Runtime and a matching installed environment; after a Core/ABI update, regenerate environments targeting an earlier build.

Use a complete matching GUI-enabled Runtime, including its shipped `Vm/` module. GUI checking and compilation can execute script-based compile processors and need the Runtime VM as well as the selected GUI providers. Provider selection remains explicit; the editor metadata snapshot describes editable declarations rather than runtime notification implementations.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
