# 受管理字节缓冲区 — std::memory

导入 `memory.vs` 或 `core.vs`，使用带限定的 `std::memory` 名称。buffer 用于二进制数据，view 用于缓冲区的一段有界范围，需要独立副本时使用 `clone`。

## 缓冲区与视图

赋值 buffer 会共享字节，不会复制。视图保留所属缓冲区，表示固定偏移和长度；`readonly_view` 禁止通过该视图写入。默认空视图有效。改变大小或容量会使现有视图永久失效，即使后来恢复原来的大小也不会恢复视图。结构变化后应重新取得视图；没有实际变化的 `resize` / `reserve` 保留视图，操作失败保留缓冲区状态。重新暴露的字节清零。

## 字节操作

偏移和数量是非负 `int64`，必须位于选定区域内。零长度操作仍检查区域、边界和写权限。`copy` 支持重叠范围。`read<T>` / `write<T>` 支持 8/16/32/64 位有符号和无符号整数及别名、`float`、`double`，默认字节序为 `little`，支持未对齐偏移。`from_array` 复制 `array<uint8>`（也可写作 `array<unsigned char>`），`to_array` 返回独立字节快照。

## API 参考

以下代码块是签名说明。`region`、`object`、`value`、`source` 和 `destination` 表示兼容实参，不是完整脚本。

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

| 类型 | 成员 |
| --- | --- |
| `buffer` | `new buffer()`, `new buffer(int64 size)`, `size()`, `capacity()`, `get(int64 offset)`, `set(int64 offset, uint8 value)`, `resize(int64 size)`, `reserve(int64 capacity)`, `slice(int64 offset, int64 count)`, `readonly_slice(int64 offset, int64 count)` |
| `view` | `view()`, `size()`, `get`, `set`, `slice`, `readonly_slice` |
| `readonly_view` | `readonly_view()`, `size()`, `get`, `slice` → `readonly_view` |
| `byte_order` | `little = 0`, `big = 1` |

## 查询与权限

`object_id` 为一个运行时实例中的存活共享对象返回稳定的 `uint64` 身份，支持类、受支持的宿主类、共享容器、通道、函数和 buffer。空共享句柄返回 0；标量、字符串、结构体、视图和弱句柄不支持。ID 不延长对象寿命，也不是持久标识或跨运行时标识。

`address_of(&value)` 观察既有引用目标的最终存储位置，而不是其数值载荷的地址；getter 和按值结果不能提供该目标。`object_address` 观察共享对象，`data_address` 观察区域字节。这些瞬时数值不能在脚本中解引用，也不能保存为持久 ID。先检查 `native_addresses_available()`：宿主策略可能禁止地址，WebAssembly 不提供原生地址；对象 ID 和有界字节操作仍可用。

## 限额与失败

宿主设置单缓冲区和保留字节总量的配额。分配或结构变化被拒绝时，旧缓冲区保持不变。批量写入可能被取消、时间或指令限额中断，已完成的写入仍然可见。失效视图会报错，只读视图拒绝写入，与宿主内存租借冲突时返回 Busy。持久化应复制字节快照，默认对象快照不序列化区域句柄。区域不能通过任务捕获、参数、结果或通道传递。

## 执行与宿主 API

使用 VM 或 Mixed 执行。独立 C++ / C# / Python AOT 拒绝实际使用内存操作或类型，允许未使用的导入。移动端集成仍为实验性，默认关闭原生地址。暂停调试时可检查区域元数据，但不提供任意 DAP 内存读写。

## 示例

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

[逐步教程](../../../../../../Resources/Docs/Tutorials/zh-CN/standard-library/20-managed-memory.md)

## 宿主参考

- [C++](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/embedding.md)
- [C#](../../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/memory.md)
- [Python](../../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/memory.md)
- [Interop](../../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md)
- [WebAssembly](../../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-CN/index.md)


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../../en-US/standard-library/memory.md) · [fr-FR](../../fr-FR/standard-library/memory.md) · [ja-JP](../../ja-JP/standard-library/memory.md) · [ko-KR](../../ko-KR/standard-library/memory.md) · [zh-CN](memory.md) · [zh-TW](../../zh-TW/standard-library/memory.md)
