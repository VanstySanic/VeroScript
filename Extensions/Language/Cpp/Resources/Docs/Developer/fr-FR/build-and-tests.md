# Contrat de build et tests C++

## Cibles

Les targets sont `VeroScriptCpp`, `VeroScriptCppAot`, `VeroScriptCppFrontend`, `VeroScriptCppBindGen` et `VeroScriptCppTranspiler`. Chaque bibliothèque exclut ses sources de test ciblées et les tests lient le target public exercé.

## Catégories de test

Les tests couvrent compilation d’en-tête public, intégration engine/session/JIT, source/Bundle AOT, parse/fusion/mapping de type frontend, catalogue/références BindGen et source/map Transpiler. Tout changement de résultat public, artefact, diagnostic ou sous-ensemble étend la catégorie de test correspondante.

## Frontière d’installation

Les en-têtes publics et bibliothèques générées sont des entrées d’installation. README et références API générées sont des artefacts documentaires, pas des en-têtes d’implémentation. Un consommateur installé ne dépend pas du build tree.

Le package C++ distribué fournit `VeroScriptCppConfig.cmake` sous le module `Runtime`. Le consommateur ajoute la racine Core SDK correspondante et `Runtime` du package C++ à `CMAKE_PREFIX_PATH`, appelle `find_package(VeroScriptCpp <version> EXACT CONFIG
REQUIRED)` et lie `VeroScript::VeroScriptCpp`. `Tools` et `Aot` n’exposent des targets importés que si leurs modules voisins sont installés. Le package enregistre l’identité SHA-256 de la bibliothèque Core de la même release ; mélanger un Core SDK de même version issu d’un autre build doit échouer à la configuration. Avant archive/installeur, la pipeline compile et exécute `Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal` uniquement avec les chemins staged, jamais avec les chemins du dépôt.

## Validation de l'intégration mémoire

`VeroScriptCppMemoryIntegrationTests` vérifie les quotas Engine, les appels
Session successifs, les services natifs, l'identité après rechargement et la
restauration après dépassement de quota, le nettoyage avant fermeture du domaine,
et la libération des baux après destruction. `CppMinimal/memory.cpp` est le
consommateur Runtime installé. Exécutez les deux avec `VeroScriptMemoryTests`,
les tests Session/debug/JIT et les refus AOT lors d'un changement de contrat.
L'argument backend optionnel teste un vrai module JIT.
Voir les [règles mémoire hôte](../../User/fr-FR/embedding.md).

## Régressions de nettoyage et AOT

`VeroScriptCppSessionDebugTests` couvre la réentrée des callbacks lors de l’effacement, du remplacement, de la désactivation du rechargement à chaud, de la libération des fonctions et de la destruction, ainsi que le refus de nouvelles racines et la réutilisation après nettoyage. `VeroScriptCppJitContractTests` vérifie l’invalidation du cache lors des inscriptions natives structurées. Exécutez `VeroScriptCppAotHelloTests` avec `VEROSCRIPT_AOT_REQUIRE_NATIVE=1` avant publication : il compile et exécute le C++ généré, compare les résultats de la VM et contrôle les effets de bord, les durées de vie, les largeurs numériques génériques et les échecs de débordement de l’incrémentation/décrémentation. `VeroScriptCppAotEmitTests` vérifie aussi le refus explicite des opérations non prises en charge sur les types utilisateur, y compris les spécialisations génériques.

## Validation de l'intégration des macros de modules

`VeroScriptCppEngineIntegrationTests` couvre les macros initiales de l'hôte, les overlays, la visibilité explicite/réexportée/implicite, les caches de sources et de fichiers, leur invalidation, le rechargement à chaud, la conservation du programme après un échec de dépendance, les diagnostics physiques et les macros de bibliothèques sans informations de débogage. `VeroScriptCppAotHelloTests` compare le code natif à la VM pour les macros variadiques publiques et leur expansion chez l'appelant. `CppMinimal/main.cpp` utilise les métadonnées de macros via les en-têtes du SDK préparé. Exécutez ces validations lorsque le prétraitement des modules ou le bytecode change.

## Validation de l'intégration des annotations

`VeroScriptCppAnnotationTests` vérifie validation/exécution Engine, chargement source/fichier et rechargement à chaud Session, diagnostics non fatals, caches de dépendances et processeurs non reproductibles sur VM/Mixed/JIT réel. Il vérifie aussi le refus de réentrée des callbacks pendant les chargements, la compilation séparée et le rechargement, ainsi que la conservation d'état et la réutilisation après annulation. `VeroScriptCppAotEmitTests` garantit qu'un budget de génération augmenté survit sans seconde préparation par défaut. `VeroScriptCppAotHelloTests` compare en natif les corps virtuels générés, les corps génériques séparés et les portées de déclaration/implémentation. Le consommateur SDK vérifie les en-têtes installés, l'enregistrement des processeurs et les API Session/AOT.

## Transpilation de projets et transactions de fichiers

