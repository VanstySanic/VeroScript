# Mémoire gérée depuis l’hôte

Voir [Propriété mémoire commune aux hôtes](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#host-memory-ownership).

Vérifiez `VeroScriptCapabilities.Memory` dans `VeroScriptRuntime.Capabilities`. Libérez les handles `VeroScriptValue` et prêts retournés. `WrapExternal(Memory<byte>, readOnly)` épingle le stockage jusqu’à la libération finale ; un MemoryManager personnalisé doit permettre Pin et Unpin sur le thread de libération finale. `context.Memory` et `context.GetArgumentAddress(index)` sont limités au callback actif. Ne laissez pas de span ou pointeur sortir du prêt.

## Quotas et propriété

Voir [Propriété mémoire commune aux hôtes](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#host-memory-ownership).

## Exemple

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

## Prêts mémoire et stockage externe

Voir [Propriété mémoire commune aux hôtes](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#host-memory-ownership).

## Requêtes et callbacks

Voir [Propriété mémoire commune aux hôtes](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#host-memory-ownership).

[Référence](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](../zh-TW/memory.md)
