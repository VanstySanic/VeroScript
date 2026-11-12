# GUI maintenance

`#include "module.vs"` follows the same demand policy as `export import "module.vs";`; the alias alone does not load the framework. Binary include aliases retain public-macro inspection. In root builds with GUI enabled, ordinary Core example and package-client regressions use the test-only `VeroScriptCliCoreTests` host. GUI integration scenarios continue to use the shipped CLI and cover its conservative macro/generator policy separately, without extending Core test deadlines.

Gui owns widget/property/event semantics, scope/Schema generation, ML/CSS compilation and lifecycle. Platform/Graphics/Image/Font are public services; RmlUi/Stb/FontBackend are separate providers. Preserve the module environment, generation-scoped roots, owner-thread cleanup, type/value template arguments and compile identity. Test JIT dispatch and an explicit cost-model VM fallback separately; unsupported-only fallback is not acceptance.

Build through this module’s standalone CMake entry or the root VEROSCRIPT_BUILD_GUI option; enable VEROSCRIPT_GUI_SCRIPTING for compiler/script integration. VEROSCRIPT_GUI_USE_VISUAL_SDK reuses installed foundations; VEROSCRIPT_GUI_BUILD_RESOURCE_BACKENDS controls bundled Stb/FontBackend. Rebuild against matching Core/C++ Runtime SDKs after ABI or bytecode layout changes. Validate native lifecycle, scripts, schema, ML, installed consumers and actual graphics/input/font/platforms separately. Do not treat static review as runtime or IDE acceptance.

Script hosts explicitly select a VM through `FEngineConfig::VmBackendFactory` or `VmBackendPath`; markup scripting uses `FMarkupScriptOptions::Engine`. GUI forwards the host selection to execution and compile processors while preserving existing configuration. The official bytecode VM is the independent `vs-bytecode` provider; linking or installing Gui does not select it. Native widget trees and plain ML/CSS parsing need no VM.

Script regressions are registered as independent scenarios. Run `ctest --test-dir BUILD -R VeroScriptGuiScriptTests --parallel 4 --output-on-failure` to cover them all. Running VeroScriptGuiScriptTests directly defaults to all scenarios, or accepts one scenario name. Splitting scenarios preserves the existing assertions; test timeouts do not change application execution deadlines.

Core may keep getters and forwarders in the VM for cost, reference or finally semantics. JitSynchronization reports NativeLeafExecutions / NativeCfgExecutions separately from dispatch, Linear/Bridge/VM-trampoline entries and fallbacks, then verifies a supported native entry and per-invocation counters across reused Session calls. ScalarNativeFastPathCalls counts host scalar calls, not all native script execution; nested counters must not be summed as independent calls. Detailed diagnostics stay disabled by default; enable FJitPolicy.Diagnostics only for a separate diagnostic run and read GetLastJitDiagnostics(), not during throughput measurements.

`VeroScriptGuiMarkupTests` covers parsing, diagnostics, selectors/states, scopes, variables/themes and relative dependencies. Optional `VeroScriptGuiScriptTests` runs real VS, including external/inline/component state, property dependencies, std::attribute and module environments. The GPU integration test checks ML/CSS layout, offscreen colors and hover changes. A direct CMake installation includes `Resources/Examples/Markup` under the module share directory; release packaging removes authored examples from functional SDK payloads and publishes them through Resources documentation. See also the [framework guide](../../User/en-US/Reference/Framework.md).

`ApplicationOptions.FontBackend` and `TextLayoutBackend` select independent services. Empty fields inherit `FApplicationFactories.DefaultBackend` or the host/CLI choices (`--font-backend`, `--text-layout-backend`); explicit `auto` selects a candidate, while `none` disables the optional service. Only when a native host supplies no default for that service does `FApplication::Create` use `font-backend` as its conventional fallback. Supply `FApplicationFactories.Font` / `TextLayout` or native `Fonts` / `TextLayouts` instances. Live RmlUi contexts share immutable instances; registered factories use weak caches. Close contexts before changing configuration. `FTextLayout::Create` receives the selected layout service first, and paragraph caches include its instance identity. Rebuild matched providers and consumers for GUI `0.1.1`, Font `0.1.2` and text-layout `0.1.0`; `ContractVersion` references the Gui `Version`. Static review does not establish runtime acceptance.

## Regression and lifetime details

