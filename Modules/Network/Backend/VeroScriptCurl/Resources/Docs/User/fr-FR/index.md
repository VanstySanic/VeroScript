# VeroScriptCurl

`vs-curl`

## Choisir et installer un fournisseur

Curl fournit clients HTTP, pool de connexions, cookies, redirections, nouvelles tentatives et flux. Passez `Curl::CreateHttpClientService()` à `Http::FClient` ou à `Http::FSseClientOptions::Service`. Le contexte doit fournir des temporisateurs et une surveillance des sockets sans en prendre possession ; Boost fournit cet exécuteur. Curl ne le choisit ni ne l’installe automatiquement. HTTPS utilise l’implémentation TLS de curl indépendamment du TLS serveur.

En script, sélectionnez explicitement `curl` pour les requêtes client HTTP ou SSE et choisissez séparément un contexte Network compatible. La sélection et l’héritage suivent [Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/fr-FR/index.md#backend-selection) ; `curl` est un identifiant de fournisseur, pas une valeur par défaut fixe.

## Utilisation du SDK natif

```cpp
#include <VeroScriptBoost.h>
#include <VeroScriptCurl.h>
#include <VeroScriptHttp.h>

VeroScript::Network::FContext context(VeroScript::Boost::CreateNetworkService());
VeroScript::Http::FClient client(context, VeroScript::Curl::CreateHttpClientService());
```

```cmake
find_package(VeroScriptBoost 0.1.1 EXACT CONFIG REQUIRED)
find_package(VeroScriptCurl 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Boost VeroScript::Curl)
```

Installez les dépendances exactes et les avis d’origine requis par le SDK et l’exécuteur choisis. Avec l’exécuteur Boost de l’exemple, suivez son [guide de déploiement](../../../../../VeroScriptBoost/Resources/Docs/User/fr-FR/index.md). Ces fournisseurs natifs n’offrent pas de transport pour navigateur.

[Network](../../../../../../VeroScriptNetwork/Resources/Docs/User/fr-FR/index.md) · [HTTP](../../../../../../VeroScriptHttp/Resources/Docs/User/fr-FR/index.md) · [WebSocket](../../../../../../VeroScriptWebSocket/Resources/Docs/User/fr-FR/index.md)

<a id="system-trust"></a>

Le curl macOS intégré active OpenSSL avec Apple SecTrust. Sans `CaFile` explicite, il utilise l’intégration de confiance native ; un fichier CA explicite sélectionne les données de confiance de cette requête. Le curl Windows intégré utilise Schannel ; les autres cibles natives utilisent leurs chemins OpenSSL configurés. Un SDK avec libcurl système suit sa configuration TLS/confiance. Ces choix sont indépendants du TLS serveur/WSS Boost.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
