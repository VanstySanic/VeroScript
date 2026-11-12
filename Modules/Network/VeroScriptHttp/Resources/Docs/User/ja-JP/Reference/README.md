# Http — ネットワークリファレンス

フロントエンドの導入とバックエンド選択の共通ルールは [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ja-JP/index.md#backend-selection) で定義します。

vs-http 0.1.1 は対応 vs-network に依存し、WS/JSON は必須でありません。ID/namespace http、target VeroScript::Http、Bindings は対応 Core の VeroScript::HttpBindings。network を先に登録し依存可視性を設定、公開 curl/Boost 型なし。buffer/stream client/server、routing、multipart、SSE、file を create_client/create_request/listen/create_response と型付き結果で使います。NUL 保持、options は提出時値 snapshot、deadline/dispatch/寿命/quota は Network です。4xx/5xx のみでは transport 失敗ではありません。body setter は競合 mode を除去、form は plus/space 明示、multipart は反復/binary 保持。buffer response は response_builder/reply_response、add_header/set_header は追加/置換。SSE reader/encoder は再接続を追加せず、C++ COMPONENTS Json は任意です。

- [HTTP クライアントポリシー](client-policy.md)
- [完了通知とプログラムの寿命](completion.md)
- [診断とローカライズ](diagnostics.md)
- [ファイルと原子的出力](files.md)
- [HTTP 応答チェーンと統計](http-trace.md)
- [JSON アダプター](json-adapters.md)
- [multipart の送信と解析](multipart.md)
- [HTTP ルーティング](routing.md)
- [SSE クライアント](sse-client.md)
- [有界バイトストリーム](streaming.md)
- [TLS 設定](tls.md)
- [HTTP から WebSocket への切り替え](upgrade.md)

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
