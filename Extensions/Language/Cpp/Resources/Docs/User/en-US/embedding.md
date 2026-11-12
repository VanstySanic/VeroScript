# C++ embedding

VeroScript can be embedded in a C++ host. Hosts commonly use `VeroScriptEngine` for one-shot checking or execution and `VeroScriptSession` for long-lived modules, bindings, runtime state, and hot reload.

## Select a VM explicitly

Core does not provide an implicit interpreter. The examples below use the separately distributed static bytecode VM: find both SDK packages, link both targets, and explicitly configure each engine configuration or Session. Linking alone does not select a backend.

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Runtime)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScriptCpp VeroScript::VeroScriptBytecodeVm)
```

For dynamic loading, set `FEngineConfig::VmBackendPath` or call `Session::SetVmBackendPath`, using the deployed VM file or directory. A factory and a path are exclusive. Backend selection is fixed after execution begins; `Clear()` does not unlock it. Compiler scripts inherit the selected VM unless `ScriptCompileExtensions` specifies its own factory or path. Ordinary compilation without compiler scripts needs no VM.

For an existing Session, an `FEngineConfig` with neither VM factory nor path preserves the current selection. Set `bClearVmBackend=true` to request an explicit clear before the domain freezes; factory, path and clear are mutually exclusive. `Clear()` resets the loaded program, while the VM domain remains frozen after execution. Use a new Session to change its interpreter after that point.

## High-level engine entry points

Include the public `VeroScriptEngine.h` header supplied by the SDK. `VeroScriptEngine` provides source-buffer and file entry points:

- `CheckSource` / `CheckFile` perform lexical, syntactic, and semantic checking;
- `RunSource` / `RunFile` load and execute a script.

These entry points can receive import paths, script arguments, defines, standard-library search paths, bytecode-cache directories, execution backends, and binding catalogs.

`FEngineConfig` also exposes structured predefined macros, implicit import paths,
in-memory source overlays, a maximum call depth, circular-import and
project-wide-access switches, and an input stream. Embedded `RunFile` defaults
to bytecode caching off; enable it explicitly with `bUseBytecodeCache` and
`BytecodeCacheDirectory` when appropriate. The execution backend is `Vm`,
`Mixed`, or `Jit`, and defaults to `Mixed`.

Bytecode caches include the Core implementation and public API identities.
Changing Core code invalidates existing caches even when the product and
bytecode versions stay unchanged; repeated runs of the same build reuse them.

`ExecutionLimits.MaxInstructions` and `ExecutionLimits.TimeoutMilliseconds` are cooperative per-execution limits; zero means unlimited. `ExecutionControl` accepts a shared `FExecutionControl`; call `RequestCancel()` from another thread to request cancellation. Core enforces the same instruction, cancellation and permission checks for interpreted and supported native execution. Governed `Mixed` execution uses the VM by default; a Session can opt into dynamic native execution with `SetJitPolicy` and `FJitPolicy::bEnableControlledMixed=true`. Explicit `Jit` and a selected static Core AOT program are independent of that switch. Code without the required governance capabilities falls back to the VM before entry. A native callback already running is not forcibly terminated. The control token is not reset automatically; call `Reset()` before reusing it.

`Capabilities` controls standard-library side effects. Its flags cover file
read/write/delete, environment read/write, console input/output, and task
spawning. The default allows all of them; restrictions apply to built-ins only,
so the host remains responsible for authorization inside registered callbacks.
`bAllowProcessDirectoryChange` separately denies `chdir` from changing the host
process working directory; `chdir` also requires environment-write permission.

The entry points return `FRunResult`. `bSuccess` reports normal
compilation/execution; a successful `int main` or `exit(n)` may still produce a
non-zero `ExitCode`. `bExitRequested` is set only by an explicit `exit(n)`,
including `exit(0)`; an ordinary `int main` return does not set it. Read `Error`
and `Diagnostics` for failure details.

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptEngine.h"

#include <iostream>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	config.ScriptArgs = {"alpha", "beta"};

	const VeroScript::FRunResult result = VeroScript::VeroScriptEngine::RunFile(
		"main.vs",
		host,
		config);

	if (!result.bSuccess)
	{
		return result.ExitCode;
	}
	return result.ExitCode;
}
```

`ERunMode` is independent of the backend: `Eval` uses the incremental/REPL
initialization path, `EvalOnce` evaluates source once, and `Script` invokes
`main` after loading. Debugger runs, retained initialization ranges and bounded cleanup use the VM path; ordinary persistent functions can use the selected JIT.

