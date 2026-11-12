# Passage de HTTP à WebSocket

Le pont facultatif permet à Http et WebSocket de ne dépendre que de Network ; WebSocket autonome n’exige pas Http. `exchange.PrepareUpgrade` renvoie un `Network::FUpgradeSource` possédé qui retient l’exchange et ses métadonnées immuables : en-tête, protocole, backend, contrat et génération. Il ne transfère pas la connexion et n’envoie pas 101. `AcceptUpgrade(context, source, options)` renvoie une opération de connexion ordinaire. Le contexte et le backend doivent correspondre ; le transfert est unique et le délai de soumission couvre vérifications, file et handshake. Préparer la source ne prolonge pas le délai de réponse HTTP.

HTTP1.1 sans corps/Transfer-Encoding/Content-Length non nul, exchange original partagé requis. Avant transfert : options/contexte/contrat, têtes ≤64 Kio, GET/Host/Upgrade/Connection/version13/key16octets/sous-protocoles. Corps de requête stream terminé ; réponse en cours/finie/génération ancienne/fermée refusée. Pré-échec ou annulation avant transfert garde une réponse HTTP possible ; après transfert, échec ferme sans restauration/seconde réponse. Annulation peut concurrencer transfert/101 : examinez IsConsumed/consumed.

Même exécuteur/slot de connexion, octets pré-lus conservés et TLS déplacé sans reconnexion. Sources/leases/handshake/résultats gardent Owners/quota ; libérer ancien échange ne ferme pas WS. Http.Close/Shutdown ne touche plus la connexion transférée, context.Shutdown oui. Allocation peut fermer contexte, toujours nettoyer normalement. Script prepare_upgrade/accept_upgrade exige dépendances réseau/http/ws ; protocol/backend/request_head immuables et HostDriven toujours piloté.

## Exemples

```VeroScript
http::exchange exchange = incoming.exchange();
Network::upgrade_source source = exchange.prepare_upgrade();
websocket::options options = websocket::make_options();
options.set_timeout(10000);
websocket::operation accepting = websocket::accept_upgrade(source, options);
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/upgrade.md) · [fr-FR](upgrade.md) · [ja-JP](../../ja-JP/Reference/upgrade.md) · [ko-KR](../../ko-KR/Reference/upgrade.md) · [zh-CN](../../zh-CN/Reference/upgrade.md) · [zh-TW](../../zh-TW/Reference/upgrade.md)
