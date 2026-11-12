# Developing the Rider GUI add-on

## Ownership and dependency

The [plugin descriptor](../../../META-INF/plugin.xml) owns the plugin identity and required base-plugin dependency. The base VeroScript project supplies Runtime resolution, compiler settings and VS lexical highlighting; its classes are not repackaged. The [Gradle localPlugin project dependency](https://plugins.jetbrains.com/docs/intellij/tools-intellij-platform-gradle-plugin-dependencies-extension.html#local-plugin) supplies development, test and IDE sandbox integration. The two installation ZIPs remain separate.

Use JDK/JVM 25, Gradle 9.5.0, Kotlin 2.4.0 and IntelliJ Platform Gradle plugin
2.19.0. SDK: Rider 2026.2.2, minimum build `262.10315.191`, strict upper branch
`262.*`. Other branches require their own verification. No native executable or
provider is bundled. The root MIT license is included as `META-INF/LICENSE`;
Gradle wrapper notices are in `Resources/Licenses`.

## Design and contracts

`gui-compile` produces the standard `.vsapp` application container. Check and Preview
pass it to the native CLI, which reads the container and Core bytecode.
Use the matching Runtime, VM libraries and GUI providers together.

- `language/`: independent case-sensitive markup/style lexers and flat PSI;
  no XML namespace inspections or browser CSS semantics. Layered highlighting
  reuses the base VS lexer for inline script and the GUI lexer for inline style.
- `GuiMarkupModel`: per-file cached structural data for completion, source
  references and documentation. It does not type-check. No recursive workspace
  scan, automatic native check or preview is started while editing. Typed
  conveniences are skipped above 1 MiB and for multiple carets.
- `Resources/Catalog/gui.json`: built-in suggestions from GUI source metadata.
  Regenerate with `python Resources/Tools/update-catalog.py`; review its diff.
  Source hashes expose drift; this catalog is not a replacement for `FGuiSchema`.
  Unknown custom schemas are validated by the native tool, never rejected from
  this finite list. Preserve Original/Mark spellings and inherited bindings/events.
  Extraction follows local imports from `VeroScriptGui.vs`, including transitive
  support files. Omitted Mark values use declaration names. Class bounds and
  masked comments/literals keep each declaration's metadata independent.
  Catalog schema 2 preserves each declaration's `qualifiedName`, including
  `VS::Gui::Internal` scopes. Lookup and widget documentation use that name;
  leaf names and Mark aliases remain available. Generate and package the catalog
  together with its consumer.
- `run/`: inherit base Runtime/locale/compiler settings, then resolve each path
  against its origin. CLI order is global compiler/environment options including
  `--path`, GUI backend options, `gui-*` task, task options, then `--` and preview
  script arguments. Preserve repeated `--asset` and `int64` frame bounds.
- Check/compile do not run script initialization. Preview owns one native process;
  Rider's process handler supplies stop/termination. Backend IDs are optional.
  There is no GUI-aware DAP launch adapter. Existing prepared-host attachment is
  the base plugin's responsibility. File/line console links do not reinterpret
  native ML code-point columns as UTF-16 offsets.

Metadata extraction accepts language whitespace between `@` and annotation names, including `@GuiWidget` and `@ GuiWidget`, with the same property/event and inheritance results. Source-hash changes still require refreshing and reviewing the catalog even when its exposed types are unchanged.

## Verification and release

Editor invariants: each lexer owns its language tokens and respects bounded scan
ranges, including partial closing markers. Inline bodies stop at the next raw/CDATA
boundary without repeatedly searching the remaining file. A source-read-count
regression case guards linear scanning independently of machine timing.
Completion consumes `=`/`:` commits,
preserves existing values and replaces the entire name on Tab. Plain local `src`
references use `GuiStringLiteralManipulator` for rename/path edits; escape inserted
markup and preserve unfinished quotes. Entity-escaped paths remain outside local
navigation. Native-absolute paths resolve from the OS root and rebind with the
complete native prefix, instead of SDK project-root semantics. Relative paths
retain their declaring-file context. SDK project-root path conversion is disabled.
Normalize native separators only, preserving literal POSIX backslashes. Reference
decoding retains filename `?` characters and the SDK's macOS NFC normalization.
Relative rebinding keeps SDK behavior: when no relative path can be formed, the
reference text stays unchanged. Moves across volumes/filesystems require separate
review and acceptance; they are not claimed as automatic migration support.
Documentation targets reject edited snapshots; style documentation
is restricted to property tokens. Context actions clear incompatible task/entry
template options and preserve applicable custom outputs. The static checker
examines owned inputs rather than Gradle SDK caches or sandboxes.

From this module directory:

```sh
python -B Resources/Tests/test_catalog.py
python -B Resources/Tools/check-static.py
./gradlew :test :verifyPluginProjectConfiguration :verifyPluginStructure :buildPlugin
./gradlew :veroscript:buildPlugin
./gradlew :verifyPlugin
```

Windows: `gradlew.bat`; POSIX: `bash ./gradlew` if needed. An installed SDK can
be selected with `-PlocalIdePath=/absolute/path/to/Rider`. Root task paths prevent
unintentional selection of all sibling test tasks. Outputs are separate
`VeroScriptGuiJetBrains-0.1.1.zip` and base-plugin ZIPs. The aggregate IDE packager
registers GuiJetBrains independently, builds its base plugin, and verifies both
local ZIPs together against their own versions. The commands above remain
available for standalone development builds.

When the base plugin is unpublished, `:verifyPlugin` alone cannot resolve its
required dependency from Marketplace. Validate both ZIPs together with the official
Plugin Verifier CLI: put their absolute paths on separate lines in `plugins.txt`,
then run the command below. This supplies the local base plugin for dependency
resolution; validate both reported results before publishing.

```sh
java -jar /absolute/path/to/verifier-cli.jar check-plugin @/absolute/path/to/plugins.txt /absolute/path/to/Rider
```

Tests cover lexer restarts and incomplete edits, catalog inheritance/style
restrictions, portable path origins, frame/entry validation, CLI option order,
argument separation, assets, overwrites, persistence, registration and six bundles.
SDK fixture tests do not replace actual Rider acceptance. Test real GUI-enabled
Runtime Check/Compile/Preview for ML/VS/artifacts, relocation, installed providers,
spaces/non-ASCII paths, cancellation/Stop and native error links on each target OS.
Confirm inline highlighting, completion/first selection, quotes/tags, `src`
navigation and normal `.vs` behavior with both plugins installed.

See [Editor integration maintenance](../../../../../../../../Resources/Docs/Developer/en-US/editors.md#acceptance-evidence).

Editor fixtures initialize normal Rider folder solutions through a test-only
bundled SDK module and use real local files. ReSharper is disabled only in the
test JVM; the SDK settings-damage guard uses Rider's initialized defaults.

Interactive Rider UI, actual-window Preview/Stop, relocation, other operating
systems and real host integration still require separate acceptance. These
results apply to the tested environment and do not establish those remaining cases.

Maintain six UI bundles and six User/Developer locales together. The module
manifest is registered in the shared catalog; run documentation publication
checks when updating documentation.

[User guide](../../User/en-US/index.md) ·
[Standards](../../../../../../../../Resources/Docs/Standards/en-US/GENERAL.md)

## Check/bake invocation and saved paths

`GuiCommand.arguments()` prepends `--backend=vm` for `check` and `compile`, before `--path` can prepare a package environment, so execution JIT selection is suppressed for those tasks. Preview emits neither an execution-backend override nor a JIT override and keeps native defaults. GUI backend fields remain separate service selections. `GuiCommand.persist()` converts only `java.io.File.separatorChar` to `/` in project-relative stored paths; preserve literal backslashes in POSIX filenames. Keep regressions for option order, omitted/auto/none/custom providers after settings persistence, and native filename round-trips. Verify path behavior on each target OS; Windows results do not validate POSIX behavior.

[Rider SDK](../../../../../VeroScriptJetBrains/Resources/Docs/Developer/en-US/index.md#rider-platform-sdk)

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
