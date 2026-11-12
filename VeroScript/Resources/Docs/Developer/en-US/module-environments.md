# Resolved module environments and native extensions

## Responsibilities and entry points

Core consumes an already resolved, enabled immutable snapshot and provides shared backend selection contracts. Package clients and hosts read project settings, resolve versions and supply installed candidates; Core does not download, unpack, install, scan installations or search system paths. Installing a package does not implicitly import all declarations.

Create `FModuleEnvironmentDesc` and call `FModuleEnvironment::Create`. Assign the returned shared immutable environment to `FCompileOptions::ModuleEnvironment`, then pass the same environment to `VeroScriptVm::SetModuleEnvironment` before executing. `FCodegenOptions::ModuleEnvironment` uses the same inputs for source export. See the buildable `Resources/Examples/ModuleEnvironment` example.

## Modules and scope

`Scopes` describes each owner and its direct dependencies. `EntryOwnerId` selects the entry scope; `bHostProvided` explicitly makes a host scope visible to all. `SourceRoots` are absolute, canonical filesystem roots with ownership. `Modules` maps exact public import names to module IDs, owners, content identities and either an absolute file or a `veroscript-module:/...` path with `Source`.

Ordinary imports consult exact mappings. A conflicting local candidate is an error; explicit `./` or `../` selects only the importing module’s relative path, still subject to ownership and root checks. A missing explicit path never falls back to search roots. Missing or invalid mapped modules do not fall back to unrelated files. `import`, `include`, exported imports and macro discovery share this loader. The environment defaults to strict sources and no ambient standard-library/cwd search; explicitly supply required standard-library roots and their scope.

A module may use its own sources and declared direct dependencies. Re-exported interfaces remain available through normal language visibility. Native declarations carry `FBindingOrigin`; the compiler applies the same scope and module visibility rules. Public API signatures can expose the native types required to use their values. This is dependency isolation, not a security sandbox: registered native code and host callbacks are trusted.

Publish catalogs and resource contracts once. Do not mutate their declarations or callback identities after publication. `Apply` normalizes compile inputs transactionally and rejects conflicting live binding catalogs. Combine native registrations against one base registry before publishing; merging independently assigned host type IDs is unsafe. Explicit overlays remain exact source overrides.

## Resource services and localization

Register `FResourceProviderBinding` entries containing the provider, owner, stable contract identity and optional resource-only `ModuleIds`. An `IResourceProvider` receives the logical `(ProviderId, ModuleId, Group, Path)` request, a cancellation callback and byte limit. Paths are relative portable paths without traversal. Read results distinguish a successful missing resource from an IO/provider failure. Existing resources require a content identity; an optional absolute physical path is diagnostic metadata, not permission to bypass a provider.

Use `FModuleEnvironment::ReadResource` at runtime. Compilation processors use `Context.ReadResource`; script compiler extensions use `read_resource(provider,module,group,path)`. These reads record content and missing-resource observations in the normal dependency ledger, so changes invalidate prepared results. Provider implementations must be thread safe, honor limits/cancellation and avoid unbounded reads. Core rechecks output limits and identities. `MaxResourceBytes` limits individual reads; compilation additionally enforces aggregate input/dependency budgets.

Reuse existing `I18n::FI18nServices`, typed messages, contexts and limits through the environment. Package metadata JSON is interpreted by the package client; Core adds no competing localization format. Native extensions may request host-defined typed services through `GetService(id,version)`; deterministic service identities enter processor cache keys.

## Native registration and lifetime

Package-managed native IDs (`entries.native[].id`) follow `[a-z0-9]+(?:-[a-z0-9]+)*`; the package schema permits 2–64 characters, while the NUL-terminated `Descriptor.Id[64]` limits a loadable native ID to 63 ASCII characters. `Descriptor.Id`, the manifest ID and `ExpectedId` must agree; `entries.backends[].module` and static registration keys `package:ID` refer to that ID. The low-level Core descriptor loader validates its own bounded UTF-8 contract and does not impose the package naming grammar. Bundled modules use `veroscript-sdl3`, `veroscript-rmlui`, `veroscript-stb`, `veroscript-font-backend` and `veroscript-gui-tools`; rebuild native libraries, catalogs and environments together after changing IDs.

