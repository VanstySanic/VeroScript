# Diagnostic localization

This page covers diagnostic language and host-tool UI. The loaded catalogs select display text without changing script semantics. Application text uses the separate [std::i18n API](standard-library/i18n.md); changing an application context does not change the process diagnostic language.

Language selection applies to diagnostics represented in the loaded catalogs. CLI help and some argument-validation messages currently remain in English; messages absent from the catalogs retain their original text.

## Support policy

The standard packages provide `en-US`, `fr-FR`, `ja-JP`, `ko-KR`, `zh-CN` and `zh-TW`. The languages available to your application depend on the catalogs it actually loads; use `GetAvailableLanguageTags` (or the corresponding SDK API) to inspect them. A valid additional culture directory can provide another language. Carry that directory with your application or load its catalog root explicitly.

Applications should use canonical BCP 47 tags such as `en-US`, `zh-CN`, and `zh-TW`. The runtime also normalizes common operating-system forms such as `ja_JP.UTF-8`, tags with an `@modifier`, and `C`/`POSIX`; the resolved form uses hyphens, lowercase language, title-case script, and uppercase alphabetic region subtags.

Normalization trims surrounding whitespace and removes encoding and modifier suffixes before resolving `C`/`POSIX` to `en-US`; this also covers `C.UTF-8` and `POSIX.UTF-8@modifier`.

## Matching and fallback

Language resolution proceeds as follows:

1. match an already loaded canonical tag exactly;
2. remove the last subtag repeatedly and try parent tags;
3. choose a deterministic candidate from the same primary language;
4. if no candidate exists, an explicit selection API reports failure, while initialization falls back to `en-US`.

Chinese script is preserved: `zh-Hant-HK` can match `zh-TW`, and `zh-Hans` can match `zh-CN`. An explicit `Hans` or `Hant` subtag takes priority over region inference; extension and private-use subtags do not select the script. With the default catalogs, `zh-Hans-TW` matches `zh-CN` and `zh-Hant-CN` matches `zh-TW`. A request for Simplified or Traditional Chinese is not silently matched to the opposite script. When a translated file or message key is missing, the runtime falls back to the corresponding `en-US` text as long as the English baseline contains that entry.

After `TrySetLanguageFromTag` succeeds, the active language is the canonical tag that actually matched. A failed selection leaves the current language unchanged. `GetAvailableLanguageTags` returns the languages currently loaded from the catalogs.

A diagnostic absent from the English baseline retains its original text. Loading a translated catalog does not translate script output or arbitrary application strings.

## Selecting a language

The CLI uses `--lang`; the environment variable is `VERO_LANG`:

```powershell
veroscript --lang zh-CN --check script.vs
$env:VERO_LANG = "en-US"
veroscript --check script.vs
```

The CLI initializes the environment and catalogs before parsing command-line arguments, so `--lang` overrides `VERO_LANG`. `VERO_LANG` overrides the operating-system language; if either value cannot be resolved, initialization uses `en-US`. An explicitly unsupported `--lang` is reported as an argument error.

For an editor-managed language-server session, set the BCP 47 preference in
`initialize.locale` before opening documents. `workspace/didChangeConfiguration`
can change analysis settings, but the language preference should be applied at
the next initialization so every open document starts with the same catalog.
`veroscript-lsp` uses the same `tooling.json` and compiler catalogs as the CLI;
clients should use diagnostic IDs rather than translated messages for logic.
Parser declaration diagnostics, including the constraints of `static class` and
`static struct`, use the same stable IDs and catalog path. CLI and LSP therefore
select the same localized message for the same source error.

## Directory layout

A localization root must contain a complete `en-US` baseline. Every other immediate directory is a culture directory, and its name must be a qualified canonical BCP 47 tag. VeroScript currently requires a hyphenated qualified culture name, so a bare `zh`, `ja`, or non-canonical `zh_cn` directory is rejected.

The default catalog layout is:

```text
localization/
  en-US/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
  <culture>/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
```

The tree may add more cultures or split JSON files into subdirectories while keeping the same rules:

```text
localization/
  en-US/
    runtime.json
    tooling/compiler.json
  <culture>/
    runtime.json
    tooling/compiler.json
```

The loader discovers `.json` files recursively and merges them by relative path. For every translated culture:

