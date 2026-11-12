# Documentation développeur — Rider

Les responsabilités communes éditeur/natif et la maintenance des six langues sont décrites dans [Intégration commune des éditeurs](../../../../../../../../Resources/Docs/Developer/fr-FR/editors.md).

Utilisez `VeroScriptBundle` / `DynamicBundle` et les ressources `.properties` localisées. Vérifiez les paramètres `MessageFormat` et les apostrophes échappées, avec substitution réelle lorsque l’exécution est autorisée.

Utilisez JDK/JVM 25, Gradle 9.5.0, Kotlin 2.4.0, plugin IntelliJ Platform 2.19.0. SDK Rider fixé à 2026.2.2 ; `sinceBuild=262.10315.191`, `strict-until-build=262.*` limite l’installation à la branche 262 ; les autres branches nécessitent une vérification distincte. `VEROSCRIPT_TEST_RUNTIME` active les tests natifs ; absent = tests ignorés explicitement. `VEROSCRIPT_TEST_JIT_BACKEND` active les tests de module réel. Structure SDK et ZIP ne remplacent pas l’acceptation UI Rider.

```sh
./gradlew test verifyPluginProjectConfiguration verifyPluginStructure buildPlugin
./gradlew verifyPlugin
```

`gradlew.bat` (Windows) · `bash ./gradlew` (Linux) · `-PlocalIdePath=/absolute/path/to/Rider`

[Standards](../../../../../../../../Resources/Docs/Standards/fr-FR/GENERAL.md)


Les régressions natives couvrent la complétion des espaces de noms et membres de `std::memory`, leur écriture dans le code, le survol, les diagnostics et le delta des jetons sémantiques sans modification. Les tests DAP utilisent le transport LSP4J du plugin pour développer buffers, vues et expressions surveillées, refuser la modification des informations d'inspection et vérifier la conservation de l'identité avec invalidation des références après un pas. Les nombres des métadonnées restent des chaînes du protocole ; `variablesReference` est un identifiant opaque, pas une adresse mémoire.

Si le nombre d’enfants est fourni, demandez des pages DAP bornées et terminez sur une réponse courte. Sinon, récupérez un instantané complet une seule fois et paginez localement ; une réponse de 100 éléments ne prouve pas la prise en charge de la pagination. Vérifiez la génération de pause et la durée de vie du nœud pour les réponses asynchrones et les rappels des pages en cache. Les régressions couvrent les listes vides, 99/100/101/250 enfants, les réponses courtes, les instantanés après reprise et un tableau réel de 100 éléments du Runtime.

Les infobulles passives du débogueur suivent uniquement les éditeurs du projet de la session. Les erreurs de reprise, de pas et de pause ne concernent que la génération de leur commande ; les échecs périmés n’affectent pas les arrêts suivants. Un échec de pause courant est signalé sans déconnexion ; la perte du transport termine toujours la session indépendamment. Des fixtures de protocole et de fenêtres contextuelles couvrent ces limites, y compris les deux ordres d’arrivée de la réponse d’échec de pause et de la notification d’arrêt.

Les régressions d’alias exportés vérifient `export using` et `export typedef` via une façade source : complétion, exclusion des alias privés, survol et navigation vers la définition. Run/Check testent aussi une façade important un `.vslib` construit par le même Runtime. Ces tests de protocole et de processus natifs ne remplacent pas l’acceptation UI Rider.

Le lexeur local reconnaît le mot-clé `constexpr` du cœur. Les variables DAP dont `presentationHint.attributes` contient `readOnly` ne proposent pas de modification ; les autres attributs d’affichage ne changent pas leur caractère modifiable, et les capacités de l’hôte ou du domaine continuent de régir l’édition. Les cas de régression couvrent les limites du mot-clé et les enfants en lecture seule, modifiables ou munis d’autres attributs d’affichage.

## Extensions de compilation et sources générées

Les valeurs `column` des frames DAP `stackTrace` sont déjà des colonnes UTF-16 commençant à 1, pour les fichiers locaux, les instantanés en lecture seule à `sourceReference` positif et les domaines live. Lorsque le texte de la ligne est disponible dans l’éditeur, le client passe seulement à une base zéro, borne la position à cette ligne et évite de séparer les paires de substitution. Il ne doit pas reconvertir des octets UTF-8. Les cas de régression des coordonnées et du contrôleur couvrent CJK, les caractères supplémentaires et CRLF.

