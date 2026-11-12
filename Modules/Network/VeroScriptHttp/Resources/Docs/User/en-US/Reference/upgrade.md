# HTTP to WebSocket upgrade

Http and WebSocket depend only on Network; the optional bridge uses an opaque source/unique transfer lease, with no Http dependency for standalone WebSocket. exchange.PrepareUpgrade returns an owned FUpgradeSource retaining the exchange, with immutable head/protocol/backend/contract/generation metadata; it neither transfers nor sends 101. AcceptUpgrade(context,source,options) returns a normal connection operation. Context/backend must match, transfer is once only, and one submission deadline covers checks/queue/handshake. Preparation does not extend HTTP reply timeout.

Require HTTP/1.1 with no request body, no Transfer-Encoding/nonzero Content-Length, and a shared original exchange. Before transfer validate options, context/contract, ≤64 KiB headers, GET/HTTP1.1/Host/Upgrade/Connection/version13/16-byte key/subprotocols. Streamed request bodies must already finish. Replied/sending/stale/closed exchanges fail. Precheck failure or pretransfer cancellation leaves the source reusable for a normal HTTP reply. Posttransfer failure closes transport, cannot restore HTTP or send a second response. Cancellation can race transfer/101; inspect IsConsumed()/consumed(), not only the error.

Transfer is serialized on the same executor, reuses the existing connection slot and preread frame bytes, and moves HTTPS TLS without reconnecting. Sources/leases/handshakes/results retain code/resources; releasing old exchange/source cannot close the new connection. Http.Close/Shutdown no longer owns transferred connections, context.Shutdown still closes both. Handshake/preread/result storage remains charged. Allocation may close the context; still shutdown/destroy normally. Script prepare_upgrade and websocket::accept_upgrade require network/http/websocket visibility; source.protocol/backend/request_head are immutable, and HostDriven must continue polling.

## Examples

```VeroScript
http::exchange exchange = incoming.exchange();
Network::upgrade_source source = exchange.prepare_upgrade();
websocket::options options = websocket::make_options();
options.set_timeout(10000);
websocket::operation accepting = websocket::accept_upgrade(source, options);
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](upgrade.md) · [fr-FR](../../fr-FR/Reference/upgrade.md) · [ja-JP](../../ja-JP/Reference/upgrade.md) · [ko-KR](../../ko-KR/Reference/upgrade.md) · [zh-CN](../../zh-CN/Reference/upgrade.md) · [zh-TW](../../zh-TW/Reference/upgrade.md)
