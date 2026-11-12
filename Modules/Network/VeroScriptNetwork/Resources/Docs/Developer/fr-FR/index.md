# Réseau: Organisation des sources

Network possède contextes opaques, opérations, quotas et contrats de flux/fichiers ; Http/WebSocket possèdent façades et liaisons. Boost possède Asio/Beast, l’intégration OpenSSL et BoostRuntime ; Curl possède le client HTTP et ses sources curl. Préservez les identités des contrats et de la fondation, les durées de vie de l’image créatrice et les blocs de contrôle des façades lors du déchargement.

Dans un hôte à registre manuel, passez FBackendCatalog::Binding() uniquement aux interfaces, enregistrez tous les fournisseurs puis appelez Update. Les fournisseurs ne doivent pas conserver ce catalogue. Le gestionnaire de paquets compose le catalogue et applique les choix du projet. Reconstruisez les images après une modification des contrats, de la fondation ou de l’identité Core requise.

`Public` · `Internal` (contrats des fournisseurs explicitement versionnés) · `Private` · `Resources/Docs` · `Resources/Examples` · `Resources/Localization` · `Resources/Tests` · `Resources/Tools/CMake`

`VeroScriptNetwork/Internal` → `VeroScript::NetworkProvider` (contextes/opérations opaques, quotas et contrats de flux et de mise à niveau).

`Modules/Network/Backend/VeroScriptBoost/Private/Runtime` → `VeroScriptBoostRuntime`; `Modules/Network/Backend/VeroScriptCurl/Private` → client HTTP. `VeroScriptNetwork/Resources/Tools/CMake` possède les aides de construction communes aux interfaces et fournisseurs. Network, Http et WebSocket possèdent chacun leur modèle de configuration de paquet dans leur propre `Resources/Tools/CMake` ; les racines de catégorie coordonnent seulement les cibles CMake.

Le SDK installe Public et les contrats opaques versionnés dans include/VeroScript*/Backend. Les structures concrètes restent privées. Ne référez jamais Private d’un autre module. Avant publication, reconstruisez providers et bindings avec le Core correspondant ; la qualification dépend des résultats réels par plateforme.

## Construction et enregistrement des modules natifs

La construction racine désactive par défaut les interfaces Network/Http/WebSocket via `VEROSCRIPT_BUILD_NETWORK`, `VEROSCRIPT_BUILD_HTTP` et `VEROSCRIPT_BUILD_WEBSOCKET`. Boost est activé par défaut lorsqu’une option réseau est choisie ; Curl suit HTTP. `VEROSCRIPT_BUILD_BOOST` et `VEROSCRIPT_BUILD_CURL` sélectionnent les fournisseurs. La construction autonome de `Modules/Network` active par défaut Boost, HTTP et WebSocket. `VEROSCRIPT_NETWORK_BUILD_BINDINGS` est activé par défaut seulement si la cible Core existe déjà ; sinon, activez-le explicitement avec un SDK Core installé exactement compatible. Curl possède `VEROSCRIPT_CURL_USE_SYSTEM_LIBRARY`, désactivé par défaut, et exige curl 8.13 ou ultérieur dans ce mode. Sa bibliothèque statique embarquée utilise PIC ; sa configuration Linux nécessite les fichiers de développement OpenSSL.

Enregistrez Network avant les protocoles et déclarez les dépendances visibles. Les hôtes statiques utilisent `QueryModule` / `GetImplementationIdentity` de chaque module ; le chargement dynamique utilise `veroscript_extension_query`. Le `ContentIdentity` statique identifie l’implémentation. N’incorporez pas un second Core au fournisseur. BoostRuntime et le socle des interfaces doivent correspondre aux images participantes ; les symboles privés et réglages des fournisseurs restent hors du SDK des interfaces.

<a id="network-execution-assembly"></a>

Core fournit le contrat d’exécution ; la VM officielle de bytecode est un backend distinct. Les hôtes qui choisissent le bytecode doivent fournir explicitement une fabrique compatible via `LoadVmBackend` / `SetVmBackendFactory`, ainsi que `FCompileOptions::ScriptCompileExtensions.VmBackendFactory` si la compilation exécute des extensions de script. Les tests de liaisons autonomes exigent `-DVEROSCRIPT_TEST_VM_LIBRARY=<chemin absolu de la bibliothèque VM>` ; un SDK Core complet contient la bibliothèque de la plateforme dans `bin/Vm`. `BUILD_TESTING=OFF` construit les liaisons sans les tests ; la couche de données seule ne nécessite pas de VM.

Vérifiez séparément SDK sans fournisseur, capacités activées/désactivées, services absents/incompatibles, sélection explicite, durées de vie statiques/dynamiques, résultats conservés, SDK déplacés et dépendances exactes. Exécutez les contrôles natifs et de plateformes lorsque cela est autorisé ; les contrôles documentaires statiques ne valident pas l’exécution.

<a id="cross-image-results"></a>

Les résultats sont extraits dans l’image productrice par `CreateTypedResult`/`OwnResult`, qui conserve `FOwnedResult`, son contexte et la propriété de la valeur. `FOperation::Result<T>()` compare nom de type, taille et alignement ; il ne dépend pas de l’identité d’adresse RTTI de `std::any_cast` entre images. Des builds C++ ABI/foundation correspondants restent requis ; ce n’est pas une ABI C portable. Reconstruisez consommateurs/fournisseurs après modification des en-têtes ou du stockage. Préservez l’extraction des callbacks par swap pour une seule libération terminale.

La sortie POSIX atomique sans remplacement préfère `renameat2(RENAME_NOREPLACE)` lorsqu’il existe et se replie sur un lien dur seulement si l’appel ou le système de fichiers n’est pas pris en charge ; conservez erreurs explicites et nettoyage.

<a id="provider-facade-ownership"></a>

L’enveloppe de façade reçoit le contexte opaque propriétaire et garde son ancrage dans l’image créatrice. Le frontend HTTP fournit la fabrique d’échanges typés pour partager le même bloc de contrôle entre `shared_from_this()` et l’upgrade HTTP. Stocker seulement le contexte comme membre d’un bloc alloué par le fournisseur ne suffit pas au déchargement sûr. Les objets retournés et résultats conservés utilisent les fabriques/helpers partagés. Le catalogue garde les fabriques par références faibles pour éviter un cycle avec l’environnement ; ne le retenez pas dans les fournisseurs.

## Préparation des ressources de localisation

L’utilitaire source `Resources/Tools/CMake/VeroScriptLocalization.cmake` sélectionne `network`, `http`, `websocket`, `network-all` et le profil indépendant `json`. Les profils HTTP/WebSocket incluent Network ; `network-all` réunit les trois modules réseau. Ce sont des entrées de construction/déploiement, pas une API SDK d’exécution. La préparation remplace le profil de destination et supprime les anciennes entrées ; choisissez toutes les racines requises avant cette étape, sans superposer les profils successivement. Le montage par un consommateur relève de la référence User sur les diagnostics.

<!-- docs-navigation -->

---

[User](../../User/fr-FR/index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