Tests cover direct activation, numeric focus-loss commits, nested scrolling, Tab wrapping and floating focus.

Check actual JIT dispatch/fallback statistics; measure warmed update and render times separately from compilation and presentation waits.

Context.Post retains callbacks through Core's generation-scoped value roots; completion or discard retires captures for cleanup at a Core safe point. Context.SetFocus preserves the active script invoker for synchronous control notifications.

[User](../../User/en-US/index.md) · [Standards](../../../../../../../Resources/Docs/Standards/en-US/GENERAL.md)

## Source import policy

Ordinary CLI setup retains native bindings and loads the script framework on source demand. GUI names, annotations and generated ML request it; comments and string literals alone do not. Source imports ending in .vs use the same detection. Small local .vslib imports inspect their public macros through tracked dependencies; ordinary declarations and parameter-free, literal-only macros do not request the framework. Simple numeric, character and string constant macros and ordinary conditional directives alone do not request it. Parameterized source/host/imported-library macros and replacements containing names, token pasting, stringification, unknown directives, opaque implicit providers used with host macros, unresolved/escaped/logical/large imports and custom compiler processors retain the conservative full import. Explicit gui-* tasks and ConfigureGuiScriptEngine retain the implicit import. Keep this policy consistent with compile metadata and semantic-cache identities; regression coverage must include sources using no GUI and sources introducing GUI through imports or processors.

For fixed-property synchronization, capture input field revisions before the callback and commit them only after a successful callback with the same registration. Changes during evaluation remain visible to the next round; failures and re-registration cannot commit an obsolete snapshot. Reuse the input buffers, and clear a retained default configuration only when switching away from it. Dynamic getters still execute in every synchronization round.

`BakeGuiApplication` remains the inner Core image API for embedding. The CLI wraps its bytes with `Application::BuildApplication` and `WriteArchive`; it never writes the image directly as `.vsapp`. The application preparation hook restores GUI resource services after package providers are available. Ordinary CLI and GUI-enabled Runtime builds share this path. Test ZIP structure, inspect/check and source-free relocated execution together. Without Application support, GUI source checking/preview remains available, but container compilation/loading reports the missing capability.

For source `import compiler` generators, the Core extension deadline also covers preparation of the containing GUI source graph. Configure a suitable host budget for larger graphs, for example CLI `--compiler-timeout 20000` or `FCompileOptions::ScriptCompileExtensions.TimeoutMilliseconds` in a native host.

Stable input identities and revisions may reuse the verified pre-callback snapshot without rescanning fields. Source classification is reusable while configuration is unchanged; getter results are not reused across synchronization rounds. Exact numeric framework controls may configure native state from the properties just synchronized in their own native callback, outside getters. Manual synchronization, derived controls and recursive queries retain the regular getter path. Inherited values still evaluate their parent getters each round; unchanged validated values avoid copying and repeated validation. Native attribute dispatch tests active mask groups without changing getter order or omitting active bindings.

Native numeric, Boolean and string attributes use direct value snapshots and reference output during both polling and property queries, avoiding temporary optional objects. Empty results retain fallback behavior and are polled again in the next epoch. Exact framework numeric controls may omit a generated empty change hook under the verified native scope; derived controls and declared hooks retain virtual dispatch.

Fixed native attribute configurations do not require periodic script polling. Configuration changes, native mutations and remounting still wake synchronization; bound and optional getters continue polling, including after an empty result.

Each widget owns independent native cache objects. Internal snapshot updates therefore leave input configuration tracking stable; setters still wake synchronization when replacing a configuration.

The exact base Widget can also suspend synchronization. Unannotated subclasses keep the conservative custom-synchronization path.

Attribute configuration classification is cached only until Set/Reset replaces that configuration. This classification does not evaluate a getter; bound and optional getters still execute in every required evaluation epoch.

Replacement retains the previous payload until the new configuration and its classification are published; destructor reentry can read or replace the new state without being overwritten afterward.

## Notification bindings and batches

The native source graph owns weak widget watches and coalesces repeated notifications. Widgets own their watch tokens and property readers; token destruction needs no script finalizer callback. A monotonic counter shared by the bridge timestamps each notification. The maximum revision of a binding’s fixed dependency set therefore detects changes exactly, without hashes or per-read script arrays. Shared Binding instances cache only successful reads, including empty optional results. Ordinary dynamic attributes retain their existing epoch behavior.

