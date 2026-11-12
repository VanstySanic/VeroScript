# Install and manage extension packages

An explicit project root source also constrains transitive dependencies of the same name that omit a source. Their version and digest constraints still apply; conflicting explicit sources remain errors. Offline protocol installation therefore declares the local `vs-network` archive explicitly before installing `vs-http` or `vs-websocket`.

`veroscript-project.json` stores project dependencies, package enablement and configuration. The distribution root contains `veroscript-package.json`; build graphs use `veroscript-build.json`. Each file is validated against its own format and schema.

Use the optional package manager to install script libraries, resources and native extensions into an explicit project or Runtime/Core SDK. A distribution is ZIP data named .vspkg or .zip, with veroscript-package.json at its root. It is distinct from a .vslib script library, .vsapp application and .vsbc cache.

## Select the environment

Commands use the startup directory as the project root. --path selects another project; --root selects the executing CLI’s verified Runtime/Core SDK, and --root --path selects another verified component. Relative paths use the startup directory. Project/SDK storage defaults to .veroscript/packages; Runtime storage defaults to Packages. There is no user installation scope, implicit merge or automatic installation while running scripts.

## Commands

```sh
veroscript env --json
veroscript install ./library.vspkg --path ./app
veroscript install --path ./app --locked --offline
veroscript list --path ./app --json
veroscript show example-math --path ./app
veroscript disable example-math --path ./app
veroscript enable example-math --path ./app
veroscript verify --path ./app
veroscript uninstall example-math --path ./app
veroscript clean --path ./app
veroscript pack ./library --output ./library.vspkg
veroscript --path ./app ./app/main.vs
veroscript deploy --path ./app --output ./deployment
```

install resolves and installs; --locked preserves the lock and --offline prohibits networking. Remote artifacts require their complete cache; local archive sources must still exist. An existing locked directory snapshot can survive removal of its original source. --upgrade selects new versions and cannot combine with --locked. list/show inspect; verify checks integrity; disable retains files and version locks, but cannot break active dependencies; enable restores selection. clean removes only unreferenced owned generations without active leases.

pack does not overwrite an existing output. Use a new output path, or move the old archive before generating its replacement.

## Upgrade, uninstall and repeat installation

```sh
veroscript install example-math --path ./app --upgrade
veroscript install --path ./app --upgrade
veroscript uninstall example-math --path ./app
```

The first command reconsiders the named root dependency; the second reconsiders the whole dependency graph. There is no separate `update` subcommand. `--upgrade` respects the declared version constraints and source, and cannot combine with `--locked`. Adding an unversioned registry dependency initially pins its selected exact version; change the requirement or explicitly install `name@version` to move beyond that pin. A local directory/archive dependency continues to use that local source. The [local examples](../../../Examples/README.md) demonstrate both upgrade commands without requiring a registry.

Without `--upgrade`, installation prefers a still-valid locked version and reuses identical package content. Changing requirements, sources or local content can require a different resolution. Repeated installation still creates a new generation and runs install actions, trusted processors and permitted hooks again; it is not an operation that skips all processing when the package exists. Hooks still require `--allow-hooks` for each invocation. `--locked` constrains the lock result and `--offline` prohibits networking; neither suppresses installation processing.

`uninstall` accepts declared root dependencies. It removes the root requirement and prunes dependencies no longer needed by the remaining roots, while preserving shared dependencies. Remove or update a dependent root instead of uninstalling an indirect-only dependency by name. Unreferenced installation generations can be reclaimed separately with `clean` once no active leases remain.

## Run and deploy

Run/check/eval/REPL/watch, bytecode packaging and supported source AOT consume the selected installed environment. Without explicit selectors, script runs discover a project from the entry, eval/REPL from startup, then fall back to the executing component. LSP/DAP/Interop integration is adapter-specific; these interfaces do not automatically share the CLI’s package selectors. DAP rejects unsupported explicit selectors. AOT still requires the target’s native bridges.

deploy requires a new output directory and copies the enabled runtime dependency closure, resources, licenses and portable loading snapshot. Compile/development-only dependencies are excluded. Your application/.vsapp is supplied separately. Cross-target installation validates and copies target data without executing target libraries; each platform requires its own acceptance.

To export an installed foreign target, pass its exact target descriptor to `deploy --target <file>` or call `FInstalledEnvironment::DeployForTarget`. Export validates and copies the closure as data. Loading a deployment for execution still requires a matching host target.

## Configuration, failures and language

`recovery-conflict` means a management file differs from both values recorded in an interrupted transaction. It is preserved with `transaction.json`; a blocked rollback may report `recovery-required`. Back up the conflicting file and resolve the retained journal before retrying. Do not simply delete the journal.

--packages-dir, --cache-dir and --lockfile override storage explicitly. Do not relocate an existing installation by editing its storage path; reinstall into a new environment or deploy it. environment-changed preserves concurrent edits: reread and retry. layout-conflict means state/lock/ownership paths overlap. restart-required means a live native implementation cannot coexist with the replacement.