## Embedded VM debugger

`VeroScriptDebugger` is the synchronous in-process debugger for hosts that own
a `VeroScriptVm` directly. It is separate from DAP: it does not open a wire
connection or encode JSON. Include `VeroScriptCompile.h`,
`VeroScriptDebugger.h`, and `VeroScriptVm.h`, compile a source buffer or file
to an `FBytecodeModule`, attach a non-owning debugger with `SetDebugger`, and
execute the module.

To use the bytecode interpreter, include `VeroScriptBytecodeVmAssembly.h`
and call `VeroScript::BytecodeVm::Configure(vm)` before execution.

```cpp
#include "VeroScriptCompile.h"
#include "VeroScriptDebugger.h"
#include "VeroScriptVm.h"
#include "VeroScriptBytecodeVmAssembly.h"

#include <iostream>
#include <sstream>

int main()
{
	VeroScript::FCompileResult compiled =
		VeroScript::VeroScriptCompile::CompileFile("main.vs");
	if (!compiled.bSuccess || !compiled.Module)
	{
		return 1;
	}

	std::ostringstream output;
	std::ostringstream errors;
	VeroScript::VeroScriptVm vm(output, std::cin, errors);
	VeroScript::BytecodeVm::Configure(vm);
	VeroScript::VeroScriptDebugger debugger;
	debugger.SetBreakpoint("main.vs", 4);
	debugger.SetStopCallback(
		[](VeroScript::VeroScriptDebugger& active,
			VeroScript::EDebugStopReason reason)
		{
			if (reason == VeroScript::EDebugStopReason::Breakpoint)
			{
				const auto frames = active.GetStackTrace();
				const auto locals = active.GetLocals();
				(void)frames;
				(void)locals;
			}
			active.SetRunMode(VeroScript::EDebugRunMode::Continue);
		});

	vm.SetDebugger(&debugger);
	const bool ok = vm.Execute(
		*compiled.Module,
		VeroScript::ERunMode::Script);
	vm.SetDebugger(nullptr);
	return ok ? vm.GetProcessExitCode() : 1;
}
```

`SetBreakpoint` uses one-based source lines. An empty file label matches any
file; a non-empty label matches the full label or its basename. A conditional
breakpoint evaluates its expression in the top frame. Evaluation errors and
false conditions skip the stop; Boolean values, non-zero numbers, and other
non-empty results are treated as true.

The stop callback runs synchronously while the VM is paused. Use
`GetStackTrace`, `GetLocals`, and `GetChildren` to inspect state. `Evaluate` is
read-only and evaluates in the selected frame; use `SetLocal` or `SetChild` to
write a value through a debugger variable. Continue the run, or choose
`StepIn`, `StepOver`, or `StepOut`, by calling `SetRunMode` from the callback.
If the callback does not choose a mode, the current mode remains in effect.

`RequestPause()` is a thread-safe cooperative request that is observed at VM
safe points and reported as `EDebugStopReason::Pause`. It does not forcibly
interrupt a native callback. Call `ResetPauseRequest()` before reusing a
debugger when a previous pause request may still be pending. `SetDebugger`
does not transfer ownership: the debugger must outlive the VM execution and
the VM must outlive any callback that uses it. Attaching a debugger forces the
effective execution backend to `Vm` and invalidates the JIT cache.

For asynchronous editor integration, use `VeroScriptDebugSession` or the DAP
adapters described in [Tooling](../../../../../../../VeroScriptTooling/Resources/Docs/User/en-US/index.md) and [Debugging](../../../../../../../VeroScriptTooling/Resources/Docs/User/en-US/debugging.md).

## Debugging an existing Session

[Debugging an existing Session](session-debugging.md). `SetDebugBridge`, `ISessionDebugBridge`, `SetDebugBridge(nullptr)`.

## Diagnostic localization

Engine entry points initialize core localization automatically. They prefer
`VERO_LOCALIZATION_DIR`, then use the deployment search order described in
[Diagnostic localization](../../../../../../../VeroScript/Resources/Docs/User/en-US/localization.md), and select a culture from
`VERO_LANG` or the system UI language. A host does not need an extra
initialization call just to receive English diagnostics.

For explicit catalog and language control, use `VeroScript::Locale` directly:

