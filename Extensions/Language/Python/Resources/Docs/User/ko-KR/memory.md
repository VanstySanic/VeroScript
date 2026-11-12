# 호스트에서 사용하는 관리 메모리

[호스트 공통 메모리 소유권](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#host-memory-ownership)을 참조하세요.

`RuntimeCapabilities.MEMORY`를 확인합니다. Python 정수는 ID 정밀도를 보존합니다. `wrap_external`은 연속 Python buffer를 요구하며 대여를 닫기 전에 모든 내보낸 `memoryview`를 해제해야 합니다. 예제의 중첩 컨텍스트가 순서를 보장합니다. `current_memory()`는 등록 callback 안에서만 유효하고 `argument_address(index)`는 원래 참조 인수를 조회합니다. Value는 네이티브 핸들을 소유하고 수집될 때 해제합니다.

## 한도와 소유권

[호스트 공통 메모리 소유권](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#host-memory-ownership)을 참조하세요.

## 예제

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

## 대여와 외부 저장소

[호스트 공통 메모리 소유권](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#host-memory-ownership)을 참조하세요.

## 조회와 callback

[호스트 공통 메모리 소유권](../../../../../../../VeroScriptInterop/Resources/Docs/User/ko-KR/index.md#host-memory-ownership)을 참조하세요.

[참조](../../../../../../../VeroScript/Resources/Docs/User/ko-KR/standard-library/memory.md)

<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory.md) · [fr-FR](../fr-FR/memory.md) · [ja-JP](../ja-JP/memory.md) · [ko-KR](memory.md) · [zh-CN](../zh-CN/memory.md) · [zh-TW](../zh-TW/memory.md)