IFrameBindingService adds a publication phase after all BeginFrame calls, without changing the existing IFrameService virtual interface. Publication deduplicates widgets and wakes their existing lifecycle observer; it does not invoke per-property script jobs or force layout invalidation. The existing synchronization path reads dirty attributes, compares results, preserves lifecycle order and retries failed getters. Script-owned observers also support direct host updates. Native batch depth prevents host updates from publishing partial batches; script wrappers reject them before entering native code. Literal setters and ClearX detach watches, while internal installation publishes the replacement before releasing old captures. FromAttribute keeps fallback polling so default attributes and native styles can change independently; its supplied getter result is still cached by notification revisions.

## Synchronization output and render submission

Native synchronization reuses resolved property output only after getters, inheritance and change hooks have run. Changed values and animation sampling, stopping or completion invalidate that output. Notification publication keeps valid configuration snapshots. Each Widget owns one stable cache holder and creates individual native-property caches on first use; these output writes do not invalidate its source configuration snapshot. Ordinary dynamic attributes and FromAttribute fallback retain their evaluation and retry rules.

RmlUi uses the optional IRenderSubmission2D scope for consecutive passes on one renderer. Producer work is submitted before another renderer's compositor reads it, then recording resumes. Failures and exceptions cancel unfinished scopes. When no swapchain image is available, unsubmitted scratch-only work may be cancelled; persistent layer captures still complete. SDL_GPU and its module provider expose this capability; other renderers keep the ordinary path. OnPaint still runs on every render.

The script cache invalidation stamp advances before publishing a native notification, so an interruption after publication cannot leave a shared result marked current. Dependency revisions still determine actual changes. This ordering does not make interrupted script execution transactional.

Observable.Set publishes its notification from finally, including when execution stops after committing the stored value. A failed or interrupted assignment may conservatively invalidate cached bindings; an unchanged value still emits no notification. Recovery remains subject to Core's RequiresReset contract below.

Temporary evaluation, initialization and construction flags are changed inside protected scopes; nested evaluation and batches restore their entry depth. A failed subscription-disconnect callback remains retryable, with recursive disconnection suppressed. Recovery after a limit, timeout or cancellation still depends on bounded Core cleanup. If Core requires a reset, recreate the affected GUI script session and its binding context; GUI finally blocks do not provide transactional rollback of application state.

Native properties synchronize in nine groups in their original order, preserving dynamic evaluation and exception recovery. Weak control callbacks recheck disposal after entering script; a disposed target finishes dispatch without entering the host bridge again.

ML capture initializers are rewritten in the enclosing scope; capture aliases are local to the closure body. Source snapshots use Core's unwrapping API to compare the actual attribute object and revision behind reference or readonly capture storage. Ordinary object fields retain the direct read path, without retaining capture payloads. Callbacks still use FValueRoot and InvokeCallable; business interfaces are unchanged.

Scalar native-property synchronization checks configuration only when a read has no value, avoiding a duplicate query on the common value path. Cache and getter interfaces remain unchanged. Ordered synchronization groups reuse scalar bookkeeping while keeping independent property-value scopes. Shared Binding reads check recursion before reusing the revision cache and avoid an extra refresh call on a cache hit.

Native element lookups and style-presence queries explicitly register readonly member metadata, so Core can resolve them through const host handles and value captures. Mutating operations retain their existing metadata and the public GUI interfaces are unchanged.

## Native artifact maintenance

After native option, public layout or embedded-library changes, rebuild GUI providers, hosts, script libraries and SDK/package candidates together. A matching product version string does not establish implementation compatibility.

During native synchronization, repeated lifetime checks on the current mounted widget share the callback's native validation. That scope is cleared on success or failure; other widgets and queries outside the callback retain their native checks. Attribute evaluation, optional-binding fallback, stabilization rounds and lifecycle order are unchanged.

## Backend defaults and internal dispatch

Implementation helpers and script dispatch entry points use `VS::Gui::Internal`, including `NativeAttributeCache<T>` and `NativeValueAttributeCache<T>`. They are framework internals, not application APIs. Members beginning with `Vsgui` remain reserved for generated code and inheritance hooks to avoid application-member collisions.