```cpp
#include "VeroScriptLocale.h"

#include <iostream>
#include <string>

int main()
{
	std::string error;
	if (!VeroScript::Locale::LoadCatalogDirectory("localization", &error))
	{
		std::cerr << error << '\n';
		return 1;
	}

	if (!VeroScript::Locale::TrySetLanguageFromTag("zh-CN"))
	{
		return 2;
	}

	const std::string language = VeroScript::Locale::GetLanguageTag();
	std::cout << language << '\n';
	return 0;
}
```

`LoadCatalogDirectory` replaces the process-wide catalog tree. A library or
standard-library root can use `MergeCatalogDirectory` to add a sibling
`localization/` tree; existing message IDs are preserved unless overriding is
requested. Core language and catalog operations are thread-safe, but
`VeroScriptSession` itself is not internally thread-safe. Hosts should define
when language changes are allowed so that shared processes do not rewrite the
display language in the middle of one request. See [Diagnostic localization](../../../../../../../VeroScript/Resources/Docs/User/en-US/localization.md)
for message IDs, placeholders, and English fallback rules.

## Long-lived sessions

`VeroScriptSession` is intended for an editor, game, or host tool that owns scripts across multiple runs. A session can:

- load, run, reload, and clear modules;
- register native functions, host types, fields, properties, methods, operators, and generic host-type templates;
- install output, diagnostic, call-interceptor, and mutation callbacks;
- select VM, mixed, or JIT execution and configure bytecode caching;
- set cooperative instruction/time limits, cancellation, and standard-library
  capabilities;
- opt into state-preserving hot reload explicitly.

Session calls are not internally synchronized. The host should use one controlling thread for `Load`, `Run`, `Reload`, and `Clear`, and request reloads while execution is inactive.

A typical long-lived session is:

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptSession.h"

