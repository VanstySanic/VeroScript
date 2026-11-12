# 診断とローカライズ

network.json/http.json/websocket.json は各 module の 6 言語 Resources/Localization にあります。FError::Id は安定/割当不要、成功空、未知 enum は network.unknown。protocol transport は同じ domain です。operation.message_id/error_code/error は ID/code/原詳細、pending も空 ID になり得るので状態と成功を別確認。stream/SSE/multipart/history/reconnect も同じです。

引数 0 は原詳細、分類は翻訳、backend detail は原文です。翻訳/OS 文字列で分岐しません。同期 binding error は network.binding/http.binding/websocket.binding と `FScriptError.MessageId/MessageArgs`、表示割当失敗でも最初の error を保持します。独立 SDK は Core/大域言語不要です。

インストールした各モジュールの `share/VeroScript<Module>/Resources/Localization` を、ホストのリソースリゾルバーへ登録してください。Http と WebSocket には依存先 Network の `network.json` も必要ですが、同じファイルは重複して同梱されません。複数モジュールを使うホストでは、各ディレクトリを個別に登録するか、必要な全コンポーネントを保持する統合カタログルートを配布します。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](../../fr-FR/Reference/diagnostics.md) · [ja-JP](diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
