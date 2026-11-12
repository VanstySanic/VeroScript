# Developer documentation — Rider

Shared editor/native responsibilities and six-language maintenance are maintained in [Common editor integration](../../../../../../../../Resources/Docs/Developer/en-US/editors.md).

Use `VeroScriptBundle` / `DynamicBundle` with localized `.properties` resources. Validate `MessageFormat` placeholders and escaped apostrophes, including real argument substitution when execution is permitted.

Use JDK/JVM 25, Gradle 9.5.0, Kotlin 2.4.0 and IntelliJ Platform plugin 2.19.0. Rider SDK is pinned to 2026.2.2; `sinceBuild=262.10315.191`, `strict-until-build=262.*` limits installation to the 262 branch; compatibility with other branches requires separate verification. Set `VEROSCRIPT_TEST_RUNTIME` to current extracted tools for native tests; unset means explicit skips. Optional `VEROSCRIPT_TEST_JIT_BACKEND` enables real module selection tests. SDK structure checks and a packaged ZIP do not replace Rider UI acceptance.

```sh
./gradlew test verifyPluginProjectConfiguration verifyPluginStructure buildPlugin
./gradlew verifyPlugin
```

`gradlew.bat` (Windows) · `bash ./gradlew` (Linux) · `-PlocalIdePath=/absolute/path/to/Rider`

[Standards](../../../../../../../../Resources/Docs/Standards/en-US/GENERAL.md)


Native regressions cover `std::memory` namespace/member completion, source spelling, hover, diagnostics and unchanged semantic-token delta. DAP tests use the plugin's LSP4J transport to expand buffers/views and watches, reject inspection-field writes, and verify identity preservation with reference invalidation after stepping. Metadata numbers remain protocol strings; `variablesReference` is an opaque handle, not a memory address.

For variables with child counts, request bounded DAP pages and end expansion on a short response. Without counts, fetch one complete snapshot and page it locally; a 100-item response alone is not evidence of paging support. Gate both asynchronous responses and cached-page callbacks on the current pause generation and node lifetime. Regression tests cover empty lists, 99/100/101/250 children, short responses, resumed snapshots and a real 100-element Runtime array.

Passive debugger tooltips only track editors in the session's project. Continue, step and pause failures apply only to the command's generation; expired failures do not affect newer stops. A current pause failure is reported without disconnecting, while transport loss still ends the session independently. Protocol and popup fixtures cover these boundaries, including both arrival orders of a pause failure response and a stop notification.

Exported-alias regressions cover `export using` and `export typedef` through a source facade, including completion, private-alias exclusion, hover and definition navigation. CLI Run/Check also exercise a facade importing a `.vslib` built by the same Runtime. These are native protocol/process tests, not Rider UI acceptance.

The local lexer follows the core's `constexpr` keyword. DAP variables whose `presentationHint.attributes` contain `readOnly` offer no modifier; other presentation attributes do not change writability, and host/domain capabilities still govern editing. Regression cases cover keyword boundaries and read-only, writable and otherwise annotated variable children.

## Compilation extensions and generated sources

DAP `stackTrace` frame `column` values already use one-based UTF-16, including local files, positive `sourceReference` snapshots and live domains. When editor line text is available, the client only changes the base to zero, clamps the position to that line and avoids splitting surrogate pairs; it must not convert from UTF-8 bytes again. Coordinate and controller regression cases cover CJK text, supplementary characters and CRLF.

Debug source references are resolved asynchronously before presenting initial or paged stack frames. Cache immutable read-only files by positive reference within one connection, including when a local path is also supplied. Keep snapshots outside project operations and LSP. Source failures preserve the stop; generation checks reject delayed stops, and disposal releases the cache. Native regressions cover source compiler imports, generated completion/hover/navigation and stepping into generated bodies, plus imported public macros. Standalone tools discover source compiler imports; native processing registries and `ModuleEnvironment` are host API options, not LSP initialization JSON settings. `veroscript/moduleSource` serves host environment modules; the current plugin starts a standalone LSP and does not connect to a host LSP endpoint. Failed source requests are removed from the cache so later stack loads can retry; duplicate references within one stack page are fetched once.

Annotation matching stays in Tooling/Core; the client preserves source spelling and server labels. Native regressions cover changing a catalog definition's `caseSensitive` policy without restarting, mixed-case compiler annotations in LSP/DAP, `matches_name`/`case_sensitive` completion, and callable parameter labels. A domain protocol fixture also covers a stop event arriving before the attach response, including capability availability and subsequent resume.

## Editor presentation

The raw editor lexer follows preprocessor line-comment continuation using immediate backslash-LF/CRLF boundaries; trailing horizontal whitespace does not continue a line. Regression tests cover token restart, truncated ranges, linear character-read counts, inline GUI highlighting, and empty-block Enter after comments and completed directives. Native LSP regressions cover nested callable, generic, alias, array/map and implicit-receiver chains, including private shadowing. DAP regressions retain read-only variant inspection and watch children while keeping unrelated locals editable.

The documentation target uses the public LSP transport asynchronously, adapts only recognized Markdown headings and retains server signatures and descriptions. The native hover target is disabled for this descriptor to avoid duplicate targets. Document stamps, file validity and client state reject stale results; cancellation propagates. The IDE Markdown renderer supplies themed code blocks. Debug value popups remain separate. Only recognized localized built-in type metadata attached to native type categories is merged, across all server result sections; source aliases and fenced examples retain their contents.

The Enter delegate performs one replacement inside the IDE command, preserving undo and using project indentation. Lexer checks exclude comments and strings; directives, continued headers, selections, multiple carets and analysis past the first 1 MiB, including the current-line suffix fall back to ordinary Enter. The project brace preference does not restart LSP. Full/range formatting and diagnostics remain server-owned. Tests cover rendering, asynchronous result invalidation, caret placement, undo and native diagnostics from incomplete to completed functions. Rider UI acceptance remains a separate step.

## Context configuration

Context producers accept saved local files and normalize their inherited template
to Run/Check. They clear attach-only fields and, for Check, script arguments,
input and JIT paths, retaining compatible working-directory/environment/compiler
settings. Check always passes `--backend=vm` without changing the saved Run backend
preference. Configuration validation propagates IDE cancellation unchanged rather
than displaying it as a configuration error.

<a id="rider-platform-sdk"></a>

When `localIdePath` is absent, both Rider projects resolve the multi-OS SDK archive with `rider(platformVersion) { useInstaller = false }`; a platform installer is not the development SDK. `-PlocalIdePath=<directory>` selects an existing local IDE instead. Keep the pinned Rider/build-branch, plugin dependency and JDK requirements above, and qualify each target OS separately.

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