`VeroScriptCppTranspilerTests` vérifie les modules finaux, la portée des implémentations, les appels virtuels, les paramètres par défaut et les diagnostics dans les fichiers d’origine. `VeroScriptCppTranspilerNativeTests` compile et exécute C++ → VeroScript → C++. `VeroScriptCppFrontendTests` couvre les syntaxes refusées, les chemins normalisés, les liens physiques et les collisions avant écriture. `FFilePathSet` est partagé par les lots, la protection des entrées CLI et les transactions ; les identités indisponibles conservent les comparaisons précédentes.

## Vérifications de l’intégration du terminal hôte

`VeroScriptCppConsoleTests` vérifie les capacités du terminal, la sortie de texte exacte et le regroupement de `print`, le refus des permissions, les échecs des rappels hôtes, la restauration du style et du curseur après une erreur ou l’épuisement des instructions, les appels Session répétés et les flux ordinaires, avec VM, Mixed et un backend JIT réel. Les comparaisons native/VM de `VeroScriptCppAotHelloTests` vérifient l’ordre d’évaluation des arguments de couleur et de curseur, y compris les effets avant un échec de validation. `CppMinimal/main.cpp` du SDK préparé vérifie l’interface publique et la restauration après exécution. Exécuter ces vérifications lors d’un changement du contrat du terminal.

La vérification native couvre aussi les entrées avec et sans arguments de ligne de commande : échec de l’adaptateur, exceptions standard ou non standard, et échec du flux de diagnostics doivent produire un code d’échec et terminer le nettoyage.

Les vérifications des threads de travail contrôlent aussi que les exceptions standard ou non standard pendant la construction du garde de terminal font échouer la tâche, et que la restauration du terminal précède la notification de fin.

## Vérification des évaluations et références AOT

Les comparaisons natives/VM de `VeroScriptCppAotHelloTests` vérifient l’ordre des arguments et les conversions des appels ordinaires, génériques, d’extension et de construction, les copies de valeurs, les paramètres ref, les opérateurs à court-circuit, les affectations composées et l’initialisation des callable globaux. Les valeurs de fonction sont évaluées après les arguments, et les récepteurs d’instance avant, comme dans la VM. Les références indexées restent valides lorsque les arguments suivants agrandissent ou réaffectent un conteneur. Une conversion d’index invalide échoue avant les effets des arguments suivants. Les expressions scalaires pures restent directes ; les valeurs capturées sont déplacées dans les appels pour éviter une copie supplémentaire.

Les régressions d’affectation vérifient aussi que le résultat reste une valeur valide après la destruction du récepteur sauvegardé. Une écriture indexée simple réaffecte le conteneur sauvegardé à sa variable ou à son champ ; les indices linéaires imbriqués et les variables globales qualifiées résolvent leur chemin de stockage actuel. Une réaffectation dans le membre droit doit préserver les mêmes alias et identités d’objet que la VM. Les limites d’une affectation indexée simple sont vérifiées après les effets du membre droit ; la conversion des indices d’un chemin de référence les précède. Les écritures scalaires sans conversion restent directes pour éviter la copie d’un tableau de taille fixe.

Le consommateur `CppMinimal` déploie la bibliothèque standard et les ressources de localisation du SDK Core correspondant à côté de l’exécutable à chaque compilation, même sans nouvelle édition de liens. Le déploiement doit restaurer les fichiers manquants et actualiser les ressources du SDK modifiées avec les générateurs Visual Studio et Ninja/Make.

Voir [AOT C++ — Retours par référence et durée de vie](../../User/fr-FR/aot.md#reference-return-lifetime).

Les alias indexés persistants et les références indexées renvoyées nécessitent des chemins de stockage gérés. Les références persistantes obtenues depuis des récepteurs temporaires de type valeur nécessitent aussi des propriétaires gérés. AOT rejette explicitement ces cas ; utilisez la VM.

Les champs implicites de structure reçoivent les valeurs initiales des champs du script, y compris pour les types génériques et les tableaux fixes. Cette initialisation du stockage n’exécute pas le corps du constructeur de la structure.

Le contrôle natif vérifie les alias locaux de champs conservés, la durée de vie des récepteurs sans argument, l’évaluation unique des cibles d’affectation par référence et les contrôles numériques de dépassement et de réduction. Les tests de génération exigent le rejet explicite des récepteurs nécessitant un nettoyage et des alias locaux renvoyés. Les définitions de membres, destructeurs, champs statiques et modèles ouverts suivent les types complets et les déclarations des accesseurs globaux ; le contrôle couvre les accès globaux anticipés ainsi que les membres et champs statiques des modèles contraints.

Le contrôle vérifie aussi les alias de champs directs et imbriqués, les références dans les initialisations for, les récepteurs de membres et capturés, les champs génériques et le stockage initial des champs de structure. Les tests de génération rejettent le stockage indexé persistant et les alias de récepteurs temporaires de type valeur. Les contrôles récursifs de nettoyage prennent un instantané des noms de types avant les recherches pouvant enregistrer des spécialisations génériques.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/build-and-tests.md) · [fr-FR](build-and-tests.md) · [ja-JP](../ja-JP/build-and-tests.md) · [ko-KR](../ko-KR/build-and-tests.md) · [zh-CN](../zh-CN/build-and-tests.md) · [zh-TW](../zh-TW/build-and-tests.md)
