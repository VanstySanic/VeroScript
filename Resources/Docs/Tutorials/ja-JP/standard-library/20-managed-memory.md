# 20: バイナリパケットを読み、コピーする

4 バイトの値を書き、読み取り専用ビューで確認し、元のバッファを拡張しても独立スナップショットを保ちます。標準ライブラリ付き Runtime を用意し VM または Mixed を選んでください。独立ソース AOT は非対応です。

## 1. プログラムを保存する

次を `memory-packet.vs` に保存します。ゼロの 8 バイトを確保し、オフセット 1 に big-endian で書くので、4 バイトのヘッダーの先頭は 1 です。

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

## 2. 検証して実行する

```powershell
veroscript --backend=vm --check memory-packet.vs
veroscript --backend=vm memory-packet.vs
```

```text
1
8
16
```

出力は順に 1、8、16 です。ビューはバイトを共有し、clone はコピーを持ちます。16 バイトへの拡張で `header` が失効するため、以後は読みません。スナップショットは 8 バイトのままです。

## 3. 試す

読み書き両方を little に変えると先頭バイトは 4 になります。clone を単純な代入に変えると、両方が同じバッファを共有しサイズは両方 16 になります。最後に resize 後に新しい読み取り専用ビューを作って内容を確認します。失効ビューは再利用しないでください。

## 4. 操作を選ぶ

一時的な有界アクセスはビュー、コピーは `clone` / `to_array`、復元は `from_array` を使います。アドレスや ID を永続化しないでください。ホストのバッファと貸出は各 SDK ガイドを参照します。

[リファレンス](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md)

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/standard-library/20-managed-memory.md) · [fr-FR](../../fr-FR/standard-library/20-managed-memory.md) · [ja-JP](20-managed-memory.md) · [ko-KR](../../ko-KR/standard-library/20-managed-memory.md) · [zh-CN](../../zh-CN/standard-library/20-managed-memory.md) · [zh-TW](../../zh-TW/standard-library/20-managed-memory.md)