- each translated file must have the same path under `en-US`;
- a translated file cannot add a message ID absent from its English counterpart;
- a missing translated file or key uses the English text;
- files within one culture cannot define duplicate message IDs;
- a culture directory cannot be duplicated, empty, non-canonical, or unqualified.

Each JSON file must be a UTF-8 flat object whose keys and values are strings:

```json
{
	"diag.example": "Example message",
	"diag.with_argument": "Value: {0}"
}
```

Text may use `{0}` through `{9}` to receive diagnostic arguments. JSON must be a complete object with no duplicate keys, invalid escapes, invalid UTF-8, trailing content, or non-string values.

## Loading, replacement, and merging

`VERO_LOCALIZATION_DIR` selects a complete localization root. Set it before starting the tool or initializing the SDK. Without a valid explicit tree, the runtime tries packaged resources beside the executable (`localization` and `../assets/localization`) before working-directory resources. To select your own catalogs reliably, supply the environment variable or call `LoadCatalogDirectory` explicitly. Automatic discovery runs only while no catalog tree has been loaded.

`LoadCatalogDirectory` validates and replaces the complete process-wide catalog tree; if loading fails, the previously loaded tree remains active. `MergeCatalogDirectory` requires its argument to be a complete valid catalog tree, then merges every culture into the active set: existing message IDs are preserved by default and replaced only when overriding is explicitly enabled. Both operations use the core's thread-safe process-wide localization state.

A standard-library root, library-package root, or module file's sibling can carry a `localization/` directory. The module loader merges these trees without overriding existing IDs, allowing a library to add its own messages without unexpectedly changing the host's text. Source overlays retain their logical module path, so sibling catalogs remain discoverable.

## SDK boundaries

- A C++ host uses the `VeroScript::Locale` API directly; language and catalog state are process-wide in the core.
- Interop exposes the same policy through `vs_interop_locale_set_language`, `vs_interop_locale_get_language`, `vs_interop_locale_get_available_languages`, `vs_interop_locale_load_catalog_directory`, and `vs_interop_locale_merge_catalog_directory`.
- A WebAssembly/WASM build embeds the complete build-time `localization` tree in its virtual filesystem; `setLanguage` can select only cultures carried by that build, while `language` and `availableLanguages` report the canonical current language and available catalogs.

See [Command-line tool](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md) for CLI options and environment variables, [Host integration](../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md) for C++ hosts, [Interop SDK](../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md) for the C ABI, and [WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/en-US/index.md) for the Web boundary.

The standalone Python Frontend, BindGen, Transpiler and AOT tools select `python_tools.*` messages independently of the native runtime. Their `--lang` option takes precedence over `VERO_LANG` and the system locale. They normalize OS tags, preserve Chinese script, and resolve exact, parent, then deterministic same-language catalogs with English fallback. This does not change the process-wide native SDK language. See [Python frontend](../../../../../Extensions/Language/Python/Resources/Docs/User/en-US/frontend.md).


## Explicit mobile localization

The native Android AAR and iOS C ABI SDK supply six-language catalogs and require explicit loading. Copy the complete tree to a real app-owned directory before running scripts; APK asset paths are not filesystem directories. Android AAR resources are under `veroscript/localization`; iOS SDK resources are under `assets/localization`. Android exposes `VeroScriptEngine.loadCatalogDirectory(path)` and `setLanguage(tag)`; both return boolean success. The iOS C ABI exposes `vs_interop_locale_load_catalog_directory` and `vs_interop_locale_set_language`; release error buffers with `vs_interop_buffer_destroy`. Check every return value.

C# mobile applications first install resources with `VeroScriptMobileResources.InstallAsync`, then call `VeroScriptRuntime.LoadCatalogDirectory` and set `VeroScriptRuntime.CurrentLanguage`. Python loads the adjacent `veroscript/localization` tree when importing `veroscript`; preserve that tree in the deployed package, then call `veroscript.set_language`. See [C# mobile integration](../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/mobile.md) and [Python mobile integration](../../../../../Extensions/Language/Python/Resources/Docs/User/en-US/mobile.md).

Language selection is process-wide, not per engine. A failed selection preserves the current language. Script output is unchanged. Messages absent from the catalogs retain their original text; allocation failures may also retain English text.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](localization.md) · [fr-FR](../fr-FR/localization.md) · [ja-JP](../ja-JP/localization.md) · [ko-KR](../ko-KR/localization.md) · [zh-CN](../zh-CN/localization.md) · [zh-TW](../zh-TW/localization.md)