Service IDs, backend choices and interface contract IDs follow their own protocols. In particular, the native extension `veroscript-gui-tools` requests the separate host service `veroscript.gui.tools`; the latter remains the `ToolServiceId` lookup key.

Include `VeroScriptNativeExtensionLoader.h`. `LoadNativeExtension` requires an explicit absolute library file; it performs no package/name/PATH/cwd discovery. A statically linked module uses `QueryNativeExtension` with the same query function and an owner. The exported C symbol is `veroscript_extension_query`; protocol `NativeExtensionApiVersion` is **0.1.1**. Validate expected ID/version/roles before registering.

The fixed-layout query descriptor must match SDK public ABI, core implementation identity, compiler/STL/runtime, architecture/platform, RTTI and exception modes. The C query does not make subsequent C++ objects a stable C ABI. Dynamic MSVC extensions require the shared CRT. iOS and WebAssembly use static registration. Binary content hashing is a cache identity, not an authenticity/signature check; the package client verifies trusted artifacts and keeps immutable files leased.

Use `FNativeExtensionRegistry::Add` to stage a module. Its `Create` receives `INativeExtensionHost` for registering functions, types/members, generic templates, aliases, annotation vocabulary, compiler processors and service providers. The extension uses the host vtable and does not link its own static Core. Registration succeeds atomically or leaves the previous snapshot intact. `GetBindings`, `GetProcessors`, `GetProviders`, `GetOwners`, `GetDependencies` and `GetFingerprint` build the resolved environment. Use `GetDependencies` to populate available contracts from the verified loaded descriptors and binary identities; do not infer them from package version alone. Their version is the native component version and is not automatically the distribution package version. Every native binding provider requires an exact available external dependency contract.

Host services are versioned interfaces with owned lifetimes. Registration is the only phase allowed to mutate declarations. After it closes, retained host services may create owned host objects, read resources, and submit compiler operations through the bridge. Use `MakeHostObject`/`MakeHostObjectNamed` for payloads and a nonthrowing destroy callback; the Core allocates ownership control blocks. Strong references retain module code until callbacks, processor factories/instances, providers, service instances and escaped objects are gone. Extension instances must avoid retaining strong references back to objects that own the same registration; use weak references for such links. Factories/destructors must obey their error and ownership contracts; destructors must not throw.

Provider registration does not initialize a GUI or another service. Select `(ServiceId, BackendId)` above Core, verify its contract, and call `CreateNativeService`. The returned instance retains its environment and code until destruction. JIT uses the existing `LoadJitBackend` protocol rather than this generic service protocol.

Native generic templates retain the existing registry contract: instantiate required concrete types with `FHostBindingRegistry::InstantiateGenericHostType` on the staging registry before publishing the immutable catalog. Runtime execution does not mutate a published catalog; script generics use ordinary compiler instantiation.

## Cache, packages and reload

Compilation fingerprints include graph ownership, source identities, bindings, processor versions/configuration, annotation definitions and compiler policy. Native registration configuration entries enter processor identities automatically, including when no separate host configuration fingerprint is supplied. Resources add exact read dependencies. Runtime-only configuration has a separate execution identity. Unidentified or nondeterministic native host services disable processor caching. Hosts must supply stable identities for all other opaque callbacks/configuration that affect generated output.

`FExternalDependency` records exact ID/version/ABI/build/content identity; Core verifies, merges and serializes these records without solving versions. Required contracts survive bytecode, `.vslib`/`.vsapp` and linking. Missing or mismatched runtime contracts fail before initializers or JIT execution. Open generic package source retains producer scope; the importing host must provide compatible native bindings and mapped requirements. Retained owner IDs for packaged helper modules must also have scopes and direct dependency edges in the effective environment, even when their source is already embedded in the library. The product and bytecode version stay **0.1.1**; older pre-release bytecode is not accepted.

Standalone AOT can export script-only results using the same environment and generated declarations. AOT rejects execution-dependent native package contracts until a target runtime adapter supplies their deployment and binding contract; it cannot silently replace extension callbacks with unrelated extern declarations.

