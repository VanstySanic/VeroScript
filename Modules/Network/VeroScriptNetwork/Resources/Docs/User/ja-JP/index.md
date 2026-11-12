# Network

利用する API のフロントエンドと、選択したネイティブバックエンドを導入してください。`vs-boost` は `network` と、有効な HTTP サーバー／WebSocket サービスを提供し、`vs-curl` は `http-client` を提供します。フロントエンドだけでは通信を実行できません。サービスの欠落や契約の不一致はエラーになります。

<a id="backend-selection"></a>

スクリプトでは `Network::create_context_with_backend("boost", false)` で実行基盤を明示的に選択します。HTTP クライアント／サーバー、SSE、WebSocket のオプションは `set_backend` で環境の選択を上書きします。空の要求は `services.<service>.backend` を継承し、未設定なら `auto` が有効な候補をバックエンド ID 順に試します。明示的な `auto` は設定済みの選択を上書きし、`none` または設定に保存した空値はサービスを無効にします。必要なサービスがなければエラーになり、実行時エラーで別のバックエンドへ処理を再送しません。`boost` と `curl` は提供者 ID であり、固定の既定値ではありません。C++ では型付きサービスインスタンスを渡します。スクリプトの選択トークンは、C++ オプションのインスタンス照合契約を置き換えません。

`vs-network` · `VeroScript::Network`

- [完了通知とプログラムの寿命](Reference/completion.md)
- [診断とローカライズ](Reference/diagnostics.md)
- [ファイルと原子的出力](Reference/files.md)
- [ネットワークリファレンス](Reference/README.md)
- [有界バイトストリーム](Reference/streaming.md)
- [TLS 設定](Reference/tls.md)

[Boost](../../../../../Backend/VeroScriptBoost/Resources/Docs/User/ja-JP/index.md) · [Curl](../../../../../Backend/VeroScriptCurl/Resources/Docs/User/ja-JP/index.md)

<!-- docs-navigation -->

---

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
