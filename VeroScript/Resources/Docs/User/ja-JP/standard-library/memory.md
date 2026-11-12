# 管理されたバイトバッファ — std::memory

`memory.vs` または `core.vs` をインポートし、修飾名 `std::memory` を使います。バッファはバイナリデータ、ビューはバッファの限定された範囲に使い、独立したコピーには `clone` を使います。

## バッファとビュー

バッファの代入はバイトを共有し、コピーしません。ビューは所有者を保持し、固定のオフセットと長さを表します。`readonly_view` はそのビューからの書き込みを禁止します。既定の空ビューは有効です。サイズや容量が変わると既存のビューは永久に失効し、元のサイズに戻しても復活しません。構造変更後はビューを作り直してください。変更のない `resize` / `reserve` はビューを保ち、失敗時はバッファの状態を保ちます。新たに公開されるバイトはゼロになります。

## バイト操作

オフセットと長さは非負の `int64` で、指定領域内に収まる必要があります。長さゼロでも領域、境界、書き込み権限を確認します。`copy` は重なる範囲に対応します。`read<T>` / `write<T>` は符号付き・符号なしの 8/16/32/64 ビット整数と別名、`float`、`double` を受け付け、バイト順は既定で `little` です。非整列オフセットも使えます。`from_array` は `array<uint8>`（`array<unsigned char>`）をコピーし、`to_array` は独立したスナップショットを返します。

## API リファレンス

次のブロックはシグネチャの説明です。`region`、`object`、`value`、`source`、`destination` は互換性のある引数を表し、完全なスクリプトではありません。

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

| 型 | メンバー |
| --- | --- |
| `buffer` | `new buffer()`, `new buffer(int64 size)`, `size()`, `capacity()`, `get(int64 offset)`, `set(int64 offset, uint8 value)`, `resize(int64 size)`, `reserve(int64 capacity)`, `slice(int64 offset, int64 count)`, `readonly_slice(int64 offset, int64 count)` |
| `view` | `view()`, `size()`, `get`, `set`, `slice`, `readonly_slice` |
| `readonly_view` | `readonly_view()`, `size()`, `get`, `slice` → `readonly_view` |
| `byte_order` | `little = 0`, `big = 1` |

## 照会と権限

`object_id` は一つのランタイムインスタンス内で、生存する共有オブジェクトの安定した `uint64` 識別子を返します。クラス、対応するホストクラス、共有コンテナ、チャネル、関数、バッファが対象です。null の共有ハンドルは 0 を返します。スカラー、文字列、struct、ビュー、弱参照は非対応です。ID は寿命を延ばさず、永続識別子や別ランタイム間の比較には使えません。

`address_of(&value)` は既存の参照先の最終格納場所を観察し、数値ペイロード自体のアドレスではありません。getter や値返しの結果は対象を提供できません。`object_address` は共有オブジェクト、`data_address` は領域のバイトを観察します。この一時的な数値はスクリプトから間接参照できず、永続 ID にも使えません。まず `native_addresses_available()` を確認してください。ホストのポリシーが拒否する場合があり、WebAssembly ではネイティブアドレスを提供しません。オブジェクト ID と境界付きバイト操作は利用できます。

## 制限と失敗

ホストがバッファごとと保持バイト総量の上限を設定します。割り当てや構造変更を拒否した場合は元のバッファを保ちます。一括書き込みはキャンセル、時間、命令予算で中断されることがあり、完了済みの書き込みは残ります。失効ビューや読み取り専用ビューへの書き込みは失敗し、ホストのメモリ貸出との競合は Busy になります。永続化にはバイトをコピーしてください。既定のオブジェクトスナップショットは領域ハンドルを保存しません。領域はタスクのキャプチャ、引数、結果、チャネルで転送できません。

## 実行とホスト API

VM または Mixed を使います。独立 C++ / C# / Python AOT はメモリ操作や型の実際の使用を拒否しますが、未使用の import は可能です。モバイル統合は実験的で、ネイティブアドレスは既定で無効です。停止中のデバッガーは領域のメタデータを表示しますが、DAP の任意メモリ読み書きは提供しません。

## 例

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

[手順付きチュートリアル](../../../../../../Resources/Docs/Tutorials/ja-JP/standard-library/20-managed-memory.md)

## ホストリファレンス

- [C++](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/embedding.md)
- [C#](../../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/memory.md)
- [Python](../../../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/memory.md)
- [Interop](../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md)
- [WebAssembly](../../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ja-JP/index.md)


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../../en-US/standard-library/memory.md) · [fr-FR](../../fr-FR/standard-library/memory.md) · [ja-JP](memory.md) · [ko-KR](../../ko-KR/standard-library/memory.md) · [zh-CN](../../zh-CN/standard-library/memory.md) · [zh-TW](../../zh-TW/standard-library/memory.md)
