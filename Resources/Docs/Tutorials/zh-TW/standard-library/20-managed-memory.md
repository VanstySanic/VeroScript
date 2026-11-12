# 20：讀取與複製二進位資料封包

你將寫入一個四位元組數值，用唯讀檢視核對資料，在原緩衝區擴充時保留獨立快照。準備含標準程式庫的 Runtime，選擇 VM 或 Mixed；獨立原始碼 AOT 不支援此功能。

## 1. 儲存程式

將以下程式存成 `memory-packet.vs`。建立八個零位元組，在位移 1 按大端寫入，因此四位元組標頭的第一個位元組是 1。

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

## 2. 檢查並執行

```powershell
veroscript --backend=vm --check memory-packet.vs
veroscript --backend=vm memory-packet.vs
```

```text
1
8
16
```

輸出依序為 1、8、16。檢視共用原資料，clone 擁有副本。擴充至 16 後 `header` 已失效，因此程式不再讀取它；快照仍為八位元組。

## 3. 動手修改

把讀寫順序都改成 little，第一個標頭位元組變成 4。把 clone 改成一般指派，兩個名稱共用緩衝區，大小都變成 16。最後在 resize 後重新取得唯讀檢視並核對資料，不要重用失效檢視。

## 4. 選擇操作

暫時有界存取使用檢視，複製使用 `clone` / `to_array`，還原位元組使用 `from_array`。不要持久保存原生位址或物件 ID。宿主緩衝區與租借請參閱對應 SDK 指南。

[參考](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/memory.md)

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/standard-library/20-managed-memory.md) · [fr-FR](../../fr-FR/standard-library/20-managed-memory.md) · [ja-JP](../../ja-JP/standard-library/20-managed-memory.md) · [ko-KR](../../ko-KR/standard-library/20-managed-memory.md) · [zh-CN](../../zh-CN/standard-library/20-managed-memory.md) · [zh-TW](20-managed-memory.md)
