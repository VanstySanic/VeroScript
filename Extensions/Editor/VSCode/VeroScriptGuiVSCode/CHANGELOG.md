# Changelog

## 0.1.1

- Ship a self-contained README in the VS Code Details view and follow the base extension's declared publisher identity.

- Reuse the shared product icon in extension listings and refresh the packaged PNG during builds.

- Validate GUI artifacts through their provider-aware GUI command, standalone application checking and ordinary checking with the selected source environment; cover explicit standard-library paths with `--path` after the native root-deduplication fix.

- Clarify matching native ABI cohorts for Core, VM and GUI provider acceptance after native updates, and standalone application checks in all six user guides.

- Allow an absolute test directory within the prepared native package project so GUI acceptance sources retain the correct environment ownership.
- Clarify native Context fallback while preserving omitted/auto/none selections, and verify that VM/JIT options are not GUI service roles.
- Preserve qualified internal GUI ancestors after namespace refactoring; document and verify inherited, automatic and disabled native service selections.
- Follow exported native GUI declarations for collection, navigation, menu and dialog hints, retaining unmarked ancestors; add popup placement values and markup selection-color editing.
- Clarify the complete Runtime VM dependency for native GUI tasks while retaining explicit provider selection.
- Follow the native standard `.vsapp` application container in GUI tasks; verify GUI outputs through ordinary CLI inspection and checking.
- Give VSWML and VSWCSS distinct light/dark file icons aligned with the base VeroScript icon.
- Give native GUI task acceptance a bounded cold-compilation budget while keeping editor waits short; document asynchronous task execution and the text-layout backend in all six user guides.
- Isolate editor regression tests from VS Code word suggestions, close editors with a supported command and terminate unfinished native test tasks on failure.
- Keep custom tag names such as `constructor` and `toString` from being mistaken for built-in directive entries during attribute completion.
- Fix completion boundaries at closed tags and unfinished CSS EOF, numeric dimension replacement, spaced assignments and duplicate attributes after closing quotes.
- Parse native CSS URLs once with case/escape handling; retain XML comments in embedded bodies and omit empty outline entries.
- Route native VSIX acceptance through the installed task provider, with an optional explicit test package environment.

- Add a separate VeroScript GUI companion extension depending on the base VeroScript extension.
- Add VSWML/VSWCSS editing, bounded structural hints, native declaration metadata and custom schema hints.
- Add explicit native check/compile/preview tasks using the base extension's scoped tools API 1.
- Add six UI/documentation locales and standalone build, package and regression test entry points.
- Verify editing in a real Windows Extension Host and local VSIX packages; require separate successful native GUI acceptance before release.
