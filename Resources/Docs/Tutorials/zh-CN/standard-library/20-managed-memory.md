# 20：读取和复制二进制数据包

你将写入一个四字节数值，用只读视图检查数据，在原缓冲区增长时保留独立快照。准备带标准库的 Runtime，选择 VM 或 Mixed；独立源码 AOT 不支持此功能。

## 1. 保存程序

将下面程序保存为 `memory-packet.vs`。创建八个零字节，在偏移 1 按大端写入，因此四字节头部的第一个字节是 1。

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

## 2. 检查并运行

```powershell
veroscript --backend=vm --check memory-packet.vs
veroscript --backend=vm memory-packet.vs
```

```text
1
8
16
```

输出依次为 1、8、16。视图共享原数据，clone 拥有副本。扩大到 16 后 `header` 已失效，因此程序不再读取它；快照仍为八字节。

## 3. 动手修改

把读写字节序都改为 little，第一个头部字节变为 4。把 clone 改为普通赋值，两个名字共享缓冲区，大小都变为 16。最后在 resize 后重新取得只读视图并核对字节，不要复用失效视图。

## 4. 选择操作

临时有界访问使用视图，复制使用 `clone` / `to_array`，恢复字节使用 `from_array`。不要持久化原生地址或对象 ID。宿主缓冲区与租借请参阅对应 SDK 指南。

[参考](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/standard-library/20-managed-memory.md) · [fr-FR](../../fr-FR/standard-library/20-managed-memory.md) · [ja-JP](../../ja-JP/standard-library/20-managed-memory.md) · [ko-KR](../../ko-KR/standard-library/20-managed-memory.md) · [zh-CN](20-managed-memory.md) · [zh-TW](../../zh-TW/standard-library/20-managed-memory.md)
