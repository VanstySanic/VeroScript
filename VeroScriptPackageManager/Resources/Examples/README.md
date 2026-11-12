# Package manager examples

Copy `Local` to a writable work directory, then run with the matching CLI:

```text
veroscript install --path=<copy>/Local/app --offline
veroscript install -r <copy>/Local/requirements.txt --path=<copy>/Local/app --offline
veroscript --path=<copy>/Local/app <copy>/Local/app/main.vs
veroscript pack <copy>/Local/library --path=<copy>/Local/app --output=<copy>/example-math.vspkg
veroscript deploy --path=<copy>/Local/app --output=<copy>/deployment
```

The dependency directory is resolved against the explicit project root. Normal imports remain `import "example/math.vs"`. `Hooks` is an opt-in example: install its directory with `--allow-hooks`; generated files are recorded outside the original package snapshot.

The two install commands use the same local package. In `requirements.txt`, paths
are relative to that text file, so `./library` resolves to `Local/library` even
when `--path` selects `Local/app`. `-r` / `--requirement` is repeatable and supports
nested local lists, UTF-8, comments, version constraints, directories, archives and
HTTP(S) archive URLs. Registry examples are `gui==0.1.1`,
`gui>=0.1.1,<0.2.0` and `gui@0.1.1`; named direct references use
`gui @ "../gui.vspkg"`. Only `-r` / `--requirement` options are allowed inside a
list; keep registry, mirror, credentials and hook permissions on the command line.

`Embed` is a standalone consumer of the installed Core + PackageManager SDK. Give the executable an absolute installed project root and entry file. It uses the public library, with no process cwd lookup or automatic downloads. For standard-library imports, supply explicit `FCoreEnvironmentOptions.StdlibSearchPaths` from your SDK assets.

The installation archive may use `.vspkg` or `.zip` (case insensitive); both contain standard ZIP data with a root `veroscript-package.json`. `pack` without `--output` defaults to `<name>-<version>.vspkg`. For example, install the generated archive with `veroscript install <copy>/example-math.vspkg --path=<copy>/Local/app --offline`. This archive is separate from `.vslib` script libraries and `.vsapp` applications.

## Repeat installation, upgrade and uninstall

After the initial installation, these commands operate on the same copied project:

```text
veroscript install --path=<copy>/Local/app --offline
veroscript install example-math --path=<copy>/Local/app --upgrade --offline
veroscript install --path=<copy>/Local/app --upgrade --offline
veroscript uninstall example-math --path=<copy>/Local/app
veroscript list --path=<copy>/Local/app --json
veroscript clean --path=<copy>/Local/app
```

The `Local` project pins `example-math` to `0.1.1` from `../library`. Both upgrade
commands therefore keep that local version; they demonstrate selection of one
root or the whole dependency graph without requiring a registry. To adopt another
local distribution, update the declared source/version or explicitly install its
directory/archive. `--upgrade` does not change a local dependency into a registry
dependency or override version constraints, and cannot be combined with `--locked`.

For a registry dependency, `install example-math --upgrade` selects newer allowed
versions for that named root; `install --upgrade` reconsiders the whole graph.
A newly added registry dependency without a version is pinned to the selected
exact version. To move beyond that pin, change its version requirement or install
an explicit `name@version`. These commands are the update interface; there is no
separate `update` subcommand.

Ordinary `install` prefers a still-valid locked version and reuses identical
package content. It still creates a new installation generation and runs install
actions, trusted processors and permitted hooks again. `--locked` fixes the lock
result and `--offline` forbids networking; neither skips installation processing.
Packages with hooks still require `--allow-hooks` on each install invocation.

`uninstall` removes a declared root dependency and prunes dependencies no longer
needed by the remaining roots. The example's package list is then empty, and
`main.vs` needs reinstallation before it can import `example/math.vs` again.
Dependencies still required by another root remain installed; an indirect-only
dependency cannot be uninstalled by name. `clean` separately reclaims eligible
unreferenced generations after active leases are released.
