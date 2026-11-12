# 受管理位元組緩衝區 — std::memory

匯入 `memory.vs` 或 `core.vs`，使用限定的 `std::memory` 名稱。buffer 用於二進位資料，view 用於緩衝區的一段有界範圍，需要獨立副本時使用 `clone`。

## 緩衝區與檢視

指派 buffer 會共用位元組，不會複製。檢視保留所屬緩衝區，表示固定的位移與長度；`readonly_view` 禁止透過該檢視寫入。預設空檢視有效。改變大小或容量會使現有檢視永久失效，即使恢復原來的大小也不會恢復檢視。結構變化後應重新取得檢視；沒有實際變化的 `resize` / `reserve` 保留檢視，操作失敗保留緩衝區狀態。重新暴露的位元組清零。

## 位元組操作

位移與數量是非負的 `int64`，必須位於選定區域內。零長度操作仍檢查區域、邊界與寫入權限。`copy` 支援重疊範圍。`read<T>` / `write<T>` 支援 8/16/32/64 位元帶正負號與不帶正負號整數及別名、`float`、`double`，預設位元組順序為 `little`，支援未對齊位移。`from_array` 複製 `array<uint8>`（也可寫成 `array<unsigned char>`），`to_array` 傳回獨立快照。

## API 參考

以下程式碼區塊是簽章說明。`region`、`object`、`value`、`source`、`destination` 表示相容引數，不是完整腳本。

```text
bool std::memory::native_addresses_available();
uint64 std::memory::address_of(&value);
uint64 std::memory::object_id(object);
uint64 std::memory::object_address(object);
uint64 std::memory::data_address(region);
void std::memory::copy(destination, int64 offset, source, int64 source_offset, int64 count);
void std::memory::fill(destination, int64 offset, int64 count, uint8 value);
bool std::memory::equal(left, int64 offset, right, int64 right_offset, int64 count);
std::memory::buffer std::memory::clone(region);
std::memory::buffer std::memory::from_array(array<uint8> bytes);
array<uint8> std::memory::to_array(region);
T std::memory::read<T>(region, int64 offset, std::memory::byte_order order = std::memory::byte_order::little);
void std::memory::write<T>(region, int64 offset, T value, std::memory::byte_order order = std::memory::byte_order::little);
```

| 型別 | 成員 |
| --- | --- |
| `buffer` | `new buffer()`, `new buffer(int64 size)`, `size()`, `capacity()`, `get(int64 offset)`, `set(int64 offset, uint8 value)`, `resize(int64 size)`, `reserve(int64 capacity)`, `slice(int64 offset, int64 count)`, `readonly_slice(int64 offset, int64 count)` |
| `view` | `view()`, `size()`, `get`, `set`, `slice`, `readonly_slice` |
| `readonly_view` | `readonly_view()`, `size()`, `get`, `slice` → `readonly_view` |
| `byte_order` | `little = 0`, `big = 1` |

## 查詢與權限

`object_id` 為一個執行階段執行個體中的存活共用物件傳回穩定的 `uint64` 身分，支援類別、受支援的宿主類別、共用容器、通道、函式與 buffer。空共用控制代碼傳回 0；純量、字串、結構、檢視和弱控制代碼不支援。ID 不延長物件存留期，也不是永久或跨執行階段的識別碼。

`address_of(&value)` 觀察既有參考目標的最終儲存位置，而非其數值內容的位址；getter 和傳值結果不能提供該目標。`object_address` 觀察共用物件，`data_address` 觀察區域位元組。這些瞬時數值不能在腳本中解參考，也不能保存為永久 ID。先檢查 `native_addresses_available()`：宿主原則可能禁止位址，WebAssembly 不提供原生位址；物件 ID 和有界位元組操作仍可用。

## 限額與失敗

宿主設定單一緩衝區與保留位元組總量的配額。配置或結構變化被拒絕時，原緩衝區保持不變。批次寫入可能被取消、時間或指令限額中斷，已完成的寫入仍然可見。失效檢視會報錯，唯讀檢視拒絕寫入，與宿主記憶體租借衝突時傳回 Busy。持久化應複製位元組快照，預設物件快照不序列化區域控制代碼。區域不能透過工作擷取、引數、結果或通道傳遞。

## 執行與宿主 API

使用 VM 或 Mixed 執行。獨立 C++ / C# / Python AOT 拒絕實際使用記憶體操作或型別，允許未使用的匯入。行動端整合仍為實驗性，預設關閉原生位址。暫停偵錯時可檢查區域中繼資料，但不提供任意 DAP 記憶體讀寫。

## 範例

```VeroScript
import "memory.vs";

int main()
{
	int value = 42;
	ref int alias = value;
	if (std::memory::native_addresses_available())
	{
		assert(std::memory::address_of(&value) == std::memory::address_of(&alias));
	}

	auto bytes = new std::memory::buffer(8);
	auto sharedBytes = bytes;
	assert(std::memory::object_id(bytes) == std::memory::object_id(sharedBytes));
	std::memory::write<uint32>(bytes, 1, 0x01020304u, std::memory::byte_order::big);
	auto header = bytes.readonly_slice(1, 4);
	assert(header.get(0) == 1u);
	assert(std::memory::read<uint32>(header, 0, std::memory::byte_order::big) == 0x01020304u);
	std::memory::copy(bytes, 2, bytes, 1, 4);
	auto snapshot = std::memory::clone(bytes);
	assert(std::memory::object_id(bytes) != std::memory::object_id(snapshot));
	assert(std::memory::equal(bytes, 0, snapshot, 0, 8));
	bytes.resize(16); // header is now expired.
	assert(bytes.get(8) == 0u && snapshot.size() == 8);
	auto packet = std::memory::from_array(std::memory::to_array(snapshot));
	assert(std::memory::equal(packet, 0, snapshot, 0, 8));
	print("memory example passed");
	return 0;
}
```

[逐步教學](../../../../../../Resources/Docs/Tutorials/zh-TW/standard-library/20-managed-memory.md)

## 宿主參考

- [C++](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-TW/embedding.md)
- [C#](../../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-TW/memory.md)
- [Python](../../../../../../Extensions/Language/Python/Resources/Docs/User/zh-TW/memory.md)
- [Interop](../../../../../../VeroScriptInterop/Resources/Docs/User/zh-TW/index.md)
- [WebAssembly](../../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-TW/index.md)


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../../en-US/standard-library/memory.md) · [fr-FR](../../fr-FR/standard-library/memory.md) · [ja-JP](../../ja-JP/standard-library/memory.md) · [ko-KR](../../ko-KR/standard-library/memory.md) · [zh-CN](../../zh-CN/standard-library/memory.md) · [zh-TW](memory.md)
