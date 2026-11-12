# C++ SDK

The C++ SDK is the native integration surface for VeroScript. It contains the
runtime embedding API and the C++-oriented source tools. The C ABI used by
Python, C#, Rust, and other adapters is documented separately in the
[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md).

## Choose a module

| Module | Use it for |
| --- | --- |
| [Embedding](embedding.md) | Check, load, run, call, reload, and configure VeroScript from a C++ host. |
| [Host bindings](bindings.md) | Describe and register native functions, host types, members, operators, and generic host types. |
| [AOT](aot.md) | Generate C++17 source from VeroScript for a native build. |
| [C++ front end](frontend.md) | Parse the supported C++17 subset into the shared public AST. |
| [BindGen](bindgen.md) | Generate a Binding Catalog and human-readable API references from C++ declarations. |
| [Transpiler](transpiler.md) | Convert the supported C++17 subset into VeroScript source and source maps. |

The front end is shared by BindGen and Transpiler. It deliberately accepts a
bounded declaration/source subset and fails closed with diagnostics when a
construct cannot preserve its meaning. BindGen describes an existing native
surface; Transpiler converts source semantics; neither tool is a general C++
compiler.

## Integration order

For a host application, use this order:

<a id="explicit-vm-selection"></a>

Before execution, select a compatible VM through `FEngineConfig::VmBackendFactory`/`VmBackendPath` or the Session selection API. Linking the C++ SDK alone does not select an interpreter. Script compile processors also need a VM. Follow the [VeroScriptBytecodeVm](../../../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/en-US/index.md) for the required SDK targets and setup.

1. Choose `VeroScriptEngine` for one-shot work or `VeroScriptSession` for a
   long-lived module and runtime state.
2. Register host functions and types before loading a module.
3. Run `CheckSource`/`CheckFile` before `RunSource`/`RunFile` when the host can
   report validation errors separately.
4. Export and validate the binding catalog when the host surface is consumed
   by an IDE, LSP, package, or static checker.
5. Treat callbacks and worker-thread execution as concurrent host entry
   points; synchronize state owned by the host.

## Thread and output boundaries

Script globals and class static fields are isolated per native worker thread.
Use `channel<T>` when the script needs an explicit synchronized sharing
mechanism. Native callbacks, instance interceptors, mutation observers, and
AOT host symbols can run concurrently when reached from workers. Keep their
captured state and `UserData` alive for the whole run and provide the required
host-side synchronization.

The AOT bundle is intentionally reconstructed as one translation unit. Its
entry source, runtime fragments, program fragments, and manifest are a source
distribution, not independently linkable libraries. This preserves templates,
internal linkage, and global initialization order.

## Managed memory

Host API: `VeroScriptSession::GetMemoryContext()` / `VeroScript::Memory`. See the [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md) reference for ownership, quotas, view expiry and leases.

## Source-tool diagnostic language

The C++ frontend and the C++ BindGen/Transpiler tools currently emit their own help, argument errors and declaration diagnostics in English. The BindGen/Transpiler CLIs do not accept `--lang`; selecting a runtime diagnostic language does not translate these tool messages. Diagnostic codes and source locations remain suitable for programmatic handling.

## Module environment and backends

When a supplied module environment has execution resolvers, Engine/Session can inherit its automatic backend choices. Explicit factories and files/directories still take priority; raw `VeroScriptVm` assembly remains explicit. See [Service backend selection](../../../../../../../VeroScript/Resources/Docs/User/en-US/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