#include <iostream>
#include <string>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::VeroScriptSession session(host);

	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	std::string configError;
	if (!session.ApplyConfig(config, &configError))
	{
		std::cerr << configError << '\n';
		return 1;
	}

	const VeroScript::FRunResult load = session.LoadFile("main.vs");
	if (!load.bSuccess)
	{
		return load.ExitCode;
	}

	const VeroScript::FRunResult run = session.Run();
	return run.ExitCode;
}
```

<a id="apply-config-result"></a>

`ApplyConfig` returns `false` on a rejected configuration and can report the reason through `OutError`. Check it before loading or running. Validation rejections occur before configuration is committed; allocation or callback-copy exceptions can propagate after partial changes, so they do not promise rollback.

`IEngineHost` must outlive the session and receives `print` and diagnostic-line
callbacks. Complete `RegisterNative`, `RegisterHostType`, and
`RegisterHostMember` registration before `Load` so the compiler can resolve
host declarations. The registry survives `Reload` and `Clear`; read it through
`GetBindingCatalog`, or export it with `ExportBindingsToFile` for IDE, LSP, and
DAP consumers.

The session also exposes `CompileFile`/`CommitCompiledFile`,
`ReloadCompiledModule`, `LoadPackage`, and `ReloadPackage`. These let editors,
package loaders, and hot-update systems separate compilation from commitment.
Verify a package's `CanonicalSignedBytes` before crossing a trust boundary.

`BuildPackage`/`ReadPackage` and Session `LoadPackage`/`ReloadPackage` operate on Core images (`.vslib` or a raw compiled application image). A standard ZIP `.vsapp` is an outer application container; use the Application service and `LoadApplication`/`ReloadApplication` for that format. Do not save a raw Core application image with a `.vsapp` suffix and treat it as a standard container.

## Binding boundary

Scripts can call only host functions or members that the host has registered and the compiler can resolve. Binding descriptions must provide parameter and return information, together with host-type, container-element, or key/value type information. Duplicate names, null callbacks, and conflicts with built-ins are rejected.

Host-struct value copies require an appropriate clone callback. Host-object handles follow the lifetime and ownership rules established by the host registration. The complete binding API is defined by the public `VeroScriptCpp` headers.

The public call and extension surface also includes:

- `CallFunction`, `CallMethod`, `CallMethodBypassingInterceptor`, `ConstructObject`, `GetField`/`SetField`, and global access;
- write-back of mutable `ref` arguments, with no write-back on failure or exit;
- `RetainCallable`/`InvokeCallable` for keeping a script function value beyond a native callback;
- `SetInstanceCallInterceptor`, `SetMutationCallback`, and `ApplyClassDefaults`;
- `RegisterGenericHostTypeTemplate`/`InstantiateGenericHostType`, type aliases, external serialization, and object-reference enumeration;
- `SetJitPolicy`, `SetJitCacheLimits`, `GetJitCacheStats`, `GetLastJitStats`: Reuse requires a selected factory, a non-VM effective policy and `FJitPolicy::bReuseSessionCode`. Eligible `Run`, `CallFunction`, `CallMethod` and retained-callable invocations share a generation cache and compile quota. Zero `MaxCompileAttempts` or `MaxCachedEntries` means unlimited, not disabled. Persistent sessions and stable callbacks may reuse code; native catalogs must publish a runtime revision through `IHostBindingCatalog::GetRevision()`. Governed native execution can reuse code when the backend supplies all required governance capabilities; the Session preserves that capability context across VMs. Configuration, policy, backend, module or binding changes invalidate retained code; failed reload preserves the previous program. Defaults are 8 MiB executable and 32 MiB retained payload. `GetJitCacheStats` reports settled generation accounting; `GetLastJitStats` describes the most recent invocation, not process-wide memory.

`VeroScriptSession` is not internally thread-safe. Use the creator thread for ordinary session calls and final destruction. Host callbacks may run on VM worker threads, so synchronize external state. By default, `RetainCallable` sets `bOwnerThreadOnly = true`: invoking the retained callable on another thread fails, even if the host serializes access. To permit cross-thread invocation, explicitly pass `false` as the second argument to `RetainCallable` and externally serialize all access to the session, including execution, configuration, reload and destruction.

Script cleanup is an active Session operation too: clearing or replacing a program, disabling hot reload, releasing a retained callable, and destroying the Session may invoke script destructors and host callbacks. These callbacks must not reenter APIs that execute or mutate the same Session. `RetainCallable` also rejects new roots during cleanup. Schedule further Session work after cleanup returns; ordinary execution callbacks may still retain callable values.

## Native JIT observability

`FJitStats::CostModelVmFallbacks` is the subset of `Fallbacks` deliberately
left on the VM because a valid compiled path is predicted to be slower. It is
not a compilation failure or a semantic change. `FJitStats::NativeTypedLoopExecutions`
counts native typed-loop kernel invocations, including invocations that report a
source-visible runtime failure. It is not a success counter.
Both are per-most-recent-run observations; zero does not prove that a target
or feature is unavailable.

<a id="threads-and-hot-reload"></a>

## Threads and hot reload

The host is responsible for coordinating thread boundaries between VM execution, session control, and native callbacks. Hot reload is opt-in. Compatible closures, including their captured `this`, can migrate when function identity and capture names and types remain compatible. Added or removed captures, changed capture types, functions that cannot be remapped, active channels, and incompatible type changes are rejected. Host-retained callable handles expire when their registration generation is successfully replaced; cleanup preserves graphs shared with live globals or value roots. Native side effects remain outside the script-state transaction.

With state-preserving hot reload enabled, compatible globals, objects, and
container identities can migrate; added fields receive language defaults. A
reload compiles, validates, and migrates before committing, and initialization
failure leaves the last good module available. Disabling hot reload or calling
`Clear` releases retained values normally, including reference cycles. Hot
reload skips the bytecode cache so `Reload` recompiles current source.

For the stable cross-language C ABI, see [Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md).
For browser and WebAssembly in-memory entry points, see [WebAssembly/WASM SDK](../../../../../../Platform/WebAssembly/Resources/Docs/User/en-US/index.md).

## Retained byte quotas

Zero `MaxNativeBytes` or `MaxRetainedPayloadBytes` permits no retained bytes of that category; these byte limits differ from the unlimited zero policy budgets.

## Explicit JIT backend selection

Without an installed module environment, Core and the C++ SDK do not select a concrete JIT implementation automatically.
Use `LoadJitBackend` from `VeroScriptJitBackendLoader.h` to load a backend file or
directory, then assign the shared factory to `FEngineConfig::JitBackendFactory`.
Sessions accept `SetJitBackendFactory` or `SetJitBackendPath`. An empty path removes
the backend; a failed load preserves the current backend and cache.

Stream `RunSource` / `RunFile` overloads can accept a JIT factory followed by a VM factory; shorter overloads do not supply an interpreter. Use `FStreamEngineHost` with `FEngineConfig` for path-based selection or module-environment defaults, following [Service backend selection](../../../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection). `Vm` and the interpreted parts of `Mixed` need an available VM factory; explicit `Jit` also needs a JIT factory. A configured host can resolve these from its module environment or receive explicit factories. Debugging and state-preserving hot reload require a suitable VM. Static Core AOT can use `Mixed` without a dynamic JIT factory, but interpreted initialization and fallback still require a VM.

Installing a debug bridge releases retained Session JIT code. If the bridge later
returns no debugger, subsequent calls can compile again with the selected factory.
Disconnecting does not cancel the script or destroy the Session. Core and C++
public layouts have changed; rebuild native consumers with matching SDKs.

## Cooperative cancellation

Configure a shared `FExecutionControl` through `ApplyConfig` or
`SetExecutionControl`. `Session::RequestCancel()` may run on a protocol thread,
including while the owner replaces that control at an idle point. It cancels a
shared snapshot of the current control; null is a no-op and cancellation is not transferred to a different control. `GetExecutionControl()` is an owner-thread borrowed reference.
Cancellation is cooperative and cannot interrupt a native callback. Reset the
token before reusing it. Await cancellation callers and execution before
destroying the Session; disconnecting a debugger remains a separate operation.

Sessions sharing the same `FExecutionControl` token share its cancellation
state. Complete prior cancellation callers before `Reset()` and reuse, so an
old request cannot cancel the next execution that reuses the token.

## Managed memory in a C++ host

Include `VeroScriptMemory.h` for the Core `Memory::*` functions.
`FEngineConfig::MemoryLimits` applies to both configured `RunSource` and
`RunFile`. `MaxBufferBytes` limits one owned buffer's capacity;
`MaxRetainedBytes` limits the shared budget for owned and borrowed regions.
The per-region limit also applies to an external region's length.
Zero means unlimited for these two limits.

A Session owns one memory context across execution VMs. Use
`GetMemoryContext()`, `SetMemoryLimits()` and `GetMemoryStats()` on the
owner thread. Changing limits requires idle execution; a rejected reduction
leaves the old limits intact. A rejected memory limit in `ApplyConfig` also
leaves the other execution settings intact. Workers share the budget and use
their own thread domains. Inside a native callback, use the borrowed
`FNativeCallContext::MemoryServices` for that execution's context, work checks,
binding catalog and argument addresses. Do not retain these services after
the callback. `Capabilities.bAllowMemoryAddress` governs address access;
a raw address does not keep bytes alive. Before exposing an address through a
custom native callback, check `MemoryAddressesAllowed()`; direct host calls to
Core memory functions remain the host's responsibility.

`Clear` and reload preserve the context. Compatible hot reload retains region
identity; failed replacement initialization restores the old program.
Session destruction runs script cleanup before closing the access domain.
Do not close the borrowed context manually or retain its reference after
Session destruction. An acquired lease retains bytes until
`Memory::ReleaseLease`, including after Session destruction; release is
thread-safe, but the host must finish or synchronize native access before
releasing it. An external region's owner and release callback data must
outlive all region aliases and leases, and its callback must not throw.

See the SDK `CppMinimal/memory.cpp` example and the Core
[memory reference](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md).
Runtime memory operations and region types are unsupported in standalone C++
AOT output; the emitter reports an error. An unused `memory.vs` or `core.vs`
import remains supported.

Passing `false` to `RetainCallable` does not transfer the Session memory domain.
Even with externally serialized access, region operations still require the
Session owner thread; a callable that accesses a region on another thread fails
with `diag.memory_wrong_thread`. Script workers use their own memory domains.

## Module public macros

`Defines` and `PredefinedMacros` seed each module independently. Engine and Session use the core module loader: explicit imports expose `#define_export` macros, and `export import` forwards them. Ordinary `#define` remains private; `ImplicitImportPaths` exposes declarations only. `SourceOverlays` follows the same rules. See the [preprocessor reference](../../../../../../../VeroScript/Resources/Docs/User/en-US/language-basics/preprocessor.md).

