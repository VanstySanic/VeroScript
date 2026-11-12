# VeroScriptCurl

Curl maintient le client HTTP concret dans `Private/HttpClient.cpp`, la configuration de curl et ses avis d’origine. Son fournisseur implémente le contrat Http ; il ne maintient pas le transport Boost ni les services WebSocket.

La propriété commune des contextes/opérations, la composition du registre, les frontières SDK et les options de compilation sont décrites dans [Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/fr-FR/index.md).

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [Utilisateur](../../User/fr-FR/index.md)

<a id="provider-registration"></a>

`curl` enregistre `http-client` / `veroscript.http.client` / `0.1.1` avec une fabrique `Http::Backend::IClientService*`. Les fabriques sont sans état ; l’enregistrement ne démarre pas d’E/S et refuse la configuration inconnue. La découverte dynamique utilise `veroscript_extension_query`, la statique `veroscript_curl_query` / `Curl::QueryModule`. Curl consomme les hooks opaques non propriétaires de disponibilité des sockets et de timers, sans dépendre des en-têtes ou bibliothèques Boost. HTTPS suit la propre configuration TLS de curl. Gardez correctifs curl, déploiement des dépendances statiques et exigences système Unix dans ce module ; vérifiez services absents/incompatibles, leases et consommateurs SDK/paquets déplacés pour le build réel.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