Résolvez les références source de manière asynchrone avant de présenter les premières frames ou les pages suivantes. Mettez en cache des fichiers immuables en lecture seule par référence positive et par connexion, même si un chemin local est fourni. Excluez ces instantanés des opérations de projet et de LSP. Les échecs conservent la suspension ; les générations rejettent les résultats tardifs et la fermeture libère le cache. Les régressions natives couvrent les imports de compilation, la complétion/le survol/la navigation générés, l’entrée dans les corps générés et les macros publiques importées. Les registres natifs et `ModuleEnvironment` sont des options API de l’hôte, pas des paramètres JSON LSP. `veroscript/moduleSource` sert les modules de l’hôte ; le plugin lance un LSP autonome sans connexion à un LSP hôte. Les requêtes source en échec sont retirées du cache afin de permettre une nouvelle tentative lors du chargement suivant de la pile ; les références répétées dans une même page ne sont demandées qu’une fois.

La correspondance des noms d’annotation reste dans Tooling/Core ; le client conserve leur graphie et les libellés du serveur. Les régressions natives couvrent le changement de politique `caseSensitive` d’une définition du catalogue sans redémarrage, les annotations de compilation à casse mixte dans LSP/DAP, la complétion de `matches_name`/`case_sensitive` et les libellés des paramètres de type appelable. Un hôte de test du protocole de domaine couvre aussi un arrêt reçu avant la réponse attach, la disponibilité des capacités et la reprise suivante.

## Présentation dans l’éditeur

Le lexer de l’éditeur suit le préprocesseur : seuls LF／CRLF immédiatement après une barre oblique inverse prolongent un commentaire de ligne, sans supprimer les espaces horizontaux finaux. Les régressions couvrent la reprise des tokens, les plages tronquées, le nombre linéaire de lectures, la coloration GUI intégrée et Entrée après un commentaire ou une directive terminée. Les régressions LSP natives couvrent les valeurs appelables imbriquées, les génériques, les alias, les chaînes tableau／map et les récepteurs implicites, ainsi que le masquage privé. Les régressions DAP conservent les attributs de lecture seule des enfants variant et des expressions surveillées tout en gardant les variables locales ordinaires modifiables.

La cible de documentation utilise le transport LSP public de manière asynchrone, adapte uniquement les titres Markdown reconnus et conserve les signatures et descriptions du serveur. La cible native est désactivée pour ce descripteur afin d’éviter les doublons. Le numéro de modification du document, la validité du fichier et l’état du client rejettent les résultats périmés ; l’annulation se propage. Le moteur Markdown de l’IDE adapte les blocs de code au thème. Les fenêtres de valeurs du débogueur restent distinctes. Seules les métadonnées localisées reconnues de types intégrés associées aux catégories de types natives sont fusionnées, dans toutes les sections de résultats du serveur ; les alias des sources et les exemples délimités conservent leur contenu.

Le délégué Entrée réalise un seul remplacement dans la commande IDE, avec annulation et indentation du projet. Le lexer exclut commentaires et chaînes ; directives, en-têtes sur plusieurs lignes, sélections, curseurs multiples et analyses dépassant le premier Mio, suffixe de la ligne courante compris utilisent Entrée normale. La préférence d’accolades ne redémarre pas LSP. Formatage et diagnostics restent gérés par le serveur. Les tests couvrent rendu, invalidation asynchrone, curseur, annulation et diagnostics natifs pendant la complétion d’une fonction. L’acceptation dans l’interface Rider reste une étape distincte.

## Configuration du contexte

Les configurations contextuelles acceptent les fichiers locaux enregistrés,
normalisent le modèle en Run/Check et effacent les champs d’attachement. Check
retire aussi arguments, entrée et chemin JIT, en conservant les paramètres
compatibles de répertoire, environnement et compilation. La validation propage
l’annulation IDE inchangée, sans la convertir en erreur de configuration.

Check passe toujours `--backend=vm` sans changer la préférence de backend Run enregistrée.

<a id="rider-platform-sdk"></a>

Sans `localIdePath`, les deux projets Rider résolvent l’archive SDK multi-OS avec `rider(platformVersion) { useInstaller = false }` ; l’installateur plateforme n’est pas le SDK de développement. `-PlocalIdePath=<directory>` sélectionne un IDE local existant. Préservez versions/branche Rider, dépendances de plugins et JDK ci-dessus, et validez chaque OS séparément.

<!-- docs-navigation -->

---

[Documentation développeur](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
