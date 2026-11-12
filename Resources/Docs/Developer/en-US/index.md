# Documentation maintenance

[Source code on GitHub](https://github.com/VanstySanic/VeroScript)

<a id="documentation-ownership"></a>

## Canonical sources and ownership

| Content | Maintained original |
| --- | --- |
| Common editor integration | [Editor integration maintenance](editors.md) |
| Shared reading paths, cross-module relationships, tutorials and project development and contribution standards | Root `Resources/Docs` |
| Module behavior, public API, defaults and limits; module architecture, builds and maintenance | Owning module’s `Resources/Docs/User/<locale>` and `Developer/<locale>` |
| Shared provider maintenance across multimedia modules | [Multimedia maintenance](multimedia.md) |
| Exported All/User trees and ZIP archives | Generated output; edit the referenced original and publish again |

Maintain one original for each topic and locale. Entry pages may summarize and link to the owner; complete contracts, procedures and changing defaults belong there. Update parallel originals in all six languages. Shared cross-module procedures belong here; protocol and SDK details belong to their module. Previous documentation revisions remain in version control.

`documentation.json` registers collection-wide languages, module manifests and supporting resources. Each module’s `manifest.json` maps its User originals to stable publication routes; Developer pages stay with that module. A source may be published once in the All collection. The User package projects the same User/Tutorials content from it. Packaged copies are not additional maintenance sources.

## Localization and paths

Keep page filenames and semantics aligned in all six languages, including Overview. Retain API names, diagnostic IDs, JSON keys, commands and runnable code. Previous acceptance results apply only to their recorded source, artifacts and environments.

Module manifests map User originals to stable publication paths. Rewrite included-page/resource links as relative URLs, preserving locales, anchors, encoded paths, queries, images and reference links; leave code and HTML comments intact. Source-only references open the online repository. Historical documentation is recovered through version control, not copied into publications.

For the source format of maintained documentation and localization files, follow [the shared file line-ending rules](../../Standards/en-US/GENERAL.md). These repository rules leave language and tool support for CRLF input unchanged. Publication generates normalized copies without rewriting maintained originals or binary resources.

## Validation and publication

Run the commands below from the repository root. Every publication rebuilds from current originals; editing a source page requires no synchronization step. Commit maintained originals only. Collection assembly rejects source changes made during generation and cleans its temporary directory on success or failure. CI checks locale parity, file inventories, exact publication bytes and links. Markdown/JSON publication text uses LF; original license notices, binary assets and author originals retain their bytes.

```powershell
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
python Resources/Tools/Packaging/veroscript_docs.py publish --audience All --output .tmp/docs-preview
python Resources/Tools/Packaging/veroscript_docs.py verify-publication --audience All --output .tmp/docs-preview
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -OutRoot .tmp/docs-collection-package
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -Audience User -OutRoot .tmp/docs-user-package
```

`build` and `publish` require a dedicated empty output subdirectory inside repository `.tmp` or `dist`. Direct output under the source `Release` or an existing managed collection is rejected; Resources publication goes through the transactional business publisher. Use a new preview directory on each run. For repeated standalone ZIP exports, use `package-veroscript-docs.ps1`, which stages and validates the new package before replacing the previous artifact.

<a id="integrated-publication-views"></a>

`package-veroscript-docs.ps1` defaults to All: current Overview, User, Developer, Standards, complete Tutorials and linked resources. `-Audience User` publishes references at `docs/<locale>/...`, complete tutorials at `docs/Tutorials/<locale>/...` and their dependencies. Neither audience includes History.

This script exports standalone documentation and does not publish to the managed product collection. Its default output is `dist/docs/<Audience>/<version>/`, or `dist/docs/Custom/<version>/` with `-SourceRoot`. Use `-OutRoot` for a dedicated alternative output directory. Full Resources releases assemble the User view through the product publisher.

Complete releases publish the User view through Resources at `Release/Resources/any/<version>/Resources/Docs/`, alongside `Resources/Scripts` and `Resources/Assets`. Current Runtime/SDK/extension archives and installers omit authored documentation and examples. Read [release-layout.md](../../../../VeroScriptInstaller/Resources/Docs/Developer/en-US/release-layout.md) in the Installer Developer guide for the payload policy.

[Project development and contribution standards](../../Standards/en-US/README.md) apply only to work on the VeroScript repository, not to SDK usage, and add no MIT license conditions. In `documentation.json`, `standardsAudience` must be `Developer`: Standards are included only in `All` as developer material, not in `User`, user documentation in Resources, Runtime/SDK/extension archives, or installers. Source copyright and license notices remain intact.

Project development and contribution standards are maintained in `Resources/Docs/Standards/<locale>`: `zh-CN` defines the rules and the other five languages remain equivalent. Publication reads these originals directly. This standalone repository has no IDE standards mirror. `sync-standards` updates a managed reference only when a ValenceStudio consumer is discovered by its documentation catalog; otherwise it creates no mirror.

Python 3.11+ is required. `check` validates sources and any discovered standards consumer; `check-collection` validates a temporary complete collection; `build/verify` preview User extraction; `publish/verify-publication` publish and verify All or User from current originals. Custom `-SourceRoot` exports the explicitly supplied tree and cannot be combined with `-Audience`. Missing or escaping links, missing anchors, empty trees, filesystem links and special entries are rejected; source and output cannot overlap. ZIP includes hidden resources and empty directories; failed replacement restores the previous artifact, retaining a recovery backup if rollback fails. Online source references require network access; HTML renderers must convert `.md` links and preserve heading IDs. For product builds, payload checks and runtime acceptance records, follow the [Release validation procedure](../../../../VeroScriptInstaller/Resources/Docs/Developer/en-US/release-validation.md).

## Module manifest

In the top-level `Resources/Docs/documentation.json`, the `$module/` prefix denotes the owning source module root. Keep product directory names such as `VeroScript` unchanged; module manifests still use paths relative to their own Docs directory. After moving or renaming the outer directory, update literal commands in authored documentation and actual paths in repository workflows. PowerShell scripts used with Windows PowerShell 5.1 must retain a UTF-8 BOM when they contain non-ASCII text.

```json
{
  "schemaVersion": 1,
  "module": "VeroScriptCli",
  "user": [
    {
      "source": "User/{locale}/index.md",
      "output": "{locale}/toolchain/cli.md"
    },
    {
      "source": "User/{locale}/packages.md",
      "output": "{locale}/toolchain/packages.md"
    }
  ],
  "developer": true
}
```

## Document purpose

User describes consumer tasks, public API behavior, configuration and limits. Developer describes architecture, implementation ownership, builds, tests and publication. Tutorials teach a task through prerequisites, steps and expected results. Standards contain only maintainer-approved rules for development of and contributions to this project. Previous plans and validation evidence belong in version control or temporary outputs and never certify a changed workspace.

Keep document bodies, titles and filenames free of calendar dates and nonessential personal identifiers. General explanations describe behavior without repeating the current release number. Retain exact versions where compatibility, tool requirements, ABI or format contracts, executable examples, artifact selection or original license notices require them. Link to maintained metadata instead of copying changing identifiers. Put dated logs and acceptance snapshots in ignored temporary outputs. User pages do not repeat test methodology or internal implementation details.

## Links and audience boundaries

Relative links in User pages must resolve inside the User view, Tutorials or registered supporting resources. A relative link from User to Developer or Standards prevents both All and User publication. Keep server implementation, maintenance and acceptance procedures in Developer pages, and describe the observable usage in User. Developer pages can link to the relevant User contract. Verify the source collection and both audience packages after changing cross-module links.

## Module references

| Area | References |
| --- | --- |
| Language and tools | [VeroScript](../../../../VeroScript/Resources/Docs/Developer/en-US/index.md) · [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/en-US/index.md) · [Tooling](../../../../VeroScriptTooling/Resources/Docs/Developer/en-US/index.md) |
| Host SDKs | [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/en-US/index.md) · [C++](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/en-US/index.md) · [Python](../../../../Extensions/Language/Python/Resources/Docs/Developer/en-US/index.md) · [C#](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/en-US/index.md) · [WebAssembly](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/en-US/index.md) |
| Execution engines | [Bytecode VM](../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/Developer/en-US/index.md) · [AsmJit](../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/en-US/index.md) |
| Packages and applications | [PackageManager](../../../../VeroScriptPackageManager/Resources/Docs/Developer/en-US/index.md) · [Application](../../../../VeroScriptApplication/Resources/Docs/Developer/en-US/index.md) · [Build](../../../../VeroScriptBuild/Resources/Docs/Developer/en-US/index.md) · [Installer](../../../../VeroScriptInstaller/Resources/Docs/Developer/en-US/index.md) |
| Editors | [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/en-US/index.md) · [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/en-US/index.md) · [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/en-US/index.md) · [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/en-US/index.md) |
| Embedded platforms | [Mobile](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/en-US/index.md) · [Android](../../../../Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Docs/Developer/en-US/index.md) · [iOS](../../../../Extensions/Platform/Mobile/VeroScriptIOS/Resources/Docs/Developer/en-US/index.md) |
| Data and networking | [JSON](../../../../Modules/Network/VeroScriptJson/Resources/Docs/Developer/en-US/index.md) · [Network](../../../../Modules/Network/VeroScriptNetwork/Resources/Docs/Developer/en-US/index.md) · [HTTP](../../../../Modules/Network/VeroScriptHttp/Resources/Docs/Developer/en-US/index.md) · [WebSocket](../../../../Modules/Network/VeroScriptWebSocket/Resources/Docs/Developer/en-US/index.md) · [Boost](../../../../Modules/Network/Backend/VeroScriptBoost/Resources/Docs/Developer/en-US/index.md) · [Curl](../../../../Modules/Network/Backend/VeroScriptCurl/Resources/Docs/Developer/en-US/index.md) |
| Multimedia and GUI | [Platform](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/en-US/index.md) · [Graphics](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/en-US/index.md) · [Audio](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/en-US/index.md) · [Image](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/en-US/index.md) · [Font](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/en-US/index.md) · [Gui](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/en-US/index.md) · [Sdl3](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/en-US/index.md) · [Stb](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/en-US/index.md) · [FontBackend](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/en-US/index.md) · [RmlUi](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/en-US/index.md) |

Use relative links between current same-repository guides. Online repository URLs must not bypass audience boundaries or turn included documentation into a network dependency; the source and publication checks reject them. Source-code references retain their existing behavior.

<a id="current-publication-policy"></a>

Both All and User publish current documentation only. History directories, old plans and validation snapshots are not publication inputs; retain previous revisions in version control and exact validation evidence in ignored temporary outputs. A source or custom SourceRoot containing a History path is rejected, including case variants. Publish Overview, User, Developer, Standards, complete Tutorials and their dependencies according to the selected audience.

<a id="module-entrypoints"></a>

A module-root README introduces its purpose, related modules, documentation and license links. Its Resources/Docs/README is the six-language audience selector used by collection publication. Detailed usage belongs in User, implementation/build/verification in Developer, and step-by-step lessons in Tutorials. Language-tool submodules link to their group’s guide originals. Keep these entries concise and update their relative links when moving files; do not maintain a second manual or acceptance record in a README.

<!-- docs-navigation -->

---

[Developer documentation](index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
