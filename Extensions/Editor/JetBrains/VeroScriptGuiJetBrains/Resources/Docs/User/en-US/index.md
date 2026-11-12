# VeroScript GUI in Rider

## Install and configure

Install **VeroScript** first, then a compatible **VeroScript GUI** plugin. Use **Settings → Plugins → Install Plugin from Disk** and restart when requested. Rider requirements and Runtime selection follow the base plugin guide below.

Configure Runtime in **Languages & Frameworks → VeroScript** using the
[base plugin guide](../../../../../VeroScriptJetBrains/Resources/Docs/User/en-US/index.md).
GUI tasks require the matching GUI-enabled CLI, GUI script libraries and native
providers. Installing the plugin does not install them. Without Runtime, local
markup/style highlighting and built-in suggestions remain available.

## Edit markup and styles

Built-in suggestions include `ListView`, `TreeView`, `TableView`, `ComboBox`,
`TabView`, `Menu` and `Dialog`, with inherited selection/popup properties, custom
events and style properties. Configure item data and virtualization through the
GUI script API. Use a matching GUI Runtime that contains these controls.

`.vswml` uses native GUI syntax; `.vswcss` is the GUI style language. These are
not HTML or unrestricted CSS. `bind:` and `on:` need no `xmlns` declaration.
Completion offers built-in controls, inherited properties, getter bindings,
events, style properties and supported pseudo states. It respects case and
Rider's first-item selection settings. Directive `src` values support soft
filesystem navigation and path completion. Relative paths use the declaring file;
paths recognized as absolute by the running OS use its filesystem root.
Separators follow that OS; literal filename characters such as `?` remain intact.
Opaque host resource identities and paths with SDK-trimmed surrounding whitespace or entity escapes (`&...;`)
are not treated as local paths. References for plain paths participate in Rider
file/folder renames; inserted names are escaped for markup.

Accept an attribute with `=` or a style property with `:` to enter its value
without duplicating existing punctuation. Existing values are preserved. Tab
replaces the complete name, including hyphenated names and `bind:`/`on:` prefixes.

Inline `<script>` and `<style>` bodies have lexical highlighting, including
CDATA. For normal VS semantic editing, place code in an external `.vs` file and
reference it through `<script src="logic.vs" type="App::Logic" />`. External
scripts keep the base plugin's LSP features. GUI API resolution and debugging
depend on the configured native tools and host; the add-on does not generate
executable bindings for LSP/DAP. Inline script/binding semantics,
custom widget schemas, formatting and automatic compiler diagnostics are not
implemented by this add-on. Run **Check GUI** for native validation.

Example `page.vswml`:

```xml
<Stack Width="100%" Height="100%">
	<style src="page.vswcss" />
	<Text Text="Hello, VeroScript GUI!" />
</Stack>
```

Example `page.vswcss`:

```css
:root {
    padding: 16px;
    gap: 12px;
    color: #edf4fc;
    background-color: #101927;
    font-size: 18px;
}
```

See the native [framework guide](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/en-US/Reference/Framework.md)
and [Showcase](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/Showcase/page.vswml).
Unknown custom controls are not marked invalid merely because they are absent
from the built-in completion catalog.

## Check, preview and bake

Right-click a local `.vswml`, `.vs` or `.vsapp` entry and open **VeroScript GUI**.
`.vswcss` is checked through its owning markup entry, not executed directly.

| Task | Behavior |
| --- | --- |
| Check GUI | Calls `gui-check`; checks/compiles without calling the application entry or opening a preview window. |
| Preview GUI | Calls `gui-preview`; executes the entry in a separate native window. Stop from Rider or close the window. |
| Configure GUI Application Baking | Opens a compile configuration. Review `.vsapp` output and assets, then use Run to call `gui-compile`. |

Checking or baking source can execute script-based compile processors. Use a complete matching Runtime with its VM and GUI script libraries; no execution JIT provider is required for these tasks.

The run selector can create a **VeroScript GUI** configuration. Context-produced
configurations are offered for `.vswml`; ordinary `.vs` Run/Debug stays with the
base plugin. GUI tasks currently provide **Run**, not a GUI-aware DAP launch.
For a host-prepared debug domain, use the base plugin's documented attach flow.
Preview is explicit; there is no editor-embedded designer or automatic reload.

## Configuration details

- Context actions inherit the GUI configuration template and clear options that
  do not apply to the chosen task/entry. An applicable custom baking output is retained.
- Runtime, language and project compiler options are inherited. Run-specific
  compiler JSON does not change editing settings. Inherited paths keep their
  project origin; relative run overrides use the working directory.
- Context entries use `$PROJECT_DIR$`. Leave working directory blank for the
  project root. Relative file references inside ML/CSS use their declaring source.
- **Package environment** selects `--path`. Blank backend fields omit the
  override and inherit Runtime/package defaults. `auto` delegates provider
  selection to the native host; `none` disables the service. Disabling a required
  preview service reports an error. An explicitly empty backend value stored in
  the native environment means `none`, unlike a blank field here. Required
  providers must still be installed or supplied by the GUI-enabled Runtime.
- The standard `Context` factory falls back to `rmlui` only when the constructor,
  invocation and factory defaults supply no GUI choice. Explicit `auto` and
  `none` are preserved. These six backend fields cover GUI services; VM/JIT
  execution options are separate.
- Check and Bake use VM execution; Preview retains native execution defaults.
- Stored entry paths normalize only native separators; backslashes in POSIX filenames stay literal.
- Preview arguments follow `--`. The optional positive `int64` frame limit is
  only for `.vswml`; blank or `0` means unlimited. Scripts and `.vsapp` artifacts
  own their execution loops.
- Baking takes `.vswml` or `.vs`, an output ending in `.vsapp`, an optional name
  and an asset JSON array such as `["assets/logo.png"]`. The output may not
  overwrite the entry or a listed asset. The default configuration output is
  under `.tmp/gui/`; change it for a persistent artifact.
- Environment is a JSON object of string values. Parent environment inheritance
  is configurable. Native source errors provide line links when the file exists
  locally. Unsupported Runtime commands and provider failures stay visible in
  the console.

<!-- docs-navigation -->

---

[User documentation](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