Package --lang and repeatable --fallback-lang select package display text independently from compiler diagnostic locale. Built-in package-manager diagnostics currently carry English messages and stable code fields; branch on code, not translated display text. --json keeps machine-readable results separate from messages. Package commands are unavailable in builds that disable the optional manager.

[Author distributions](authoring.md) · [Consume installed environments from C++](embedding.md)

## Install a dependency list

```sh
veroscript install -r requirements.txt
veroscript install --requirement "config/dependencies.txt" --path ./app
veroscript install -r requirements.txt --root
veroscript install -r requirements.txt --locked --offline
```

`-r FILE`, `-rFILE`, `--requirement FILE` and `--requirement=FILE` are repeatable, install-only options. They can accompany individual package arguments. The filename is arbitrary; no `.txt` suffix is required. Command-line file paths resolve from the startup directory, independently of `--path` / `--root`. Within a list, nested lists and local distributions resolve from the containing file's directory. Absolute paths, UTF-8 paths and spaces are supported; quote paths containing spaces or comment characters. Backslashes in quoted Windows paths remain literal; forward slashes also work.

Example UTF-8 list (LF/CRLF and an optional UTF-8 BOM are accepted):

```text
# Registry requirements use VeroScript versions.
gui==0.1.1
math >= 0.1.1, < 0.2.0 # both comparisons apply
widgets@0.1.1-beta.1
-r "lists/common.txt"
"../archives/local library.vspkg"
tools @ "../tools"
assets @ https://example.com/assets/download
```

Each logical line contains one dependency or one local `-r` / `--requirement` include. Blank lines and `#` comments are ignored; inline comments require whitespace before `#` outside quotes. An unescaped trailing backslash joins the next physical line with a space; comments are removed afterwards. Repeated package names intersect version requirements; differing explicit sources/settings fail instead of silently overwriting a previous entry. Named direct references verify the actual package manifest name. Relative local sources are rebased into the selected project's `veroscript-project.json`; resolved URL/archive hashes are recorded normally. Existing project dependencies are retained, and explicit list entries update the corresponding root requirements. All entries use one dependency solve and installation transaction. `--locked`, `--offline`, `--upgrade` and opt-in hooks keep their usual semantics. A failed batch does not commit a partial dependency set. Concurrent edits to input lists invalidate the pending commit.

Version operators are `==` / `=`, `<`, `<=`, `>` and `>=`; comma-separated or whitespace-separated comparisons mean AND. `name@<requirement>` also accepts the existing VeroScript syntax, including `name@*`. A bare name retains an existing project requirement; a new unversioned registry root is pinned to its selected version. `name @ <path-or-URL>` uses whitespace around `@` and selects a direct source. Paths without slashes should use `./directory` or quotes to distinguish them from package names.

This is a VeroScript dependency-list format inspired by pip, using VeroScript package names and versions. Python-specific extras, environment markers, VCS/editable installs, constraints files, `~=`, `!=` and pip options are not accepted. Includes must be local files; list contents do not expand environment variables, `~`, shell escapes or globs. Configure registries, mirrors, credentials and installation permissions through existing CLI/host options. Include cycles, malformed UTF-8 and invalid syntax produce file/line diagnostics. Reads, include depth/count, entry count, aggregate metadata, cancellation and deadlines use the existing client limits. The requirements file is an explicit input; ordinary `install` continues to use `veroscript-project.json` and does not discover text lists automatically.

### Offline example

Copy the `library` directory from the [local package example](../../../Examples/README.md) to `workspace/vendor/library`. Create the two files below, then run the commands from `workspace`. The result lists `example-math`; requirements are saved to the selected app’s veroscript-project.json and lockfile.

`workspace/requirements.txt`:

```text
# Local package list
-r lists/local.txt
```

`workspace/lists/local.txt`:

```text
"../vendor/library"
```

```sh
mkdir app
veroscript install -r requirements.txt --path ./app --offline
veroscript install --requirement=requirements.txt --path ./app --locked --offline
veroscript list --path ./app --json
```

Include cycles, invalid encoding and resource-limit violations are rejected. Diagnostics report the source file and starting physical line when available. `requirements-changed` means a list changed before commit; reread the files and retry.

For an explicit URL entry, `--locked` or `--offline` reuses a digest already recorded for the same URL (and package name, for a named reference). Cache bytes are still verified; an unknown or changed URL cannot borrow another URL’s cached identity. An offline repeat requires both the saved project requirements and complete verified archive content.

An incomplete installation returns `recovery-required`, including when the first transaction stopped before creating project/state records; use an explicit package writer operation to recover it. A valid, fully applied `committed` journal does not block reading. `combine-processing` means generated output or native loaders were prepared for different dependencies/configuration: install the desired package graph in one environment. `pack` never replaces an existing output, including one created concurrently.

Package storage and management files reject symbolic links and special entries. macOS permits the root-owned `/tmp`, `/var` and `/etc` system aliases only with their expected `/private/...` targets, so an ordinary temporary project or cache can use those ancestors. Package archive entries and links below the selected storage retain their existing rejection rules.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/en-US/index.md) · [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
