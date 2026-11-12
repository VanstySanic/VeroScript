# Http — Référence réseau

Les règles communes d’installation des interfaces et de sélection des fournisseurs sont définies dans [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/fr-FR/index.md#backend-selection).

vs-http 0.1.1 exige vs-network correspondant, sans WebSocket/JSON. ID/namespace http, cible VeroScript::Http ; Bindings ajoute VeroScript::HttpBindings avec Core compatible. Chargez network avant http et déclarez les dépendances ; pas de types curl/Boost publics.

Clients/serveurs tamponnés ou en flux, routes, multipart, SSE et fichiers utilisent create_client/create_request/listen/create_response et résultats typés. NUL conservé, options copiées puis figées à la soumission. Délais/dispatch propriétaire/durée de vie/quota suivent Network. 4xx/5xx ne signifie pas seul un échec de transport. Les setters remplacent les modes de corps incompatibles ; form active plus/espace explicitement, multipart conserve répétitions/octets. response_builder/reply_response pour réponse tamponnée, add_header/set_header pour ajout/remplacement. Reader/encodeur SSE n’ajoute pas de reconnexion. Le C++ COMPONENTS Json est facultatif.

- [Politique du client HTTP](client-policy.md)
- [Achèvement et durée de vie du programme](completion.md)
- [Diagnostics et localisation](diagnostics.md)
- [Fichiers et sortie atomique](files.md)
- [Chaînes de réponses et statistiques HTTP](http-trace.md)
- [Adaptateurs JSON](json-adapters.md)
- [Envoi et analyse multipart](multipart.md)
- [Routage HTTP](routing.md)
- [Client SSE](sse-client.md)
- [Flux d’octets bornés](streaming.md)
- [Configuration TLS](tls.md)
- [Passage de HTTP à WebSocket](upgrade.md)

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
