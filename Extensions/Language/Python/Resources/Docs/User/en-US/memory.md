# Managed memory from the host

See [Shared host memory ownership](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#host-memory-ownership).

Check `RuntimeCapabilities.MEMORY`. Python integers retain full ID precision. `wrap_external` requires a contiguous Python buffer; keep every exported `memoryview` released before closing its lease. The example uses a nested context manager to guarantee that order. `current_memory()` is valid only during a registered callback; `argument_address(index)` queries its original reference argument. Value objects own their native handles and release them when collected.

## Limits and ownership

See [Shared host memory ownership](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#host-memory-ownership).

## Example

```python
from veroscript import Backend, ByteOrder, Session

with Session(backend=Backend.VM) as session:
	session.set_memory_limits(16 * 1024 * 1024, 32 * 1024 * 1024)
	memory = session.memory
	packet = memory.buffer(8)
	memory.write(packet, 1, "uint32", 0x01020304, ByteOrder.BIG)
	header = memory.slice(packet, 1, 4, read_only=True)
	assert memory.read(header, 0, "uint32", ByteOrder.BIG) == 0x01020304
	with memory.acquire_lease(packet, write=True) as lease:
		with memoryview(lease) as bytes_view:
			bytes_view[0] = 7
	assert memory.read(packet, 0, "uint8") == 7
	snapshot = memory.clone(packet)
	memory.resize(packet, 16)
	assert memory.size(snapshot) == 8
	print(memory.read(snapshot, 0, "uint8"))
```

## Leases and external storage

See [Shared host memory ownership](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#host-memory-ownership).

## Queries and callbacks

See [Shared host memory ownership](../../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md#host-memory-ownership).

[Reference](../../../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](../zh-TW/memory.md)