After editing a macro dependency, reload a file Session with `Reload()` and run it again to complete initialization, or load the updated source with `LoadSource()`. With hot reload enabled, `Reload()` migrates compatible global state; `LoadSource()` replaces the module and resets its state. The public macro interface changes the bytecode layout: rebuild deployed `.vslib`/`.vsapp` files with this core, and rebuild native consumers against the matching Core and C++ SDKs.

## Compile-time annotation processing

Configure Engine and Session with `FEngineConfig::AnnotationProcessing`; Session also exposes `SetAnnotationProcessing`/`GetAnnotationProcessing`. Freeze the processor registry with `Freeze()` and keep it immutable. Factories create independent processors for each declaration preparation. Keep their native libraries and callback captures alive. Configure `FCppCodegenBackend` through `FCodegenOptions::AnnotationProcessing` for C++ AOT.

Processors run synchronously on the compiling thread. Preparation callbacks must not execute or mutate the same Session; those reentrant operations are rejected. Cancellation propagates the core `FAnalysisCancelled` exception. Cancellation or compilation failure preserves the active program. `FRunResult::Diagnostics` retains Warning/Information/Hint and the error stream also reports them; nonfatal diagnostics do not turn success into failure. `RunSource`/`RunFile` results include compilation and execution diagnostics.

