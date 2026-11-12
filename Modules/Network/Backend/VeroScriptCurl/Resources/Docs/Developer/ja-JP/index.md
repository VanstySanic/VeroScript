# VeroScriptCurl

Curl は `Private/HttpClient.cpp` の HTTP クライアント、curl の依存設定、原本の告知を管理します。このプロバイダーは Http 契約を実装し、Boost トランスポートや WebSocket サービスは管理しません。

共通のコンテキストと操作の所有権、レジストリ合成、SDK 境界、ビルド設定は [Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/ja-JP/index.md) で管理します。

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [ユーザー](../../User/ja-JP/index.md)

<a id="provider-registration"></a>

`curl` は `http-client` / `veroscript.http.client` / `0.1.1` を `Http::Backend::IClientService*` ファクトリで登録します。ファクトリは状態を持たず、登録は I/O を開始せず不明な設定を拒否します。動的探索は `veroscript_extension_query`、静的登録は `veroscript_curl_query` / `Curl::QueryModule` です。Curl は非所有の不透明なソケット待機／タイマーフックを使い、Boost のヘッダーやライブラリには依存しません。HTTPS は curl 自身の TLS 構成に従います。同梱パッチ、静的依存配置、Unix システム依存をこのモジュールで保守し、実ビルドに対して不足／不一致サービス、lease、移設 SDK／パッケージ利用者を検証します。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
