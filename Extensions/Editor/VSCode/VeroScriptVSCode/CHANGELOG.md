# Changelog

## 0.1.1

- Ship a self-contained README in the VS Code Details view and use the publisher declared by the extension manifest.

- Reuse the shared product icon in extension listings and refresh the packaged PNG during builds.

- Verify static generic, handle and nested-type receiver hover/completion against the current native tools; document matching ABI cohorts, relocated fixture VM selection and host-only JIT diagnostics.

- Keep native argument forwarding regression independent of unused host macros that trigger full GUI cold compilation.
- Document the extracted VM Runtime layout and cover inferred function captures and live receiver hover through actual SDK requests.
- Normalize JIT fixture paths and stop unfinished JIT test tasks on failure.
- Bound native task regression waits and terminate unfinished test tasks on failure.
- Normalize the expected Runtime source path in navigation regression tests when the Runtime is reached through a directory junction.
- Expose scoped tools API 1 for the optional GUI companion, sharing Runtime/compiler/run configuration without duplicating LSP or DAP.

- Add regression coverage for native readonly variant inspection and domain variable presentation hints, plus contextual exact numeric type coloring.

- Verify native inherited-member snippets, rewrite edits and transitive module-source navigation against rebuilt CLI/LSP/DAP tools and the current module-environment fixture.

- Bound repeated directive scanning in long continued macros, retaining template suppression and ordinary Enter behavior across physical-line hashes.

- Include explicitly configured implicit-import buffers in pre-run/debug save checks using host path comparison rules, including Windows case differences.

- Suppress built-in templates throughout continued preprocessor directives and line comments; respect block comments opened inside directives, preserve comment coloring and ordinary Enter behavior, and restore editing context afterward.

- Unify primitive/container type hover headings and built-in metadata presentation across hover and completion, preserving source aliases, signatures and documentation.

- Add a compact VeroScript file icon for light and dark themes, respecting the selected file icon theme.
- Combine localized built-in type hover headings; lay out empty block braces on Enter with native indentation, undo and multiple cursors, without a Runtime.

- Verify live annotation name-policy updates, native multiline/generic signature parameters and precise organize-import edits against updated language tools.
- Complete live-domain disconnect after host termination or transport closure; settle pending requests once and preserve final disconnect responses.
- Open advertised `veroscript-module:` sources as readonly snapshots scoped to their server lifetime and compile fingerprint; preserve module ownership for navigation, symbols and diagnostics, and invalidate retired sources across automatic restarts.
- Follow native multiline raw-string and lowercase wide-string lexing in TextMate coloring and template filtering.
- Verify current CLI/LSP/DAP compatibility and case-insensitive `.vslib` file notifications with current native tools.
- Preserve host sequence IDs in domain reverse-request replies, including out-of-order success/failure responses.
- Coalesce editor-event bursts into current-state LSP reconciliation while retaining explicit resets and events arriving during a pass; decode synchronization options once per client pass.
- Verify `std::memory` editing, run/check and VM debugging against current native tools; preserve decimal-string memory metadata through live-domain attachment.
- Verify standard namespace navigation with the default Runtime stdlib; packaged documentation links no longer require a repository checkout.
- Preserve synchronized document buffers when opening or closing other files, including unsaved cyclic imports.
- Explicit independent JIT module selection for normal runs and tasks; compile-only checks and VM debugging do not load run JIT modules.
- TextMate highlighting recognizes native `handle<T>` and `shared<T>` aliases and nested type arguments, including in hover code blocks.
- Built-in snippet suggestions respect lexical context and typed prefixes, avoiding import/declaration templates in expressions, comments and strings.
- Native Markdown hover titles align with signatures and notes; symbol names retain monospace formatting.
- TextMate highlighting recognizes qualified names, container type arguments and variables in hover signatures and source files; colors follow the active theme.
- Drain in-flight document-close notifications before client disposal, preventing spurious server-start errors when closing scripts.

- Domain capability handling covers pause, stepping, evaluation, conditional breakpoints and variable editing; host cancellation remains host-owned.
- Domain attachment without a local Runtime or entry file, guarded by host capability negotiation; host identity/revisions, variable editing capabilities and nonterminating disconnect follow the live-domain contract.
- Host attachment command, validated connection inputs and reusable attach templates; external hosts retain ownership of omitted compile settings.
- Accepted namespace completions continue with member suggestions; VeroScript defaults disable unrelated word-based suggestions while preserving native filtering and sorting.
- Hover signatures use VeroScript code blocks and separate overloads; resolved completion details avoid duplicate signatures and retain documentation.
- Initial VS Code extension using an external VeroScript Runtime.
- Basic editing and localized commands; LSP integration with separate workspace and standalone contexts.
- Unsaved configured imports synchronize across contexts and recover after settings changes or server restarts.
- Automatic language server restarts retain the latest workspace configuration.
- Synchronization registrations release removed imports; ordinary LSP requests preserve editor providers.
- Process tasks for running and checking scripts, with explicit compile options and saved-source checks.
- Native VM DAP launch and external TCP attach.
- Reproducible build, integration tests and local VSIX packaging.
