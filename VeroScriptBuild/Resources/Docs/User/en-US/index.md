# Build and package applications

`--build-file <file>` selects any configuration filename using an absolute path or a path relative to the command startup directory. Without `--path`, its parent becomes the project root; with `--path`, configuration inputs remain relative to that selected root. The default filename is `veroscript-build.json`.

The optional `VeroScriptBuild` service executes a strict `veroscript-build.json` graph. Paths inside the configuration are relative to the explicitly selected project root. CLI paths are relative to the startup directory; no command changes the process directory.

`--profile <name>` selects a profile declared in `profiles`; the default is `release`. `--target <name>` selects a target, and may be repeated. Without an explicit target selection, the configured `defaultTargets` apply.

```sh
veroscript build --path /absolute/project --plan --json
veroscript build --path /absolute/project --build-file /absolute/config/release.json --plan --json
veroscript build --path /absolute/project
veroscript build --path /absolute/project --target application --profile release
veroscript /absolute/project/dist/release/portable/application/sample.vsapp
```

`--plan` validates references and freezes inputs without running processors, custom steps, installation hooks, or subprocesses. `--allow-process` and `--allow-script` authorize the corresponding custom steps only during execution. Build does not install dependencies: install and lock the project first when packages are required. Run a deployed application with its explicit environment, rather than overlaying an unrelated current project.

Targets: `library` produces a `.vslib`; `application` produces a `.vsapp` (compiled or source); `distribution` produces an installable `.vspkg` or `.zip`; `deployment` exports an already installed environment; `bundle` combines an application and optional components; `custom` runs an explicitly registered provider or authorized process/script; `group` selects dependencies. Dependency references use `{ "target": "library", "output": "main" }`; source-tree members add `"path": "main.vs"`.

Application validation during a build uses the selected build target, including its platform and ABI, rather than the build machine identity. This permits preparing another target when its declared components are available; the resulting native components still require validation and execution on that target.

The checked-in `Resources/Examples/hello` project builds a library, compiled/source apps, directory and ZIP releases, and an installation distribution. Copy the project to a writable location before running it. Named outputs are under `dist/<profile>/<target-identity>/<target>/`; the portable identity is `portable`. Release directories belong to their build receipts. Foreign or modified files are never overwritten. Keep the returned artifact leases alive while native consumers use them.

Cached steps require unchanged configuration, frozen inputs, upstream content, provider identity and output hashes. Custom step caches need an explicit deterministic declaration and toolchain identity. Crypto/trust-sensitive compilation and dynamic compile processors are conservatively executed again. A stale plan fails; obtain a new plan after changing inputs. Publish each target atomically on the same volume; a later failing target does not roll back already completed independent targets.

A process step receives an executable and argument array, without a shell. Arguments may refer to a named input/output object. A script step has `build_read_text(input, relative)`, `build_write_text(output, relative, text)`, and `build_message(text)`. Imports are restricted to the script directory, declared input directories and explicit standard-library roots. Standard-library filesystem/environment/console/task/address/nondeterminism services are disabled; VM instruction/time/depth/memory/numeric limits apply. Native providers and native processes are trusted host capabilities, not an OS sandbox.

Runtime, dependency environment, and launcher are independent. Source applications need deployment usage `["runtime", "compile"]`; compiled apps normally need `["runtime"]`. `bundle.runtime` uses a verified `veroscript-runtime.json` component, copying only its listed runtime files. It never copies a whole SDK. A generated launcher uses an explicit matching bootstrap template; no PATH search or compiler invocation occurs. A single-file PE/ELF launcher contains the same ZIP content and materializes native components into an owned, verified cache. Mach-O/platform-specific wrappers require a custom provider. Target-specific native dependencies require separate application outputs for each platform/ABI; portable script content may be shared.

Schema: `Resources/Schemas/veroscript-build.schema.json`. Application loading, encryption and trust are described by the Application module. Build protection references keys by ID/version; do not put secrets in build files. `--crypto-library <absolute-library> --keyring <absolute-file>` configure the optional native service. Keyring files are host secrets and must not be copied into releases. Registry `publish` accepts installation distributions, not application containers.

Source applications are validated against the shipped content before publication. Portable defines, function macros and implicit imports are retained. Source-mode development import/stdlib search paths are rejected: use files/modules plus an explicit runtime environment. Materialize editor overlays as generated source inputs. `Resources/Examples/generated` demonstrates a restricted script generator and typed source-tree references. Directory and single-file applications use content signatures; native signing is a separate step.

Reassembling a signed application requires an explicit signature policy for the new content.

Windows Runtime inventories retain staged MSVC redistributables and their notice. Directory/ZIP bundles with a bundled Runtime also place these libraries beside the launcher. Single-file launchers still follow native loader requirements: a dynamic CRT build requires the matching VC++ Redistributable on the target; use a consistent static CRT source build for the native components when that prerequisite must be removed.

Embedded hosts executing `script` steps must explicitly supply a VM through `FServices::Compile.ScriptCompileExtensions.VmBackendFactory` or `VmBackendPath`; these options are exclusive. Missing configuration reports `script-vm`. The desktop build command uses its product VM while retaining the existing permission, instruction, deadline, cancellation and memory controls. VM path/content changes invalidate generator receipts; custom factories need a stable `FServices::Identity` to reuse script-step caches.

<!-- docs-navigation -->

---

[User documentation](../../../../../Resources/Docs/User/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
