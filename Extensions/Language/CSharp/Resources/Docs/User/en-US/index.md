# C# SDK

`VeroScriptCSharp` is the NativeAOT-friendly .NET adapter for the stable
VeroScript Interop C ABI. It provides managed sessions, values, callbacks,
host bindings, packages, reflection, localization, and JIT diagnostics without
duplicating the compiler or VM.

The managed package targets applications using .NET 8 or .NET 9. The native
`VeroScriptInterop` library is a separate trusted deployment input and must
match the process architecture. VeroScript source and packages do not depend
on C# or .NET.

The current Interop ABI is `0x00000101` (ABI 0.1.1). The managed adapter requires an exact ABI version match. Check `VeroScriptRuntime.AbiVersion` before creating a session and deploy the managed package and native Interop library from matching releases.

## Minimal host

```CSharp
using System;
using VeroScript;

using var session = new VeroScriptSession(new()
{
	Print = Console.WriteLine,
	Diagnostic = Console.Error.WriteLine,
});
session.ApplyConfiguration(new VeroScriptConfiguration
{
	ExecutionBackend = VeroScriptExecutionBackend.Mixed,
});

var loaded = session.LoadSource("""
    int Add(int left, int right)
    {
        return left + right;
    }

    int main()
    {
        print(Add(20, 22));
        return 0;
    }
    """, "main.vs");
if (!loaded.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "load failed");
}

var run = session.Run();
if (!run.Success)
{
	throw new VeroScriptException(VeroScriptStatus.EngineError, "execution failed");
}
```

`VeroScriptSessionOptions.Print` receives script output lines and
`VeroScriptSessionOptions.Diagnostic` receives diagnostic lines. These
callbacks are optional and remain rooted until the session is disposed.

## Documentation map

- [Sessions and runtime](session.md): deployment, configuration, execution, diagnostics, JIT policy, and threading.
- [Values and ownership](values.md): scalar values, containers, borrowed callback views, and callables.
- [Host bindings](host-bindings.md): native types, functions, classes, protocols, interception, and mutation callbacks.
- [Packages, reflection, and localization](packages.md): package transfer, trust boundaries, metadata, serialization, and catalogs.
- [C# AOT](aot.md): generated C# source, deterministic bundles, NativeAOT deployment, and unsupported constructs.
- [C# frontend](frontend.md): Roslyn-backed semantic declarations and type mapping.
- [C# BindGen](bindgen.md): Binding Catalogs, live registration source, API references, and AOT bridges.
- [C# transpiler](transpiler.md): semantic conversion from the supported C# subset to VeroScript source.

Use the language reference for VeroScript syntax. This module documents the
managed C# boundary and the lifetime rules imposed by the Interop ABI.

- [Android and iOS / MAUI](mobile.md)

## Managed memory

The loaded runtime must advertise `VeroScriptCapabilities.Memory`; older compatible runtimes raise `NotSupportedException` before native calls or pinning. Use `context.Memory` in callbacks and `session.Memory` while idle. Custom `Memory<byte>` managers must support `Pin` and allow `Unpin` on the final releasing thread; failed registration releases the pin.

Callback memory operations and `GetArgumentAddress` must run on the callback's managed thread; cross-thread access reports `WrongThread`. A lease may be disposed on another thread after its use has finished.

Host API: `session.Memory` / `SetMemoryLimits`. See the [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md) reference for ownership, quotas, view expiry and leases.

- [Managed memory from the host](memory.md)

## Source-tool diagnostic language

The C# frontend and C# BindGen/Transpiler tools currently emit their own help and diagnostics in English; Roslyn diagnostics are captured with invariant culture. The BindGen/Transpiler CLIs do not accept `--lang`. `--language-version` selects the C# syntax version, not the display language. The native runtime diagnostic language configured through `VeroScriptRuntime.CurrentLanguage` is a separate setting.

## variant

See [Interop SDK — variant](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#exported-value-lifetime).

```csharp
using var payload = new VeroScriptValue();
payload.Set(42);
using var box = session.CreateVariant("int", payload);
string type = box.VariantTypeName;
using var extracted = box.GetVariantValue();
```

Load an application with `VeroScriptSession.LoadApplication(absolutePath, optionsJson)` and check its result before calling `Run()`. Paths in the host options must be absolute. Follow the [Application loading contract](../../../../../../../VeroScriptApplication/Resources/Docs/User/en-US/index.md) for environment, signature and trust configuration.

<!-- docs-navigation -->

---

[User documentation](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/en-US/index.md)

Language: [en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
