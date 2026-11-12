# Proxy CONNECT WebSocket

Client natif CONNECT HTTP sans Http/curl ; seul http://host:port avec chemin vide ou /, sans query/fragment. Autre schéma InvalidArgument, environnement proxy ignoré/non modifié. URL/auth ≤4096 octets chacun, auth valeur complète sans contrôles. Résolution proxy/TCP/CONNECT/TLS cible pour wss/handshake partagent délai/cancel. SNI/CA/identité cible, auth au proxy seulement.

2xx réussit, ≤8 réponses 1xx ; ensuite tunnel sans corps même avec Content-Length/Transfer-Encoding, prélecture conservée. Têtes ≤64 Kio, échec/troncature/invalidité ferme. Réserve 320 Kio jusqu’à libération, hors TLS/OS complet. Client seul, Listen/AcceptUpgrade refusent. Script set_proxy/proxy_url/clear_proxy efface URL et auth, options copiées. Le navigateur a ses propres règles. proxy.vs attend un echo et paramètres proxy/cible/auth/CA optionnelle ; l’application gère son protocole.

## Exemples

```cpp
WebSocket::FOptions options;
options.ProxyUrl = "http://127.0.0.1:8080";
options.ProxyAuthorization = "Basic ..."; // Full proxy Authorization value; empty if not required.
options.Operation.Timeout = std::chrono::seconds(10);
auto operation = WebSocket::FConnection::Connect(context, "wss://example.com/socket", options);
```

```VeroScript
websocket::options options=websocket::make_options();
options.set_proxy("http://127.0.0.1:8080", "");
options.set_timeout(10000);
websocket::operation opening=websocket::connect_with_options(context,"ws://example.com/socket",options);
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/websocket-proxy.md) · [fr-FR](websocket-proxy.md) · [ja-JP](../../ja-JP/Reference/websocket-proxy.md) · [ko-KR](../../ko-KR/Reference/websocket-proxy.md) · [zh-CN](../../zh-CN/Reference/websocket-proxy.md) · [zh-TW](../../zh-TW/Reference/websocket-proxy.md)