Use `ReadDependency` for external inputs that affect generation. Cache hits still prepare declarations first; processor identity, configuration, generated content and dependencies participate in the cache key. Set `Cacheable=false` for processors whose results cannot be reproduced. Custom budgets also apply to imported libraries containing generated declarations. See Core's `VeroScriptAnnotationProcessing.h` and `VeroScriptProgramPreparation.h`, and the buildable SDK consumer in `CppMinimal/main.cpp`.

## Host console integration

`IEngineHost` can implement `QueryConsole` and `ExecuteConsole` using the types in `VeroScriptConsole.h`. Defaults report no terminal capabilities. `HasConsoleTextOutput`/`WriteConsoleText` opt in to exact text without a newline; `FlushConsoleText` completes pending text. `FStreamEngineHost` forwards these operations to its backing stream. Callback hosts must synchronize worker calls and remain alive through cleanup. The VM restores changed style/visibility at execution boundaries. Terminal state is shared: coordinate complete color/text/cursor sequences across tasks.

The terminal interface adds virtual methods. When upgrading, recompile the host and related native extensions using Core/C++ SDK libraries from the same build. If using JIT, deploy the matching backend as well.

## General compile processing

Engine and Session forward `FEngineConfig::CompileProcessing`; Session also provides `SetCompileProcessing`/`GetCompileProcessing`. C++ AOT uses `FCodegenOptions::CompileProcessing`. Freeze the `FCompileProcessorRegistry` before compiling. Each compilation owns its processor instances across `RawSource`, `ActiveSource`, `Types`, `Members` and `Bodies`; prepared AOT emission reuses accepted generation. Callbacks run on the compiling thread and cannot reenter the same Session.

Use `ReadDependency` for generation inputs and `Cacheable=false` for nondeterministic processors. Generation, dependencies, annotation definitions, comment retention and budgets enter the bytecode cache identity; processing still runs before a cache hit. Failed or cancelled replacement keeps the active Session program. `CompileProcessing` and the legacy `AnnotationProcessing` registry share core transactions and budgets. For annotation-only hosts, raising the legacy operation/source budget also raises the unchanged general default; an explicitly configured general registry or budget remains authoritative.

Annotation names are case-sensitive by default. To accept ASCII case variants, pass `false` as the final `RegisterAnnotation` argument or set `FAnnotationDefinition::CaseSensitive=false` in `CompileProcessing.AnnotationDefinitions`. Session uses its binding registry, an immutable module environment uses its published catalog, and C++ AOT accepts the catalog through `FCodegenOptions::Bindings`. The policy governs processor subscriptions and runtime reflection; `annotation_info.matches_name()` and `annotation_definition_info.case_sensitive()` expose it. Source spelling is preserved. Argument names and metadata keys remain exact. `@` is significant for `matches_name()` and `find_annotations()`; `has_annotation()` also accepts names without `@`. Overlapping sensitive/insensitive registrations are rejected, and policy changes invalidate bytecode caches. These reflection queries require the Core runtime (VM, Mixed or hosted JIT); standalone C++ AOT rejects them.

`CommentRetention` controls retained source comments; runtime annotation/comment queries are available through the VM. Rebuild native consumers and binary script packages against matching Core/C++ libraries after this source-metadata format change. See `VeroScriptCompileProcessing.h`, `VeroScriptSourceMetadata.h` and the SDK `CppMinimal/main.cpp` example.

When upgrading Core/C++ SDKs, use a fresh build directory or rebuild all native objects. With localized MSVC and Ninja on Windows, run `chcp 65001` in the Developer Command Prompt before both CMake configuration and builds, so the detected include prefix and compiler output use the same encoding. Otherwise header changes can go untracked by Ninja.

## Script compile extensions

`FEngineConfig::ScriptCompileExtensions` configures compiler scripts; Session exposes `SetScriptCompileExtensions`/`GetScriptCompileExtensions`. Configure `Roots`, optional `RegistrationEntry`, `Configuration`, source overlays and standard-library paths before loading. Source `import compiler "builder.vs";` is enabled by default; `bAllowSourceImports=false` disables that entry path. Deploy the matching `compiler.vs` with the SDK.

