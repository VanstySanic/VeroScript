# HTTP 升級為 WebSocket

選用橋接讓 Http 和 WebSocket 僅依賴 Network；獨立使用 WebSocket 不需要 Http。`exchange.PrepareUpgrade` 回傳擁有的 `Network::FUpgradeSource`，保留原 exchange 及不可變的請求頭、協定、後端、契約與世代資訊；不移交連線，也不發送 101。`AcceptUpgrade(context, source, options)` 回傳普通連線操作。context／後端必須相符，移交僅一次，且提交時開始的期限涵蓋檢查、排隊及握手。準備來源不會延長 HTTP 回應期限。

須HTTP1.1、無正文/Transfer-Encoding/非零Content-Length、原exchange共享持有。移交前檢options/context/contract/header≤64 KiB/GET/Host/Upgrade/Connection/version13/16bytekey/subprotocol；request流已完整結束、正回/已回/舊代/已關拒絕。前檢失敗或移交前取消保來源可HTTP回應，移交後失敗關閉、不可回復或第二回應；cancel與transfer/101競爭，查IsConsumed/consumed。

同executor/原連線槽，保預讀frame，TLSstream直接移動不重連。source/lease/handshake/result保Owner/quota，釋放舊exchange不關新WS；HttpClose/Shutdown不再擁有，contextShutdown仍管兩者。Allocation後仍正常清理。腳本prepare_upgrade/accept_upgrade須三provider可見性，metadata不變，HostDriven持續poll。

## 範例

```VeroScript
http::exchange exchange = incoming.exchange();
Network::upgrade_source source = exchange.prepare_upgrade();
websocket::options options = websocket::make_options();
options.set_timeout(10000);
websocket::operation accepting = websocket::accept_upgrade(source, options);
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/upgrade.md) · [fr-FR](../../fr-FR/Reference/upgrade.md) · [ja-JP](../../ja-JP/Reference/upgrade.md) · [ko-KR](../../ko-KR/Reference/upgrade.md) · [zh-CN](../../zh-CN/Reference/upgrade.md) · [zh-TW](upgrade.md)
