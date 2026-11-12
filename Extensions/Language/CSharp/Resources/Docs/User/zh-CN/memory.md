# 宿主中的受管理内存

参阅[宿主共享内存所有权](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#host-memory-ownership)。

检查 `VeroScriptRuntime.Capabilities` 中的 `VeroScriptCapabilities.Memory`。释放返回的 `VeroScriptValue` 和租借。`WrapExternal(Memory<byte>, readOnly)` 固定存储直到最终释放；自定义 MemoryManager 必须支持 Pin 及最终释放线程上的 Unpin。仅在活跃回调内使用 `context.Memory` 和 `context.GetArgumentAddress(index)`，不要将 span 或指针带出租借范围。

## 配额与所有权

参阅[宿主共享内存所有权](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#host-memory-ownership)。

## 示例

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

## 租借与外部存储

参阅[宿主共享内存所有权](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#host-memory-ownership)。

## 查询与回调

参阅[宿主共享内存所有权](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#host-memory-ownership)。

[参考](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](memory.md) · [zh-TW](../zh-TW/memory.md)