Extensions run in an isolated VM on the compiling thread, preserving state across phases. Generated business code runs in the selected backend. Business bindings are not automatically available to extensions; explicitly provide compiler-only `Bindings` and keep them alive throughout compilation. Compiler callbacks must not execute or mutate the same Session. Bound native callbacks remain under host authorization.

A null extension `ExecutionControl` inherits the Engine/Session runtime token. In Session it follows `SetExecutionControl`; an explicitly supplied token remains independent, even if initially identical to the runtime token. Setters affect subsequent compilation/reload. Extension `MaxInstructions`, `TimeoutMilliseconds`, path and bridge limits are separate from business execution limits; cancellation is cooperative and cannot interrupt native callbacks.

Extension identity, configuration, sources, generated results and declared dependencies participate in cache validation. Read generation inputs through compiler dependency services; additional native bindings disable reuse unless explicitly declared deterministic with a stable `BindingConfiguration`. A failed extension compilation/reload preserves the active Session program. Rebuild all native consumers and binary script packages with matching Core/C++ SDK libraries after upgrading. The SDK `CppMinimal` consumer exercises Session and AOT extension paths.

`GetScriptCompileExtensions` returns the configured options, retaining a null inherited control. Reading options, changing unrelated fields and setting them back preserves inheritance.

## Module environments and native extensions

Set `FEngineConfig::ModuleEnvironment` to an immutable `FModuleEnvironment::Create` snapshot before checking, loading or running. Engine and Session use its module mapping, dependency scopes, resources, compiler policy and native contracts. A mapped memory module can be passed to `CheckFile`/`RunFile`/`LoadFile` using its canonical `veroscript-module:/...` path. Imports are authorized by the snapshot; installation does not import declarations automatically.

When the environment supplies `Bindings`, `GetBindingCatalog`, binding export and the Session debug view expose that catalog. Build combined bindings before publishing the environment, using one staging registry (or the base registry of `FNativeExtensionRegistry`) to preserve type IDs. Session rejects conflicting local bindings and subsequent registration; `GetBindingRegistry` throws `std::logic_error` while the published environment owns the catalog. An environment without a catalog can still use local Session registrations.

