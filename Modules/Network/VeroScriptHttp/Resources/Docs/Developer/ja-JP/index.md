# HTTP: ソースの所属

Http は要求・応答とクライアント・サーバーのファサード契約、プロトコルバインディング、パッケージ設定を管理します。`Internal/` フックは Curl クライアントと Boost サーバーに接続し、共通のトランスポート契約は Network に属します。

共通のコンテキストと操作の所有権、レジストリ合成、SDK 境界、ビルド設定は [Network](../../../../../VeroScriptNetwork/Resources/Docs/Developer/ja-JP/index.md) で管理します。

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [ユーザー](../../User/ja-JP/index.md)

<!-- docs-navigation -->

---

[User](../../User/ja-JP/index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
