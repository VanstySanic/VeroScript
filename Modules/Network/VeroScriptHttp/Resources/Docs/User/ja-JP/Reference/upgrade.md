# HTTP から WebSocket への切り替え

任意のブリッジにより、Http と WebSocket は Network だけに依存できます。単独の WebSocket に Http は不要です。`exchange.PrepareUpgrade` は exchange を保持する所有済み `Network::FUpgradeSource` を返し、ヘッダー、プロトコル、バックエンド、契約、世代の不変メタデータを持ちます。この操作では接続移譲も 101 応答も行いません。`AcceptUpgrade(context, source, options)` は通常の接続操作を返します。コンテキストとバックエンドの一致が必要で、移譲は一度だけです。提出からの期限に検査、待機、ハンドシェイクを含みます。準備で HTTP 応答期限は延長されません。

HTTP1.1/body なし/Transfer-Encoding なし/Content-Length 非 zero 不可、sharedoriginal が必要。事前に options/context/contract/header≤64 KiB/GET/Host/Upgrade/Connection/version13/key16byte/subprotocol を検査。requeststream は完了必須、応答中/済/旧 generation/close は不可。事前失敗/取消は HTTPreply 可、移譲後失敗は close して復元/二重応答不可。cancel は transfer/101 と競合し IsConsumed/consumed で確認します。

同 executor/既存 connectionquota、pre-readframe 保持、TLSstream 移動で再接続なし。source/lease/handshake/result は Owner/quota 保持、旧 exchange 解放は新 WS を閉じません。HttpClose/Shutdown は移譲済み対象外、contextShutdown は双方対象。Allocation でも通常 cleanup 必須。scriptprepare_upgrade/accept_upgrade は 3provider 可視性、metadata 不変、HostDrivenpoll 継続です。

## 例

```VeroScript
http::exchange exchange = incoming.exchange();
Network::upgrade_source source = exchange.prepare_upgrade();
websocket::options options = websocket::make_options();
options.set_timeout(10000);
websocket::operation accepting = websocket::accept_upgrade(source, options);
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/upgrade.md) · [fr-FR](../../fr-FR/Reference/upgrade.md) · [ja-JP](upgrade.md) · [ko-KR](../../ko-KR/Reference/upgrade.md) · [zh-CN](../../zh-CN/Reference/upgrade.md) · [zh-TW](../../zh-TW/Reference/upgrade.md)
