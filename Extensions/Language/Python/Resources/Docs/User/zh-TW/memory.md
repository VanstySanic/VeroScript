# 宿主中的受管理記憶體

請參閱[主程式共用記憶體擁有權](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#host-memory-ownership)。

檢查 `RuntimeCapabilities.MEMORY`。Python 整數保留 ID 的完整精確度。`wrap_external` 要求連續的 Python buffer；關閉租借前必須釋放所有匯出的 `memoryview`，範例用巢狀環境管理保證順序。`current_memory()` 僅在註冊回呼內有效，`argument_address(index)` 查詢原始參考引數。Value 物件擁有原生控制代碼，在回收時釋放。

## 配額與擁有權

請參閱[主程式共用記憶體擁有權](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#host-memory-ownership)。

## 範例

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

## 租借與外部儲存

請參閱[主程式共用記憶體擁有權](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#host-memory-ownership)。

## 查詢與回呼

請參閱[主程式共用記憶體擁有權](../../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md#host-memory-ownership)。

[參考](../../../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](memory.md)
