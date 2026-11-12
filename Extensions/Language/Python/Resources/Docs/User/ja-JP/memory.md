# ホストから使う管理メモリ

[ホスト共通のメモリ所有権](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#host-memory-ownership) を参照してください。

`RuntimeCapabilities.MEMORY` を確認します。Python 整数は ID の精度を保ちます。`wrap_external` は連続した Python buffer を要求し、貸出を閉じる前に全ての export 済み `memoryview` を解放します。例の入れ子コンテキストが順序を保証します。`current_memory()` は登録済み callback 内だけで有効で、`argument_address(index)` が元の参照引数を照会します。Value はネイティブハンドルを所有し、回収時に解放します。

## 上限と所有権

[ホスト共通のメモリ所有権](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#host-memory-ownership) を参照してください。

## 例

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

## 貸出と外部領域

[ホスト共通のメモリ所有権](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#host-memory-ownership) を参照してください。

## 照会と callback

[ホスト共通のメモリ所有権](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md#host-memory-ownership) を参照してください。

[リファレンス](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](memory.md) · [ko-KR](../ko-KR/memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](../zh-TW/memory.md)
