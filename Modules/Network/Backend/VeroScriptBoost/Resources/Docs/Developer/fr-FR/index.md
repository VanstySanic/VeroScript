# VeroScriptBoost

Boost maintient Asio/Beast, l’intégration OpenSSL et `Private/Runtime` (`VeroScriptBoostRuntime`). Les implémentations concrètes et les inventaires des sources tierces appartiennent à ce module ; le SDK frontal reste indépendant de ces types.

La propriété commune des contextes/opérations, la composition du registre, les frontières SDK et les options de compilation sont décrites dans [Network](../../../../../../VeroScriptNetwork/Resources/Docs/Developer/fr-FR/index.md).

[CMakeLists.txt](../../../../CMakeLists.txt) · [README](../../../../README.md) · [Utilisateur](../../User/fr-FR/index.md)

<a id="provider-registration"></a>

Le fournisseur `boost` enregistre `network` (`veroscript.network`), puis `http-server` (`veroscript.http.server`) et `websocket-client` / `websocket-server` (`veroscript.websocket`) si ces cibles sont activées. Les contrats sont en version `0.1.1`, avec respectivement `Network::Backend::IService*`, `Http::Backend::IServerService*` et `WebSocket::Backend::IService*`. Les fabriques sont sans état, l’enregistrement ne démarre aucune E/S et une configuration inconnue est refusée. La découverte dynamique utilise `veroscript_extension_query`, la statique `veroscript_boost_query` / `Boost::QueryModule`. Seul Boost possède `VeroScriptBoostRuntime` ; ni les façades ni Curl n’en installent une seconde copie. Vérifiez métadonnées des protocoles activés/désactivés, leases et consommateurs déplacés avec les identités foundation/Core correspondantes.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
