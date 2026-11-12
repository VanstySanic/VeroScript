# Managed memory from the host

See [Shared host memory ownership](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#host-memory-ownership).

Check `VeroScriptRuntime.Capabilities` for `VeroScriptCapabilities.Memory`. Dispose returned `VeroScriptValue` handles and leases. `WrapExternal(Memory<byte>, readOnly)` pins its storage until final release; a custom MemoryManager must support pinning and Unpin on the final releasing thread. Use `context.Memory` and `context.GetArgumentAddress(index)` only within the active callback. Do not let spans or raw pointers escape their lease.

## Limits and ownership

See [Shared host memory ownership](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#host-memory-ownership).

## Example

```CSharp
using VeroScript;

using var session = new VeroScriptSession();
session.SetMemoryLimits(new VeroScriptMemoryLimits(16 * 1024 * 1024, 32 * 1024 * 1024));
var memory = session.Memory;
using var packet = memory.CreateBuffer(8);
using var number = new VeroScriptValue();
number.Set(0x01020304u);
memory.Write(packet, 1, VeroScriptNativeTypeKind.UInt32, number, VeroScriptByteOrder.Big);
using var header = memory.Slice(packet, 1, 4, readOnly: true);
using var decoded = memory.Read(header, 0, VeroScriptNativeTypeKind.UInt32, VeroScriptByteOrder.Big);
Console.WriteLine(decoded.GetUInt32());
using (var lease = memory.AcquireLease(packet, write: true))
{
	lease.WritableBytes[0] = 7;
}
using var snapshot = memory.Clone(packet);
memory.Resize(packet, 16);
Console.WriteLine(memory.Size(snapshot));
```

## Leases and external storage

See [Shared host memory ownership](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#host-memory-ownership).

## Queries and callbacks

See [Shared host memory ownership](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#host-memory-ownership).

[Reference](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](../zh-TW/memory.md)
