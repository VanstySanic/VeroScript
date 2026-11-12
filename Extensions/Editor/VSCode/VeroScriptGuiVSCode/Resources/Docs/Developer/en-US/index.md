# Developing VeroScript GUI for VS Code

## Architecture

Native GUI acceptance must use the matching Core, Tooling, VM and provider cohort; equal product versions do not establish ABI compatibility. Keep editor metadata validation separate from actual check, application-container compilation and bounded preview execution.

Artifact regression checks the generated `.vsapp` through provider-aware `gui-check`, standalone ordinary `--check` and ordinary `--check` with the selected source-project environment and compile arguments. This includes an explicit standard-library path together with `--path`, covering native standard-library root deduplication. The overall test deadline covers the native tasks and archive-validation commands; each command retains its own bounded deadline. Regenerate installed test environments when the native Core/ABI cohort changes.

Keep Runtime/LSP/DAP in the base extension. GUI code uses `Public/api.d.ts` API 1 through dependency activation, resolving a scoped toolchain only for explicit execution. The source URI resolves base run variables while the task's owning folder determines configuration. Editing and automatic task enumeration do not resolve or spawn native tools; configured-task resolution may query the CLI version. Native GUI production sources remain independent.

Markup/style models are cached by document version. Parsing is bounded; asynchronous metadata/path results are discarded after edits or cancellation. Custom schema caches/watchers are scoped by resolved configuration. Original/Mark tags stay case-sensitive, ambiguous aliases are omitted, and inherited metadata is traversed once per index. Do not turn compiler hints into semantic authority.

## Build and acceptance

Use Node.js 22.18+ and desktop VS Code 1.106+. Run each block from its named module. Install both modules' dependencies: shared public declarations refer to the base VS Code SDK types. Integration development launches both extensions; packaged tests install both local VSIX files in an isolated profile. The test CLI must not fetch the unpublished dependency from Marketplace.

```sh
# VeroScriptVSCode
npm ci
npm run typecheck
npm run package
# VeroScriptGuiVSCode
npm ci
npm run typecheck
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
node Resources/Tools/test.mjs --packaged --skip-build
```

Run desktop Extension Host suites one at a time. Simultaneous VS Code test windows can compete for the editor focus used by completion acceptance, undo and Enter cases.

Native GUI task acceptance allows 600 seconds per check, compile or preview task and adjusts only that test's overall deadline to cover its task sequence. Editor-provider waits keep their shorter deadlines. Explicit GUI commands can cold-compile the full framework, so a successful task may take over a minute. Production tasks remain asynchronous and can be stopped through VS Code; a nonzero exit, missing output or exceeded test budget still fails acceptance.

Unit cases cover structural recovery, UTF-16 offsets, CSS boundaries, native metadata, ambiguity/cycles, CLI ordering and TextMate scopes. Extension Host cases cover dependency activation, languages, completions, hover, links, highlights, diagnostics and dormant native services. Set `VEROSCRIPT_TEST_RUNTIME` to the matching GUI-enabled Runtime, then `VEROSCRIPT_GUI_TEST_NATIVE=1` to require real check/compile acceptance; add `VEROSCRIPT_GUI_TEST_PREVIEW=1` only in a desktop environment with compatible native providers to require a two-frame markup preview. Verify Windows, Linux and macOS separately, including spaces/non-ASCII paths, folderless and multiple-root workspaces, remote extension hosts and a real preview window. Optional tests being skipped is not native acceptance.

`VEROSCRIPT_TEST_DIRECTORY` optionally selects an absolute parent for isolated test workspaces and profiles, as in the base extension. For native package-environment acceptance, place it inside the project selected by `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH` so generated GUI sources belong to that environment. An unrelated temporary workspace can be rejected by native source ownership checks; do not widen compiler access to bypass this fixture mismatch.

Record the tested Runtime source/release snapshot for every native acceptance run; old reports are not evidence for new native code. Native CLI timeouts fail acceptance. Do not automatically run preview on edits.

## Editor metadata

Save the following as `demo.veroscript-gui.json` and list it in `veroscriptGui.schemaFiles`. It supplies editor hints only. The host/compiler must independently register the actual type and processors. Later schemas replace types with the same canonical name; they must provide that type's complete own metadata. Bases are canonical names. `routedEvents` optionally adds common event hints.

```json
{
	"schemaVersion": 1,
	"types": [{
		"name": "Demo::Card",
		"mark": "card",
		"bases": ["VS::Gui::Widget"],
		"properties": [{ "name": "Title", "mark": "title", "type": "string", "bindable": true }],
		"events": [{ "name": "Selected", "mark": "selected", "type": "std::multicast_delegate<void()>" }]
	}]
}
```

Bundled metadata follows local exported imports from the native `VeroScriptGui.vs` entry; all inputs are listed in `builtin.json`. Annotated widgets without a `Mark` alias are retained so composite-control inheritance remains complete. `npm run schema:update` refreshes the snapshot; review native literals and style/bindability flags. Normal builds require a matching snapshot and reject duplicate types or missing bases. Unit and Extension Host regressions cover composite tags, inherited properties/events, placement values, CSS hints and selection colors. Metadata validation does not validate native execution. Qualified `VS::Gui::Internal` ancestors are retained for inherited editor hints after native namespace refactoring.

Metadata extraction accepts language whitespace between `@` and annotation names, including `@GuiWidget` and `@ GuiWidget`, with the same property/event and inheritance results. After source formatting changes, rerun `npm run schema:check`. Use `npm run schema:update` when the extracted metadata changes; `builtin.json` records source paths and metadata, not source-content hashes.

## Distribution

Package a functional stage containing bundle, manifests, localized resources, grammars, schemas, icons and MIT notices; no native binaries, build tools, tests or developer docs. GUI runtime JavaScript currently has no third-party dependency; adding one requires notices changes. The project LICENSE must match the repository. Keep all six UI and documentation locales aligned. Standalone GUI VSIX packaging remains available; the aggregate IDE packager registers GuiVSCode independently, builds its base dependency, and runs packaged acceptance with both local VSIX files.

Static regression review covers EOF/header boundaries, numeric value ranges, spaced assignments, closing-quote duplicate suppression, XML comments in embedded bodies and empty outlines. Native CSS URLs are consumed once during the cached style parse, including uppercase names and escapes; link requests reuse those ranges rather than rescan every opaque span. Preparing a configured task can query the CLI version; editing and automatic task discovery do not start GUI commands. Native acceptance fetches configured tasks through VS Code's registered provider so packaged tests exercise the installed extension. Set optional `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH` to an absolute prepared package environment when the providers require it. Regression coverage also includes attribute completion for custom tags named after JavaScript prototype members. The isolated test profile disables word suggestions so they cannot mask plugin results, and uses a supported editor close command. Native acceptance requires a matching Runtime and successful check, compilation and preview.

See [Editor integration maintenance](../../../../../../../../Resources/Docs/Developer/en-US/editors.md#acceptance-evidence).

Use the publisher declared in `package.json`; the GUI dependency must match the base extension identity. Ship the root README as the extension Details page, without links to excluded repository files. Only `extension/readme.md` is exempt from the functional package documentation filter. Marketplace publication requires access to that registered publisher and completed acceptance gates; a manifest change does not register the publisher.


[en-US](../../User/en-US/index.md) · [Documentation](../../README.md)
