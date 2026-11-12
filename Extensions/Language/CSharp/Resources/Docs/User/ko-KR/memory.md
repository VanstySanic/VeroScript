# 호스트에서 사용하는 관리 메모리

[호스트 공통 메모리 소유권](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#host-memory-ownership)을 참조하세요.

`VeroScriptRuntime.Capabilities`의 `VeroScriptCapabilities.Memory`를 확인합니다. 반환된 `VeroScriptValue`와 대여를 Dispose하세요. `WrapExternal(Memory<byte>, readOnly)`는 최종 해제까지 저장소를 고정합니다. 사용자 MemoryManager는 Pin 및 최종 해제 스레드의 Unpin을 지원해야 합니다. `context.Memory`와 `context.GetArgumentAddress(index)`는 활성 callback 안에서만 사용합니다. span이나 포인터를 대여 밖으로 내보내지 마세요.

## 한도와 소유권

[호스트 공통 메모리 소유권](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#host-memory-ownership)을 참조하세요.

## 예제

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

## 대여와 외부 저장소

[호스트 공통 메모리 소유권](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#host-memory-ownership)을 참조하세요.

## 조회와 callback

[호스트 공통 메모리 소유권](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#host-memory-ownership)을 참조하세요.

[참조](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](../zh-TW/memory.md)
