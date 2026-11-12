# Author distributions

`pack` reads `veroscript-package.json` when present. Otherwise it reads `veroscript-project.json` and obtains distribution metadata from `packageManager.distribution` (an object or a project-relative JSON filename). Project packaging generates a separate `veroscript-package.json` in the archive and excludes the source project configuration. File filters must include the generated package manifest and every declared entry.

## Manifest and dependencies

Keep veroscript-package.json at the ZIP root. Names use 2–64 lowercase letters/digits with single hyphens and exclude reserved device names. Versions use major.minor.patch with optional prerelease and no build metadata. Requirements accept exact versions or whitespace-separated comparison intersections. Runtime also validates paths, targets, identities and complete dependency graphs; schema validation alone is insufficient.

```json
{
	"format": "veroscript-package",
	"formatVersion": "0.1.1",
	"name": "demo-math",
	"version": "0.1.1",
	"entries": {
		"modules": {
			"demo/math.vs": "scripts/math.vs"
		}
	}
}
```

Dependencies declare runtime/compile/development usage. --development selects development dependencies; optional dependencies require `packages.<name>.enabled=true`. Prereleases require an explicit prerelease requirement or --prerelease. One canonical package name has one version and source in an environment. Only applicable runtime root declarations enter deployment; transitive installation does not grant an unrelated direct dependency permission.

## Resources, inputs and display text

Expose public imports through entries.modules and named resource groups through resources. Package paths are portable relative paths without traversal, links or device aliases. Missing optional resources differ from missing/modified receipt-owned files; integrity failures must not be treated as ordinary absence.

A localization root contains normalized BCP 47 directories and an en-US JSON baseline. Translations may omit baseline keys but cannot add new ones, duplicate keys across files or change formatId. Plain strings are literal, including braces; formatted messages explicitly use pattern and formatId=vero.named. Standard display keys include package.displayName, package.description, install.before/install.after and `inputs.<name>.description`. Named standard messages receive package.name and package.version. These catalogs do not change script/compiler diagnostics.

```json
{
	"package.displayName": "Demo Math",
	"install.after": {
		"pattern": "{package.name} {package.version}",
		"formatId": "vero.named"
	}
}
```

Inputs retain string, boolean, integer or enum types. Use `--input=name=<JSON>` for ordinary input and --secret-env=name=ENV_NAME for sensitive strings. Secrets remain operation-only and cannot be substituted by declarative templates. Version-qualified keys `<package>@<version>.<input>` take precedence over `<package>.<input>` and a bare name. The CLI is non-interactive by default.

## Installation processing and limits

Declarative copy/template actions write only the new generated area. Script hooks require --allow-hooks and an exported non-generic void entry(std::package::install_context). They use permitted resources and generated writes; file/environment/console/task/address/clock services and package native registration are unavailable. Cancellation and VM budgets are cooperative, not an operating-system sandbox.

Defaults: 1,000,000 hook instructions, 2,000 ms hook time, 16 MiB per resource text service operation, 64 MiB hook buffer quota, and 65,536 display bytes. These do not bound the whole heap. Hooks receive typed input and an independent display snapshot. Custom trusted processors are host services with explicit versions and implementation identities. Configuration-dependent generated outputs require reinstall after configuration changes. uninstall --skip-hooks is an explicit recovery option that reports a warning.

Hook compilation uses Core's ordinary `constexpr`, `if constexpr`, generics and source/`.vslib` import paths. Each compilation shares one constant-evaluation budget: 1,000,000 work units, 64 calls deep, 256 expression levels, 16 MiB evaluation memory and 8 MiB cache by default. Hosts configure `FLimits.HookConstantEvaluationLimits`; only `MaxCacheBytes` may be zero to disable caching. Preflight and execution compilation, including uninstall hooks, use the same policy. Compilation observes the package operation's total deadline and cancellation; the 2,000 ms VM timeout applies to execution separately. Numeric and buffer limits remain runtime policies.

## Registries and publication

The CLI defaults to https://veroscript.org/api when neither the project configuration nor lockfile defines the official source. --registry-api replaces this fallback; packageManager.registries.official configures a project source. Embedding hosts still configure FClientOptions.OfficialApi or a project source explicitly. HTTPS verifies certificates; --allow-http is only an explicit development choice. --credential-env reads the Authorization header value, for example Bearer YOUR_TOKEN, without the Authorization: field name. Without --credential-scope, credentials are limited to --registry-api or the built-in official API; only URLs within that origin/path scope receive credentials. Set --credential-scope explicitly for a project API or mirror outside that scope. Never put credentials in locks or source manifests.

A configured mirror needs trustedMirror=true; --mirror authorizes only the official source for that invocation. allowOriginFallback defaults to false. Redirects strip sensitive headers across origin/scope changes and never downgrade HTTPS.

