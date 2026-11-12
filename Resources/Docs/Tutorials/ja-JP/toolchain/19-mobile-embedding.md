# 19：モバイルアプリでスクリプトを実行する

Android/iOS アプリで対象に合うモバイル SDK を使用します。アプリには対象 CPython の組み込み環境または .NET 10 MAUI が必要です。デスクトップパッケージにはモバイル用ネイティブバイナリはありません。この連携は実験段階です。

[Python](../../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/mobile.md) · [CSharp](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/mobile.md)

1. 使用するバインディングのガイドに従い、ネイティブライブラリを配置し、標準ライブラリと翻訳リソースをインストールします。Session の作成前にインストールを完了してください。

2. 同じワーカースレッド上でカタログを読み込み、言語を選び、VM Session を作成・設定します。`import "core.vs"; void main() { print(42); }` を読み込み、読み込みと実行の両方の結果を確認し、print コールバックを収集して Session を閉じます。収集した `42` を UI スレッドに返します。

3. 英語以外の言語を選び、`void main() { int x = ; }` で再試行します。診断を表示し、UI が応答することを確認します。正常なスクリプトに戻し、実行制限を設定し、各対象で最終的な署名済みアプリを検証してください。

[en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/toolchain/19-mobile-embedding.md) · [fr-FR](../../fr-FR/toolchain/19-mobile-embedding.md) · [ja-JP](19-mobile-embedding.md) · [ko-KR](../../ko-KR/toolchain/19-mobile-embedding.md) · [zh-CN](../../zh-CN/toolchain/19-mobile-embedding.md) · [zh-TW](../../zh-TW/toolchain/19-mobile-embedding.md)
