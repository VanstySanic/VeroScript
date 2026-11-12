# Application i18n runtime contract

The core owns values, atomic context roots, immutable provider snapshots and normal native-call diagnostics. Application i18n never mutates the diagnostic locale. ICU, resource file formats and GUI refresh are external responsibilities.

## Public API

- [`VeroScriptI18n.h`](../../../../Public/VeroScriptI18n.h): `FI18nArgument`, `FI18nArguments`, `FI18nConfiguration`, `FI18nText`, `FI18nContext`, `FI18nSnapshot`, `FI18nVersion`.
- `CreateContext(config, services, out, error)`; `CaptureSnapshot()`; `TryLookup`, `TryResolve`, `TryFormat`; `ToScriptValue` / `FromScriptValue` validate core wrapper identity.
- `FI18nServices::Publish` atomically publishes both immutable views. `PublishResources` / `PublishFormatter` preserve the latest other view. `Acquire` returns an owned snapshot. Use a fresh view when resource/config contents change. Re-publishing the same view with an omitted owner preserves its existing module pin; owning alias-null shared pointers are valid pins.
- `ConfigureAndSetServices` atomically swaps configuration and the service reference. It does not transactionally mutate a service shared by other contexts. `Set*` methods merge against the latest other fields.
- Resolver: one exact normalized language per call, `Found` / `NotFound` / `Failed`. Empty found templates are valid. Only core performs the explicit fallback chain.
- Formatter: immutable, concurrently callable, `SupportsFormat` and `Format`; request carries original typed arguments, requested language, matched language, effective format locale and typed options. Reserve `literal` / `vero.named` for core.
- `FI18nWork`: borrowed current-operation check and output limit (zero unlimited). Check cancellation during long provider work and enforce output budget before allocating output. Core checks before/after callbacks but cannot forcibly interrupt a blocking native call.
- Values and snapshots own their data. No retained callback context, VM, borrowed request views or `FI18nWork`. Mutation of one ordinary native value is externally synchronized; context/service methods are thread-safe. Custom values and providers must be immutable and safe for concurrent reads.
- Provider/custom module owners outlive callbacks and virtual destructors; pass an explicit owner for dynamically loaded code. Do not form context/service → provider → context/service strong cycles. Use weak references.
- Same-context `Try*` recursion is rejected per thread. Configuration reads and updates remain possible from a provider; the active operation pins its starting snapshot and text value even if a callback replaces or resets the caller’s value variables. Borrowed string_view inputs must remain valid until return; the error output must not own the input strings. Do not reenter an executing VM.
- Version equality includes context identity, configuration revision, resource revision and formatter revision. Revision exhaustion fails; there is no wrap or persistent/interprocess meaning.
- Structured failure: `FI18nError::Status`, owned `Diagnostic`, domain/key/language/name. Resolver `NotFound` must have no error; failures require a non-Ok status and diagnostic. Successful callbacks must not return an error.
- `FNativeCallContext::DiagnosticOutput` is borrowed output for owned `FScriptError`; VM and JIT bridges preserve ID/args and apply the current call site. `I18nServices` is borrowed current-call service access. Script-created contexts capture the owning service selection, not the VM.
- Configure `VeroScriptVm::SetI18nServices`, or C++ Engine/Session `FEngineConfig::I18nServices` / `I18nLimits`. Workers inherit selection and budget, and use their own execution controls. Existing contexts keep their service reference after a host configuration change.

## Compilation and execution

Parser and compiler consume the common `StandardLibrary::GetBindingCatalog`; canonical memory entries precede i18n. The existing core-relative opcode retains its internal name `CallMemory`, now indexes this common catalog. No new opcode or `FValue` alternative is introduced. `Memory::UsesRuntimeMemory` excludes i18n entries. Bytecode/package loading checks the same catalog; live context/provider/custom payloads are runtime values and are not serialized into a library.

AsmJit declines unsupported core-relative calls before entry, allowing the established VM fallback without replaying a callback. Standard values use ordinary host clone/equality protocols; general value equality does not require a key hash. Map/Set key equality still requires a valid hash protocol.

Ordinary named host methods support parameter-type overloads. Script APIs retain `text::message_key(domain,key)` / `text::message(domain,key,args)` and `format` / `format_with`; typed formatting does not implicitly call the string-only positional `std::format` implementation.

## Validation

`VeroScriptI18nTests` covers scalar precision, owned/COW data, providers, versions, concurrent publication, reentry, cancellation/output limits, script generics/containers/Attribute, worker inheritance, bytecode/library round trips, VM/Mixed/JIT and diagnostic bridges. The JIT portion runs when a compatible backend is provided. `VeroScriptI18nExample` links Core and a matching Bytecode VM SDK.

[I18n SDK example](../../../Examples/I18n/README.md) · [User API](../../User/en-US/standard-library/i18n.md).

<!-- docs-navigation -->

---

[Developer documentation](index.md) · [en-US](i18n-runtime.md) · [fr-FR](../fr-FR/i18n-runtime.md) · [ja-JP](../ja-JP/i18n-runtime.md) · [ko-KR](../ko-KR/i18n-runtime.md) · [zh-CN](../zh-CN/i18n-runtime.md) · [zh-TW](../zh-TW/i18n-runtime.md)
