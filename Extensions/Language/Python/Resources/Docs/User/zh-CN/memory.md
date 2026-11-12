# 宿主中的受管理内存

参阅[宿主共享内存所有权](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#host-memory-ownership)。

检查 `RuntimeCapabilities.MEMORY`。Python 整数保留 ID 的完整精度。`wrap_external` 要求连续的 Python buffer；关闭租借前必须释放所有导出的 `memoryview`，示例用嵌套上下文管理保证这个顺序。`current_memory()` 仅在注册回调内有效，`argument_address(index)` 查询其原始引用参数。Value 对象拥有原生句柄，在回收时释放。

## 配额与所有权

参阅[宿主共享内存所有权](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#host-memory-ownership)。

## 示例

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

## 租借与外部存储

参阅[宿主共享内存所有权](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#host-memory-ownership)。

## 查询与回调

参阅[宿主共享内存所有权](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md#host-memory-ownership)。

[参考](../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](memory.md) · [zh-TW](../zh-TW/memory.md)
