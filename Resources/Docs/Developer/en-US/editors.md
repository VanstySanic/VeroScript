# Editor integration maintenance

This guide owns the common boundary between editor clients and native execution/protocol hosts. Client implementation and resource formats remain in the module guides below.

Keep editor clients responsible for configuration, transport and IDE presentation; compiler semantics, LSP and DAP contracts belong to native Tooling/Core. Native binding catalogs are metadata, never executable bindings. Domain tests use protocol fixtures; passing them does not validate actual VM safe points or an application/UE host. Install the Session bridge before execution; the host serializes its single active debug context and owns cancellation. Core `jitCapability` is a service contract; normal Run may pass an explicit compatible module path or inherit CLI module-environment selection (unset defaults to auto). LSP/Check/local VM debug do not load JIT.

Maintain all six UI/documentation cultures together; preserve command options, message IDs and protocol values. Validate placeholders using the client’s actual formatter. User guides cover operations; Developer guides cover client architecture, build and release. Check claimed SDK, Runtime and platform compatibility against the selected versions and actual acceptance evidence.

## Client-specific implementation

- [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/en-US/index.md)
- [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/en-US/index.md)
- [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/en-US/index.md)
- [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/en-US/index.md)

## Shared native contracts

- [Tooling / LSP / DAP](../../../../VeroScriptTooling/Resources/Docs/Developer/en-US/workspace-and-debug.md)
- [C++ Session / DAP](../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/session-debugging.md)

<a id="acceptance-evidence"></a>

For each acceptance run, record OS/architecture, editor and Runtime versions, full source identity, package hashes, actual results and skipped cases. Native execution needs its own matching Runtime acceptance. Historical results apply only to their recorded artifacts and environment.

<a id="editor-icon-inputs"></a>

## Shared icon inputs

The maintained outline and its output families are defined by `Resources/Tools/Assets/generate-icons.cjs`. Product artwork uses `#60A5FA`, file marks use `#5D75AE`, and editor glyphs retain their theme colors and native alignment. Full regeneration updates product/file SVG, PNG, ICO and ICNS resources and editor SVGs. `--brand-only` updates the product family. Follow the [asset regeneration procedure](../../../Assets/icons/README.md#regeneration) for Node.js, `sharp` and `--sharp-module`. Runtime packaging consumes the maintained files.

VS Code display PNGs are refreshed from the product PNG by each extension build. JetBrains V-bearing SVGs come from the shared generator, and Gradle places plugin-list icons in `META-INF` while preserving other icon resources under `Icons`. Keep asset paths, theme suffixes and manifest references stable across UI languages. Client-specific copy and packaging details belong to the module guides.

VS Code packages the module-root README as its extension Details page, while JetBrains uses the plugin descriptor’s description and vendor fields. Keep product presentation concise and link to the six-language documentation selector. Public manuals and maintenance procedures retain their existing originals and publication roles.

<!-- docs-navigation -->

---

[Documentation maintenance](index.md)

[en-US](editors.md) · [fr-FR](../fr-FR/editors.md) · [ja-JP](../ja-JP/editors.md) · [ko-KR](../ko-KR/editors.md) · [zh-CN](../zh-CN/editors.md) · [zh-TW](../zh-TW/editors.md)