Reload publishes a new environment at an idle boundary. Existing work retains the old snapshot and file leases. A module without `NativeExtensionParallelRegistrations` requires process restart before another registration. Replacing a loaded library in place is rejected; use an immutable versioned path. Native objects have a registration identity, so an object from an old registration cannot enter new callbacks even when its type name matches. VM/session threading and cancellation rules remain in force; a blocking native callback cannot be forcibly interrupted. Already running workers keep their original environment.

## Verification and integration

`VeroScriptModuleEnvironmentTests`, `VeroScriptNativeExtensionTests`, public-header tests and `VeroScriptModuleEnvironmentExample` cover Core contracts. The dynamic fixture deliberately links no Core. Each release target still requires its own loading/device validation. Package installation and environment selection belong to the package client, CLI and Interop/language adapters; Core consumes their resolved snapshots.

## Command-line host service

The `CommandLine` role (`0x8`, package spelling `cli`) uses the matched-SDK service `veroscript.cli` version `0.1.1`. The CLI supplies `ICommandLineExtensionService`; a native module requests it through `GetService` during Create and registers its descriptor before registration closes. Combined-role modules must tolerate a missing CLI service in ordinary SDK hosts. The package client skips CLI-only entries in analysis and in hosts that do not supply this service. The CLI owns option parsing and command dispatch.

[CLI](../../../../../VeroScriptCli/Resources/Docs/Developer/en-US/process-and-assets.md)

## Backend selection and environment adapters

`VeroScriptBackendSelection.h` owns `ResolveBackendSelection`, `SelectServiceBackend` and `ResolveExecutionBackends`. An omitted request inherits `SelectedBackends`; a stored empty value means `none`. Explicit `auto` ignores a configured default. Candidates are sorted by ID; empty, reserved or duplicate candidate IDs are rejected. Only factory/preparation failure tries another automatic candidate. Cancellation escapes; a running operation is never replayed.

`FBackendSelectionResult` distinguishes `Selected`, `Disabled`, `Unavailable` and `Failed`, retaining `BackendId`, rejected attempts and error text. An automatic optional service may be unavailable without failing the environment; callers must check the resulting factory as well as success. `FInstalledEnvironment::GetBackendCandidates` is metadata-only and excludes disabled packages. `LoadJitBackend` / `LoadVmBackend` optionally return the selection record; `BackendDiagnostics` retains unavailable automatic provider-module diagnostics.

`FModuleEnvironmentDesc` carries `SelectedBackends`, `ServiceProviders`, `BackendDiagnostics` and `ExecutionBackends`. Package adapters provide immutable resolver callbacks and `ExecutionBackends.Identity`; they retain the installed snapshot and code leases. `AnalysisOnly` does not install execution resolvers. `FNativeServiceProviderDesc` is declared in the selection header; native-extension helpers validate the service/backend/contract/version tuple before creation. C, C++, C# and Python share native Session resolution rather than implementing separate package discovery.

`ResolveExecutionBackends` prepares an output copy before committing. Explicit `VmBackendFactory` / `VmBackendPath` outrank environment defaults; factory, path and explicit clear are mutually exclusive. `FallbackVmBackendFactory` / `FallbackVmBackendPath` are product assembly inputs used when automatic environment selection has no usable VM; they never override explicit failure or disable. `bDisableJitBackend` prevents environment JIT defaults from being enabled. Session retains its product VM fallback; `SetFallbackVmBackendFactory` changes that fallback and `SetVmBackendPath("auto")` may restore it before execution. The VM domain still freezes on execution, including JIT, and Clear/reload does not thaw it. Keep module and environment owners alive for factories, instances and retained code.

With a module environment, a processor whose `Origin.ProviderId` is set receives events only for source owners that can access that provider, or through its visible exported `Origin.ModulePath`. Packaged producer ownership and captured source ownership take precedence over physical-path lookup. Host-global processors without a provider and hosts without an environment retain their existing scope. `import compiler` and configured script roots still activate script processors explicitly; dispatch filtering does not grant source, resource or native-binding dependencies.

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [en-US](module-environments.md) · [fr-FR](../fr-FR/module-environments.md) · [ja-JP](../ja-JP/module-environments.md) · [ko-KR](../ko-KR/module-environments.md) · [zh-CN](../zh-CN/module-environments.md) · [zh-TW](../zh-TW/module-environments.md)
