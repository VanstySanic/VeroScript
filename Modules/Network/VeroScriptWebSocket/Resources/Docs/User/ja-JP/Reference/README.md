# WebSocket — ネットワークリファレンス

フロントエンドの導入とバックエンド選択の共通ルールは [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/ja-JP/index.md#backend-selection) で定義します。

`vs-websocket` は対応する `vs-network` に依存します。ネイティブ ID とスクリプト名前空間は `websocket`、CMake ターゲットは `VeroScript::WebSocket` です。任意の `VeroScript::WebSocketBindings` には対応する Core が必要です。Network を先に登録し、可視依存を宣言してください。Http、curl、Json は必須ではなく、JSON 連携には `COMPONENTS Json` を使用します。

`FConnection` は ws/wss のクライアント／サーバー、テキスト／バイナリ、任意の圧縮、自動 Pong、有界キューを提供します。スクリプトの受信待ちがなくてもネイティブ読み取りを続け、Ping と終了を処理します。期限、コールバック配信、使用量、寿命は Network の契約に従います。未完了、失敗、型不一致の結果取得は拒否され、オプションは値のスナップショットです。プロバイダーとともに BoostRuntime を配布し、全ネイティブ構成要素の Core、ビルド、プラットフォーム、ツールチェーンを一致させてください。

- [完了通知とプログラムの寿命](completion.md)
- [診断とローカライズ](diagnostics.md)
- [JSON アダプター](json-adapters.md)
- [TLS 設定](tls.md)
- [WebSocket 圧縮](websocket-compression.md)
- [WebSocket CONNECT プロキシ](websocket-proxy.md)
- [有界 WebSocket 再接続](websocket-reconnect.md)
- [WebSocket メッセージと制御](websocket.md)

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](../../fr-FR/Reference/README.md) · [ja-JP](README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