Source import detection distinguishes namespace/annotation use from an ordinary variable, field or parameter named `Gui`; update the processor configuration identity when changing this detection. Keep native bindings, generated call sites and both editor metadata extractors aligned with the qualified `VS::Gui::Internal` names. `FContext` records `bLayoutNotificationPending` after changed geometry, publishes complete geometry before callbacks and clears the flag only after successful delivery. Failed and undelivered notifications remain pending; completed notifications do not repeat without a new change. Mounted-hook changes synchronize before layout; layout-hook changes synchronize on the next `Update`. Input/rendering still require successful update. Exercise failure/retry, sibling delivery and callback-visible geometry separately.

## Synchronization implementation

The static fast path requires an exact runtime type, completed initialization and no custom script synchronization. Unknown subclasses retain the conservative path. Fixed primitive configurations may suspend polling; configuration replacement, setters, style changes, host mutations and binding installation resume it. Bound and optional getters remain polled, including empty results. Weak observations must not retain widgets.

Generated primitive properties retain unboxed local/style/output snapshots. Commit change tokens only after successful callbacks; failure and re-registration must not commit stale inputs. Native mutation tokens avoid rereading unchanged control/style state without skipping dynamic getters or custom codecs. Styles cache parsed builtin literals while variable expansion, resource resolution, selector states and themes stay live. Common Builder methods are emitted with their type to reduce declaration operations without changing syntax.

During native synchronization, repeated checks for the current mounted widget share the validated callback-entry scope. Restore that scope after success or failure; other widgets and queries outside the callback keep native checks. Preserve attribute evaluation, optional fallback, stabilization and lifecycle order.

## Context fallback and runtime option routing

The default `__NativeContext` constructor bridge in `Private/Bindings/VeroScriptGuiRuntimeBindings.cpp` consults `FScriptServices.DefaultBackend("gui")`, then the resolved `Application.DefaultBackend("gui")` for an unspecified constructor argument. Recheck for an empty result before invoking `Factories.Gui` and use `rmlui` only then; preserve explicit ID/auto/none choices. `FScriptServices.CreateContext` bypasses this application-factory branch and remains host-owned. In `VeroScriptRuntimeMain.cpp`, keep VM/JIT choices in execution configuration and package `SelectedBackends`; exclude service keys `vm` and `jit` from values passed to the GUI command adapter. Review omitted callbacks, empty callback results, environment precedence, explicit choices and GUI-container check/run with VM/JIT options separately.

## Scene reuse and synchronization boundaries

Style synchronization reuses unchanged scene structure and selector matches; ordinary geometry, text and animation outputs do not independently invalidate selector matches. Resource resolution, style value decoding and dynamic getters retain their synchronization rules. Only widgets whose generated code establishes that no periodic work is required stop polling; dynamic bindings and unknown synchronization behavior retain the existing path.

`FMarkupRuntime::MatchesScene` compares theme, widgets, parents, name scopes, memberships and style sheets before reusing `FContext::SceneStyles`. Selector winners track eligibility, focus/modal/hover/pressed state and style inputs; dynamic eligibility getters disable reuse. Commit pre-application inputs so style outputs invalidate the next stabilization round. Clear retained scene state on root replacement/disposal. `CanSuspendSynchronization` remains conservative for unknown methods/constructors, foreign-generated work, codecs and old packaged bases without generated helpers. Private generated names retain canonical module linkage identity.

## GUI compilation configuration

GUI `CompilerOptions` forwards the chosen VM to script compile extensions, uses `FallbackVmBackendFactory`/`FallbackVmBackendPath` only when no explicit VM exists and `bClearVmBackend` is false, and inherits `ExecutionControl` only when the extension options did not supply it. Explicit disable and caller-specific controls remain authoritative.

<a id="gui-text-layout-maintenance"></a>

## Text-layout consumer invariants

Keep the selected service instance and immutable configuration in paragraph-cache identity together with logical text and direction; a backend ID alone is insufficient. Preserve the full logical paragraph across styled spans, choose wrapping before line-specific bidi ordering and retain module leases through resolved services. Render letter spacing once after the final glyph of each visible cluster, preserving original advances and offsets, so drawing agrees with line width and caret positions. Preserve finite negative spacing and the nonnegative final-width clamp. Keep clipboard values and offsets logical. The public shaping, cluster and capability limits belong to the [Font contract](../../../../../VeroScriptFont/Resources/Docs/User/en-US/index.md).

<!-- docs-navigation -->

---

[Developer documentation](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