## Publish additional platforms

Use publish to create a version or add more platform artifacts to that same version. All supplied archives must have the same package name, version, dependencies and conflicts. Their manifest targets must be disjoint from one another and from already published artifacts. Different OS or architecture constraints can distinguish platforms; different minimum versions or capabilities alone cannot. A portable artifact overlaps every target.

Existing artifact IDs, bytes and metadata remain immutable. An identical artifact is reused without another upload; changed bytes for an existing target require a new package version. Changing only the archive filename does not create a different target. After an uncertain network result, retain the original archives and publication cache and retry with the same files. The client checks the registry before confirming success.

Supply each archive only once per command; two inputs with identical archive bytes are rejected even if their filenames differ.

```text
veroscript install example-math@0.1.1
veroscript publish ./library.vspkg
veroscript publish ./gui-0.1.1-windows-x64.vspkg ./gui-0.1.1-linux-x64.vspkg
veroscript publish ./gui-0.1.1-macos-arm64.vspkg
veroscript install example-math@0.1.1 --registry-api=https://your-host/api/v1
```

[Package schema](../../../Schemas/veroscript-package.schema.json) · [Localization schema](../../../Schemas/veroscript-package-localization.schema.json) · [Example manifest](../../../Examples/Local/library/veroscript-package.json)

## Native entries and CLI packages

Native entries require `id`, `path`, `linkage` and `roles` in `entries.native`. An optional `componentVersion` checks the native descriptor version; analysis of bindings/compiler entries requires both it and `catalog`. Supported role strings are `bindings`, `compiler`, `provider` and `cli`; the manifest role set must match the queried library descriptor. Native packages require the matching target identity, including OS, architecture, environment, Core build and `abis.native-extension`; obtain values from the corresponding `veroscript env --json`, not from the package version alone.

A `cli` entry registers commands/options with the CLI host service. Once its package is installed and enabled in the selected environment, commands are available by their registered names; no library selector is needed. Disabling the package removes those commands from discovery. CLI-only entries are skipped in analysis and hosts without the CLI service. Combined bindings/compiler roles still require the normal analysis catalog and component version. Package installation validates the ordinary immutable artifact and loader metadata.

[Command use](../../../../../VeroScriptCli/Resources/Docs/User/en-US/index.md)

Official VeroScript installable packages use the `vs-` prefix, for example `vs-asmjit`; its JIT backend ID remains `asmjit`. The official registry reserves this prefix for authorized official publishers. Local or private-source packages with the same prefix are not automatically official; origin and artifact verification still apply.

Native entry IDs use lowercase ASCII letters/digits separated by single hyphens, with no leading/trailing hyphen. The schema accepts 2–64 characters; a native descriptor stores 64 bytes including its NUL terminator, so use at most 63 characters for an actual native module. The declared ID must equal the descriptor and any expected ID. `entries.backends[].module` refers to a declared native provider ID, not a host service ID.

### Official backend and GUI distributions

JIT and script-enabled foundation releases provide installable `.vspkg` files alongside the existing portable ZIP/tar archives with a product directory. Only an archive with a root `veroscript-package.json` is installable. Installation defaults to the current project; `--path` accepts a relative or absolute project path and `--root` selects a Runtime/SDK environment.

```text
veroscript install ./vs-asmjit-0.1.1-win-x64.vspkg --path ./MyProject
veroscript --path ./MyProject --jit-backend asmjit --backend jit ./MyProject/main.vs
```

The GUI adapter uses the host's existing `veroscript.gui.tools` service. Use a matching GUI-enabled CLI/Runtime or an embedding host explicitly assembled with GuiScript. Installation does not embed GUI into pure Core. SDL3, RmlUi, Stb and FontBackend remain independently installed and selected providers. Offline dependencies need explicit archive sources in project dependencies or command inputs.

## Backend entries and reserved selections

`entries.backends[]` requires `service` and `id`, using the package ID grammar (2–64 lowercase ASCII letters/digits with single hyphen separators). `auto` and `none` are reserved selection values and cannot be backend IDs. Choose exactly one form: `module` refers to a declared native entry with the `provider` role, excluding `path` and `contract`; or `path` plus `contract` declares a dedicated VM/JIT library, excludes `module`, and requires both `service` and `contract` to be `vm` or both `jit`. Native target and ABI validation still apply. In project `services`, omitting `backend` inherits automatic selection; an explicitly empty value or `none` disables, and `auto` requests automatic candidates.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/en-US/index.md) · [en-US](authoring.md) · [fr-FR](../fr-FR/authoring.md) · [ja-JP](../ja-JP/authoring.md) · [ko-KR](../ko-KR/authoring.md) · [zh-CN](../zh-CN/authoring.md) · [zh-TW](../zh-TW/authoring.md)
