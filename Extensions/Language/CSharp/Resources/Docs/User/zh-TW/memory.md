# 宿主中的受管理記憶體

請參閱[主程式共用記憶體擁有權](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#host-memory-ownership)。

檢查 `VeroScriptRuntime.Capabilities` 中的 `VeroScriptCapabilities.Memory`。釋放傳回的 `VeroScriptValue` 與租借。`WrapExternal(Memory<byte>, readOnly)` 固定儲存直到最終釋放；自訂 MemoryManager 必須支援 Pin 及最終釋放執行緒上的 Unpin。僅在活躍回呼內使用 `context.Memory` 和 `context.GetArgumentAddress(index)`，不要將 span 或指標帶出租借範圍。

## 配額與擁有權

請參閱[主程式共用記憶體擁有權](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#host-memory-ownership)。

## 範例

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

## 租借與外部儲存

請參閱[主程式共用記憶體擁有權](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#host-memory-ownership)。

## 查詢與回呼

請參閱[主程式共用記憶體擁有權](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#host-memory-ownership)。

[參考](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](memory.md)
