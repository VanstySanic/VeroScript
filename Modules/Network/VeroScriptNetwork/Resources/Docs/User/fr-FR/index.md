# Network

Installez les interfaces utilisées et leurs fournisseurs natifs. `vs-boost` fournit `network` et les services serveur HTTP/WebSocket activés ; `vs-curl` fournit `http-client`. Une interface seule ne fournit aucun transport. Un service absent ou incompatible produit une erreur.

<a id="backend-selection"></a>

En script, `Network::create_context_with_backend("boost", false)` sélectionne explicitement l’exécuteur. Les options HTTP client/serveur, SSE et WebSocket utilisent `set_backend` pour remplacer le choix de l’environnement. Une requête vide hérite de `services.<service>.backend` ; sans configuration, `auto` essaie les fournisseurs activés dans l’ordre de leur ID. Un `auto` explicite remplace le choix configuré ; `none` ou une valeur de configuration explicitement vide désactive le service. Un service requis absent échoue clairement ; une erreur d’exécution ne rejoue pas l’opération sur un autre backend. `boost` et `curl` sont des ID de fournisseurs, pas des valeurs fixes par défaut. Les consommateurs C++ fournissent des instances de service typées ; ces mots de sélection du script ne remplacent pas le contrat de correspondance des instances des options C++.

`vs-network` · `VeroScript::Network`

- [Achèvement et durée de vie du programme](Reference/completion.md)
- [Diagnostics et localisation](Reference/diagnostics.md)
- [Fichiers et sortie atomique](Reference/files.md)
- [Référence réseau](Reference/README.md)
- [Flux d’octets bornés](Reference/streaming.md)
- [Configuration TLS](Reference/tls.md)

[Boost](../../../../../Backend/VeroScriptBoost/Resources/Docs/User/fr-FR/index.md) · [Curl](../../../../../Backend/VeroScriptCurl/Resources/Docs/User/fr-FR/index.md)

<!-- docs-navigation -->

---

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
