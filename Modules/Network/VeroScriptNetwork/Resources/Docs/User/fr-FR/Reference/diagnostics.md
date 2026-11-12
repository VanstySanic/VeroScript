# Diagnostics et localisation

Network/Http/WebSocket possèdent network.json/http.json/websocket.json dans les six Resources/Localization/<locale>. FError::Id() est stable, sans allocation, vide au succès et network.unknown pour enum inconnu. Les transports partagent ce domaine. operation.message_id/error_code/error donne ID/code/détail ; une opération en attente peut aussi avoir ID vide, vérifiez état/succès. Flux, SSE, multipart et historiques suivent ces règles.

Argument 0 : détail brut. Catégories traduites, détails moteur conservés ; jamais de branche sur texte traduit/OS. Les erreurs synchrones utilisent network.binding/http.binding/websocket.binding et FScriptError.MessageId/MessageArgs. Erreur initiale préservée même si la présentation ne s’alloue pas. SDK natifs sans Core/langue globale.

Montez `share/VeroScript<Module>/Resources/Localization` de chaque module installé dans le résolveur de ressources de l’hôte. Http et WebSocket ont aussi besoin du `network.json` de leur dépendance Network, sans le dupliquer. Un hôte combiné peut monter les répertoires séparément ou déployer une racine fusionnée conservant tous les fichiers de composants requis.

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/diagnostics.md) · [fr-FR](diagnostics.md) · [ja-JP](../../ja-JP/Reference/diagnostics.md) · [ko-KR](../../ko-KR/Reference/diagnostics.md) · [zh-CN](../../zh-CN/Reference/diagnostics.md) · [zh-TW](../../zh-TW/Reference/diagnostics.md)
