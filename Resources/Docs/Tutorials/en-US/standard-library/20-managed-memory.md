# 20: Read and copy a binary packet

You will write a four-byte value, inspect it through a read-only view, and keep an independent snapshot while the original buffer grows. Install the Runtime with its standard library and choose VM or Mixed; standalone source AOT does not support this feature.

## 1. Save the program

Save the following as `memory-packet.vs`. Allocate eight zeroed bytes. Write at offset 1 in big-endian order, so the first byte of the four-byte header is 1.

```VeroScript
import "memory.vs";

int main()
{
	auto packet = new std::memory::buffer(8);
	std::memory::write<uint32>(packet, 1, 0x01020304u, std::memory::byte_order::big);
	auto header = packet.readonly_slice(1, 4);
	print(header.get(0));
	assert(std::memory::read<uint32>(header, 0, std::memory::byte_order::big) == 0x01020304u);
	auto snapshot = std::memory::clone(packet);
	packet.resize(16);
	print(snapshot.size());
	print(packet.size());
	return 0;
}
```

## 2. Check and run

```powershell
veroscript --backend=vm --check memory-packet.vs
veroscript --backend=vm memory-packet.vs
```

```text
1
8
16
```

The program prints 1, 8 and 16. The view shares the packet bytes; the clone owns a copy. Resizing the packet to 16 expires `header`, so the program no longer reads it. The snapshot remains eight bytes.

## 3. Try it yourself

Change the byte order to little in both read and write; the first header byte becomes 4. Change the clone to a plain assignment: both names share the buffer, so both sizes become 16. Finally, create a new read-only view after resize and verify its bytes. Do not reuse the expired view.

## 4. Choose the right operation

Use a view for temporary bounded access, `clone` or `to_array` for a copy, and `from_array` to restore bytes. Do not persist native addresses or object IDs. For host-owned buffers and leases, use the matching SDK guide.

[Reference](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/memory.md)

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](20-managed-memory.md) · [fr-FR](../../fr-FR/standard-library/20-managed-memory.md) · [ja-JP](../../ja-JP/standard-library/20-managed-memory.md) · [ko-KR](../../ko-KR/standard-library/20-managed-memory.md) · [zh-CN](../../zh-CN/standard-library/20-managed-memory.md) · [zh-TW](../../zh-TW/standard-library/20-managed-memory.md)
