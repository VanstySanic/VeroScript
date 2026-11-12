# Managed byte buffers — std::memory

Import `memory.vs`, or `core.vs`, and use the qualified `std::memory` names. Use buffers for binary data, views for a bounded part of a buffer, and `clone` when an independent copy is needed.

## Buffers and views

Assigning a buffer shares its bytes; it does not copy them. A view keeps its owner alive and covers a fixed offset and length. `readonly_view` prevents writes through that view. An empty default view is valid. Resizing or changing capacity expires existing views permanently, even if the buffer later regains its previous size. Recreate views after a structural change. A no-op `resize` or `reserve` preserves them; a failed operation preserves buffer state. Newly exposed bytes are zeroed.

## Byte operations

Offsets and counts are nonnegative `int64` values and must fit inside the selected region. Zero-length operations still validate the region, bounds and write permission. `copy` handles overlapping ranges. `read<T>` and `write<T>` accept fixed-width signed/unsigned 8/16/32/64-bit integers, their aliases, `float` and `double`; the byte order defaults to `little`. Unaligned offsets are supported. `from_array` copies `array<uint8>` (also spelled `array<unsigned char>`); `to_array` returns an independent byte snapshot.

## API reference

The following block describes signatures. `region`, `object`, `value`, `source` and `destination` represent compatible arguments; it is not a complete script.

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

| Type | Members |
| --- | --- |
| `buffer` | `new buffer()`, `new buffer(int64 size)`, `size()`, `capacity()`, `get(int64 offset)`, `set(int64 offset, uint8 value)`, `resize(int64 size)`, `reserve(int64 capacity)`, `slice(int64 offset, int64 count)`, `readonly_slice(int64 offset, int64 count)` |
| `view` | `view()`, `size()`, `get`, `set`, `slice`, `readonly_slice` |
| `readonly_view` | `readonly_view()`, `size()`, `get`, `slice` → `readonly_view` |
| `byte_order` | `little = 0`, `big = 1` |

## Queries and permissions

`object_id` returns a stable `uint64` identity for a live shared object within one runtime instance: classes, supported host classes, shared containers, channels, functions and buffers. A null shared handle returns 0. Scalars, strings, structs, views and weak handles are unsupported. An ID does not keep the object alive and is not a persistent or cross-runtime identifier.

`address_of(&value)` observes the final storage location of an existing reference target, rather than the address of its numeric payload. Getters and by-value results cannot provide that target. `object_address` observes the shared object, and `data_address` observes region bytes. These transient numbers cannot be dereferenced in script or saved as durable identifiers. Check `native_addresses_available()` first: host policy may deny addresses, and WebAssembly does not provide native addresses. Object IDs and bounded byte operations remain available.

## Limits and failures

The host sets per-buffer and retained-byte quotas. A rejected allocation or structural change preserves the old buffer. Bulk writes may be interrupted by cancellation, time or instruction limits; completed writes remain visible. Views can fail with an expired-region error; writes through read-only views fail; conflicts with a host lease report Busy. Copy a byte snapshot for persistence: default object snapshots do not serialize memory-region handles. Regions cannot be transferred through task captures, arguments, results or channels.

## Execution and host APIs

Use VM or Mixed execution. Standalone C++, C# and Python AOT reject actual use of memory operations or types; an unused import is allowed. Mobile integrations remain experimental and deny native addresses by default. In a paused debugger, region metadata can be inspected, but arbitrary DAP memory reads/writes are unavailable.

## Example

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

[Step-by-step tutorial](../../../../../../Resources/Docs/Tutorials/en-US/standard-library/20-managed-memory.md)

## Host references

- [C++](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/en-US/embedding.md)
- [C#](../../../../../../Extensions/Language/CSharp/Resources/Docs/User/en-US/memory.md)
- [Python](../../../../../../Extensions/Language/Python/Resources/Docs/User/en-US/memory.md)
- [Interop](../../../../../../VeroScriptInterop/Resources/Docs/User/en-US/index.md)
- [WebAssembly](../../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/en-US/index.md)


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](memory.md) · [fr-FR](../../fr-FR/standard-library/memory.md) · [ja-JP](../../ja-JP/standard-library/memory.md) · [ko-KR](../../ko-KR/standard-library/memory.md) · [zh-CN](../../zh-CN/standard-library/memory.md) · [zh-TW](../../zh-TW/standard-library/memory.md)
