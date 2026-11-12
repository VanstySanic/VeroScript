# Tooling testing and extension rules

This page records the compatibility rules for extending the tooling layer.

## Feature and protocol coverage

Every public feature needs a result-level test. A feature that crosses an LSP
or DAP boundary also needs serialization and transport coverage. The current
LSP test surface includes framing, JSON-RPC correlation, malformed input,
initialization, lifecycle notifications, incremental synchronization, document
versions, cancellation, diagnostics, configuration, completion, generated
override and interface-member implementations, hover,
navigation, symbols, semantic tokens, signature help, references, highlights,
rename, formatting, workspace symbols, shutdown, exit, and unsupported-method
errors. Completion coverage includes textual-match ranking, `sortText`, and
the conservative `preselect` rule. Semantic-token coverage includes lexer
fallback spans, incomplete-buffer recovery, primitive type words as `keyword`,
and container type words as `type`. Completion tests also cover grouping
overloads by visible name and generating compilable method skeletons. Built-in
type tests also cover localized
hover, no source definition, and rename refusal.

Qualified-completion coverage must also prove that the leaf `Label` and
`InsertText` remain the insertion contract, `LabelDescription` serializes as
`labelDetails.description`, and optional `$valencePresentation` has correct
UTF-16 display spans without changing filtering or sorting.

Namespace-segment coverage must separately verify that expression/statement
completion appends `::` only when the replace range has no following separator,
and that `using namespace` targets insert only the leaf name.

Workspace tests cover overlays, imported-file analysis, import paths, defines
and predefined macros, circular imports, project-wide access, ordered binding
catalogs, duplicate catalog definitions, catalog reload, and stale diagnostic
clearing. Localization tests verify catalog-key and placeholder parity,
fallback behavior, stable diagnostic identifiers, localized built-in type
display text, and the preservation of non-translatable protocol and language
identifiers. Localization coverage also checks generated completion descriptions
and `refactor.rewrite` titles, including their signature placeholders.

Tests must discover available catalog directories at runtime. They must not
assume a fixed number of locales.

## Extension boundary

Add language semantics to the core first. Tooling should consume only public
source, AST, diagnostic, module-metadata, and debugger APIs. A tooling query
must not bypass this boundary to inspect private compiler or VM state.

For a new feature, update these contracts together:

1. public result and option types, including source, range, version, and failure
   semantics;
2. the direct service and its workspace behavior;
3. protocol serialization and capability advertisement, only when the behavior
   is safe and complete;
4. localization only for genuine user-facing prose; and
5. focused feature, localization, and transport tests.

The LSP capability surface must match the actual safe handlers. Unsupported
operations must not be described as available, and a handler must not fabricate
an edit or location. The current server advertises completion resolve, type
definition, implementation, selection ranges, document links, full and delta
semantic tokens, and document, range, and on-type formatting. Its advertised
code-action kinds are `source.formatDocument`, `source.organizeImports`, and
`refactor.rewrite`; organize-imports appears when exact duplicate imports can
be removed safely, while rewrite actions generate eligible inherited or
interface method implementations at a blank cursor position in a type body. Range
formatting expands a selection to complete physical lines and returns the
actual replacement range.

## Failure and recovery behavior

Malformed documents, incomplete input, invalid requests, cancellation,
duplicate catalog definitions, unsupported locales, and stale workspace state
are expected states. Recovery should keep the host responsive without
fabricating declarations, ranges, diagnostics, or edits.

An empty feature result is different from a protocol or transport failure. A
language diagnostic is different from a framing error or invalid JSON-RPC
request. Preserve these distinctions through the direct service, adapter, and
host.

When a document version is not newer than the tracked version, ignore the
change. When a document becomes clean, publish an empty diagnostic list. When
an imported file disappears from a new analysis result, clear its previous
diagnostics. These are correctness requirements.

## Review checklist

Before publishing a tooling change, confirm:

- public headers and implementations agree on result and option shapes;
- all source ranges and edits use the shared coordinate contracts;
- document identity, overlay lifetime, and version checks are preserved;
- imported diagnostics and stale-result cleanup are covered;
- every advertised capability has a real handler and test;
- localized messages have coverage and unchanged placeholders;
- completion rank and LSP `sortText` agree, while `preselect` is never forced
  for tied or weak matches;
- built-in type hover text has catalog keys in every locale, while its type
  identifier, absent definition, and rename refusal remain invariant;
- semantic-token legends and lexer-recovery spans are documented and tested;
- protocol tokens, identifiers, paths, and source fragments remain exact; and
- the corresponding user-facing Tooling and LSP pages describe the contract.
## Debug and DAP coverage

Debug tests must cover source and file loading, overlays, conditional
breakpoints, entry and step stop reasons, output versus error output, and the
independence of success from the script exit code. They must also cover locals,
children, expression evaluation, local/child writes, call-depth limits, JIT
policy snapshots, and the rule that a debugger attachment selects `Vm`.

Breakpoint tests must cover multiline declaration/initializer pause locations,
preprocessing line maps, inactive and out-of-range failures, imports that share
a basename, and no instruction-budget cost for compiler pause markers.

DAP adapter tests must assert the initialize, launch/attach, breakpoint,
configuration-done, stopped, output, exited, terminated, and disconnect
lifecycle. Request methods that are only legal from a stopped callback must be
tested from that callback, while a disconnect during an active run must resume
and clean up after `ConfigurationDone` returns. Live binding pointers and
serialized catalog data require separate in-process and out-of-process tests.
They must also cover pending pre-launch breakpoints, stable DAP IDs, relocated
verified lines, localized unverified messages, and post-load `breakpoint`
events with `reason: "changed"`.

New protocol adapters should consume `VeroScriptDebugSession` and
`VeroScriptDapAdapter` rather than duplicating compilation, VM inspection,
breakpoint resolution, or value lifetime logic. Transport tests must separately
verify JSON/framing and must not change the in-process DAP-shaped contract. TCP
coverage additionally exercises IPv4 binding, `Port == 0` and `OnListening`,
sequential `MaxSessions`, clean termination mapping, and client-disconnect
behavior. DAP request coverage includes initialize metadata, locale selection,
script arguments, standard-library roots, working directory, scoped environment,
initial input, and collection-limit validation.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/en-US/index.md)

Language: [en-US](testing-and-extension.md) · [fr-FR](../fr-FR/testing-and-extension.md) · [ja-JP](../ja-JP/testing-and-extension.md) · [ko-KR](../ko-KR/testing-and-extension.md) · [zh-CN](../zh-CN/testing-and-extension.md) · [zh-TW](../zh-TW/testing-and-extension.md)
