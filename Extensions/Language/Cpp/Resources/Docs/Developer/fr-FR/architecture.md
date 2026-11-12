# Architecture du groupe C++

## Direction des dépendances

Le groupe C++ est une couche d’adaptateurs et d’outillage au-dessus de `VeroScript/`. `VeroScriptCpp` fournit embedding/session, `VeroScriptCppAot` dépend directement du cœur, et `VeroScriptCppFrontend` possède le modèle partagé de déclarations C++17. `VeroScriptCppBindGen` et `VeroScriptCppTranspiler` le consomment indépendamment et ne modifient jamais l’AST frontend pour faciliter leur lowering. Le cœur n’inclut pas d’en-tête d’outil C++. `VeroScriptInterop` est une ABI C stable distincte, non une dépendance d’implémentation du frontend, BindGen ou Transpiler. La couche embedding peut utiliser les interfaces runtime/binding publiques du cœur.

## Organisation des sources

Chaque module natif expose les en-têtes pris en charge depuis `Public/` et place implémentation/tests dans `Private/`. CMake découvre les sources privées récursivement, exclut les tests ciblés des bibliothèques et crée des exécutables de test séparés. Un en-tête public compile sans chemin include privé.

## Modèle de sortie partagé

Les diagnostics frontend contiennent code, message, position et données token/déclaration, préservés par BindGen/Transpiler. Les opérations projet rassemblent tous les inputs avant de produire catalogue, références, source maps ou manifests ; elles refusent le même chemin de fichier et remplacent les sorties après succès complet seulement.

## Règles de changement

Une modification de struct/enum public, position source, format généré, target CMake ou règle de sous-ensemble met à jour tests ciblés et page concernée. Les en-têtes publics et tests font foi, non le README.

## Frontière du débogage natif

`VeroScriptSessionDebug.h` définit le pont Session C++ utilisé par un hôte ou un domaine actif Tooling. Session gère son enregistrement et la durée de vie du débogueur partagé autour des VM réellement exécutées. Remplacement, restauration du programme et destruction notifient le pont ; les VM de nettoyage n’héritent pas du débogueur. Les vues du programme, des bindings, des sources et du contexte sont empruntées. L’hôte copie les métadonnées synchronisées et traite les commandes en attente sur le thread d’exécution, arrêté à un point sûr.

Cpp ne dépend pas de Tooling. Tooling gère le registre des domaines, DAP et le transport ; l’intégration UE gère éditeur/PIE, durée de vie des objets et pause du thread de jeu. Cette interface modifie la disposition C++ de Session : recompilez les hôtes natifs avec le SDK correspondant.

`VeroScriptCppSessionDebugTests` couvre mutation des arguments, appels d’objet, rappels conservés, sélection de l’interpréteur, restauration de rechargement, nettoyage d’exception et remplacement/retrait du pont. `VeroScriptCppJitContractTests` vérifie les transitions de backend avec une fabrique publique indépendante. Le consommateur SDK utilise les en-têtes et bibliothèques exportés. Les exemples TCP sont vérifiés dans un processus enfant avec délais et arrêt bornés ; ne détachez pas un thread serveur qui emprunte une Session locale.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/architecture.md) · [fr-FR](architecture.md) · [ja-JP](../ja-JP/architecture.md) · [ko-KR](../ko-KR/architecture.md) · [zh-CN](../zh-CN/architecture.md) · [zh-TW](../zh-TW/architecture.md)