An environment cannot be replaced while a Session has a loaded program. Finish execution, call `Clear`, then apply the new configuration and load again. A configuration rejected by validation preserves the prior program and settings; a compilation failure preserves the active program. Exceptions from `ApplyConfig` follow its [configuration result contract](#apply-config-result). Callbacks and escaped native objects retain their extension code; release tasks, callable handles and objects before unloading host resources. Load native libraries through the explicit `VeroScriptNativeExtensionLoader.h` API; rebuild consumers and native modules with the matching Core/C++ SDK and shared CRT. JIT backends retain their separate loading protocol.

The current environment compilation path uses Core preparation directly and does not use Session/Engine's persistent bytecode cache. Pure script AOT supports the snapshot; programs requiring runtime native package contracts are rejected until a target runtime adapter is provided.

`GetModuleEnvironment` retains the current snapshot; the debug view's borrowed `ModuleEnvironment` exposes mapped memory sources and provider contracts. Explicit source overlays take precedence. These describe compilation inputs, not a frozen copy of the loaded main source; the host still publishes actual `LoadSource` revisions.

## Exact numeric APIs

SetNumericLimits/GetNumericLimits on Session configure FNumericLimits. VeroScriptNumerics.h supplies immutable FBigInteger/FRational and checked Numerics::Try* operations with FNumericOperationContext (Limits/Work). Zero chooses finite defaults; work/cancellation, errors and unchanged failure outputs remain explicit. Standalone source AOT currently rejects Core exact-numeric operations.

## Bounded cleanup after interruption

<a id="bounded-interruption-cleanup"></a>

An instruction limit, timeout or cancellation ends ordinary execution. Core runs entered scopes’ `finally` blocks and required script destructors with a separate shared reserve of **16,384 logical instructions and 50 ms**. Synchronous nested calls and temporary cleanup VMs share this reserve. The original exhausted limit or cancellation token does not immediately reject cleanup; existing permissions remain in effect. Cleanup cannot spawn tasks, wait for console input or replace the original failure with `exit`.

The first termination remains the primary error. Secondary failures appear in `FRunResult.Diagnostics` and `VeroScriptVm::GetCleanupDiagnostics()`. After successful cleanup, the host may remove limits or reset the cancellation token and call again. Business changes are not rolled back.

Incomplete cleanup, an uncaught cleanup failure or exhaustion of the reserve makes `VeroScriptVm::RequiresReset()` and `VeroScriptSession::RequiresReset()` true. If interruption occurs inside an already-running finally or destructor, incomplete cleanup is not replayed and requires reset. Further execution and retained-state operations fail with `diag.execution_state_reset_required`. Call `Session.Clear()` and load a fresh program. Clear abandons unfinished script cleanup in that unusable state while releasing native ownership; it does not reset the host cancellation token. Discard standalone VM and retained Core state before reuse. The deadline is cooperative and cannot forcibly interrupt a blocking host callback. VM, Mixed, JIT and applicable AOT paths share this Core contract. If a failed reload leaves restoration incomplete, or external roots prevent `Clear()`, check `RequiresReset()` and `IsLoaded()` after Clear and recreate the Session when it cannot reset. Do not resume execution in the unusable instance.

## Session backend selection arguments

`VeroScriptSession::SetVmBackendPath` / `SetJitBackendPath`

These Session setters accept a compatible file/directory or an installed backend ID when the supplied module environment provides its resolver. `auto` requests environment candidates; for VM it may use the retained product fallback. `none` or an empty native string disables selection; `""` map to that empty request. On a new session, omitting a backend setting allows environment selection; it differs from explicitly clearing that setting. When reconfiguring an existing session, omitting the VM selection preserves its execution domain. A failed explicit selection preserves the previous backend. Set on the idle creator thread; VM changes must precede execution because the domain remains frozen after Clear/reload. JIT selection remains independent of VM/Mixed/JIT execution policy.

Unified automatic, explicit and disabled selection rules are documented in [Service backend selection](../../../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` supplies `FallbackVmBackendFactory`; explicit `VmBackendFactory` / `VmBackendPath` take priority. A Session preserves the product fallback so `SetVmBackendPath("auto")` can restore it before execution.

## Persistent execution and reload settlement

Ordinary functions in a persistent Session can use its selected Mixed/JIT backend. Retained initialization ranges, debugging and bounded cleanup keep the VM path. Unsupported native operations still fall back before entry; instruction, time, cancellation, permission and call-depth rules are unchanged.

Supported intercepted method bodies also use backend capability negotiation; nested calls through `Proceed` retain interception, reference write-back and execution controls. Interceptor errors without a host-supplied location use the call site; explicit locations and original body errors are preserved.

Directly forwarding the original `Proceed` result preserves its existing value-return copy boundary, including structs, native value types and fixed arrays. A substituted or borrowed result still receives ordinary type validation and value copying. Methods declared to return a reference retain reference semantics.

Stable native bindings and callbacks can reuse generation code. `IHostBindingCatalog::GetRevision()` publishes a runtime binding revision; zero keeps custom catalogs with native callbacks ineligible. `FHostBindingRegistry` updates it automatically. Use `InvalidateRuntimeBindings()` when external changes affect callback code or lifetime ownership. Ordinary mutable data read by a callback needs no revision. Do not mutate a catalog during execution. Session callback setters invalidate retained code; each invocation receives its current callbacks and services.

`RetainMigratableValue(value, root, error)` explicitly joins a host-held graph to the ordinary global migration graph. Compatible object, container, alias, cycle and closure rules are shared. `root.Read()` returns null while replacement initialization is pending and resumes after a successful commit or completed rollback. Copied raw handles must not be accessed during staging and are not automatically relocated. Ordinary roots still block replacement, generation roots still expire, and Load/Clear require migratable roots to be released. Source reload requires `SetHotReloadEnabled(true)`; transactional package/application reloads also use this migration service.

Read `GetReloadStatus()` on the owner thread. `OperationId` is independent of `PreviousGeneration`, `CandidateGeneration`, `CurrentGeneration` and `CommittedGeneration`: rollback can reuse a candidate generation number. `Rejected` leaves the current program untouched; `Pending` means staged; `RolledBack` means restoration completed; `Applied` means initialization committed; `AppliedWithErrors` means later business execution or retiring-value cleanup failed. `ResetRequired` agrees with `RequiresReset()` and forbids further execution; recreate the Session if rollback or cleanup cannot complete. The snapshot describes the most recent accepted reload request. Script rollback does not undo native host side effects.

Core, C++ embedding and Application source support C++17 and C++20 UTF-8 paths through `PathUtil::FromUtf8` / `ToUtf8`. Keep normal `char8_t` support enabled. Rebuild native backends and modules against the matching Core implementation even when product version strings match.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](../ja-JP/embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](../zh-TW/embedding.md)
