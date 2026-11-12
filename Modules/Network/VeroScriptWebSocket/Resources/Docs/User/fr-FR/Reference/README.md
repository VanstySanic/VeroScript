# WebSocket — Référence réseau

Les règles communes d’installation des interfaces et de sélection des fournisseurs sont définies dans [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/fr-FR/index.md#backend-selection).

`vs-websocket` dépend du `vs-network` correspondant. Son ID natif et son espace de noms script sont `websocket` ; la cible CMake est `VeroScript::WebSocket`. Les liaisons facultatives `VeroScript::WebSocketBindings` exigent le Core correspondant. Chargez Network avant WebSocket et déclarez les dépendances visibles. Http, curl et Json ne sont pas obligatoires ; la composition JSON utilise `COMPONENTS Json`.

`FConnection` fournit les clients/serveurs ws/wss, les messages texte/binaires, la compression facultative, Pong automatique et des files bornées. Les lectures natives continuent sans réception script en attente, afin de traiter Ping et fermeture. Les délais, la distribution des rappels, les quotas et les durées de vie suivent Network. Un résultat de type incorrect, inachevé ou en échec est refusé ; les options sont des instantanés de valeurs. Déployez BoostRuntime avec les fournisseurs et faites correspondre Core, build, plateforme et chaîne d’outils de tous les composants natifs.

- [Achèvement et durée de vie du programme](completion.md)
- [Diagnostics et localisation](diagnostics.md)
- [Adaptateurs JSON](json-adapters.md)
- [Configuration TLS](tls.md)
- [Compression WebSocket](websocket-compression.md)
- [Proxy CONNECT WebSocket](websocket-proxy.md)
- [Reconnexion WebSocket bornée](websocket-reconnect.md)
- [Messages et contrôles WebSocket](websocket.md)

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
