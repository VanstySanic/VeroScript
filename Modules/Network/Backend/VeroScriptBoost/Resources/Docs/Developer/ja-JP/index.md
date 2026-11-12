# VeroScriptBoost

Boost は Asio/Beast、OpenSSL 統合、`Private/Runtime` (`VeroScriptBoostRuntime`) を管理します。具体的な実装と第三者ソース一覧はこのモジュールに置き、フロントエンド SDK にはそれらの型を公開しません。

共通のコンテキストと操作の所有権、レジストリ合成、SDK 境界、ビルド設定は [Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/ja-JP/index.md) で管理します。

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [ユーザー](../../User/ja-JP/index.md)

<a id="provider-registration"></a>

`boost` は `network`（`veroscript.network`）を登録し、有効なプロトコルに応じて `http-server`（`veroscript.http.server`）、`websocket-client` / `websocket-server`（`veroscript.websocket`）を追加します。契約版は `0.1.1`、戻りハンドルは順に `Network::Backend::IService*`、`Http::Backend::IServerService*`、`WebSocket::Backend::IService*` です。ファクトリは状態を持たず、登録は I/O を開始せず、不明な設定を拒否します。動的探索は `veroscript_extension_query`、静的登録は `veroscript_boost_query` / `Boost::QueryModule` です。`VeroScriptBoostRuntime` の所有者は Boost だけで、フロントエンドや Curl に二重配置しません。有効／無効なプロトコルのメタデータ、lease、移設利用者を一致する foundation/Core 身元で検証します。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
