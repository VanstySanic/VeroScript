# VeroScriptBoost

`vs-boost`

## Choisir et installer un fournisseur

Boost fournit DNS, TCP et UDP ainsi que les fonctions serveur HTTP/WebSocket incluses dans le SDK installé. Passez `Boost::CreateNetworkService()` à `Network::FContext`, `CreateHttpServerService()` dans `Http::FServerOptions::Service` et `CreateWebSocketService()` dans `WebSocket::FOptions::Service`. Les serveurs et WebSocket exigent un contexte compatible.

En script, sélectionnez explicitement `boost` pour son service Network, serveur HTTP ou WebSocket installé. La sélection et l’héritage suivent [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/fr-FR/index.md#backend-selection) ; `boost` est un identifiant de fournisseur, pas une valeur par défaut fixe.

## Utilisation du SDK natif

```cpp
#include <VeroScriptBoost.h>
#include <VeroScriptNetwork.h>

VeroScript::Network::FContext context(VeroScript::Boost::CreateNetworkService());
```

```cmake
find_package(VeroScriptBoost 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Boost)
```

Installez toutes les dépendances exactes des paquets sélectionnés avec leurs notices originales. Boost possède `VeroScriptBoostRuntime` ; sous Windows, sa DLL compatible doit accompagner l’exécutable ou les modules natifs. Linux/macOS utilisent les chemins relatifs installés. iOS utilise l’enregistrement statique dans une même image. Ces fournisseurs natifs ne fournissent aucun transport navigateur.

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/fr-FR/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/fr-FR/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/fr-FR/index.md)

<a id="system-trust"></a>

Avec `bUseSystemTrust` activé côté client, Boost ajoute les ancres système au contexte OpenSSL : magasin ROOT Windows ou ancres système macOS, en plus des chemins OpenSSL par défaut. macOS importe les certificats d’ancrage ; la vérification reste celle du transport OpenSSL. Fournissez `TrustPem` pour des racines supplémentaires explicites, ou désactivez la confiance système pour utiliser seulement les vôtres. Vérifiez la disponibilité du magasin et le déploiement sur le système cible.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
