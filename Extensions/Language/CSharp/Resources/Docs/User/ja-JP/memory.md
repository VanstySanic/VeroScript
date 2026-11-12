# ホストから使う管理メモリ

[ホスト共通のメモリ所有権](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#host-memory-ownership) を参照してください。

`VeroScriptRuntime.Capabilities` の `VeroScriptCapabilities.Memory` を確認します。返された `VeroScriptValue` と貸出を Dispose してください。`WrapExternal(Memory<byte>, readOnly)` は最終解放まで領域を固定します。独自 MemoryManager は Pin と最終解放スレッドからの Unpin に対応する必要があります。`context.Memory` と `context.GetArgumentAddress(index)` は callback 中だけで使います。span やポインターを貸出の外へ持ち出さないでください。

## 上限と所有権

[ホスト共通のメモリ所有権](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#host-memory-ownership) を参照してください。

## 例

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

## 貸出と外部領域

[ホスト共通のメモリ所有権](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#host-memory-ownership) を参照してください。

## 照会と callback

[ホスト共通のメモリ所有権](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#host-memory-ownership) を参照してください。

[リファレンス](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](../zh-TW/memory.md)
