# Runtime et bindings Web

La forme à espace unique reste également exacte : `void main(string[] args)` et `int main(string[] args)`.
## Factories runtime

`createVeroScriptRuntime()` est la factory Asyncify-capable pour interactive input. `createFastVeroScriptRuntime()` omet l’instrumentation Asyncify whole-program et est préférée lorsque tout input est fourni avant run. Les deux factories exposent le même contrat language/result là où les capability flags le permettent.

`runSource` est synchrone après creation de module. `runSourceInteractive` conserve VM/script state suspendu pendant qu’un input provider attend une logical line. Une seule interactive run peut être active sur un Wasm module. Une provider exception rejette la promise après que runtime a quitté input wait en sécurité.

Voir [SDK WebAssembly/WASM](../../User/fr-FR/index.md#runtime-factory-configuration).

## Inputs et résultats

Les runs acceptent file label, optional virtual file, standard input, script argument et backend `vm`, `mixed` ou `jit`. `void main(string[]
args)` et `int main(string[]
args)` sont supportés. Un résultat integer non nul reste un run réussi et est renvoyé comme `exitCode`. Un embedded NUL input est rejeté.

Le browser baseline ne publie pas script-level worker thread. Un unbounded program est arrêté en terminant son Web Worker owner. Les program indépendants utilisent runtime module ou worker indépendants.

Voir [ABI Web, artefacts et contrat de release — Capacités des programmes et des entrées](release-contract.md#web-execution-policy).

La réflexion exécutable sur les champs et les méthodes fonctionne avec les backends `vm` et `mixed`. Les nouvelles tâches à callable, comme les anciennes tâches, ne peuvent pas démarrer dans les modules navigateur publiés sans pthreads. `is_file`, `is_directory` et `remove_dir` accèdent uniquement au système de fichiers virtuel du module, y compris la bibliothèque standard intégrée, et non aux fichiers locaux du visiteur.

## Programmes compilés

`compileSource` accepte source file label et in-memory virtual file, puis retourne result et optional owned `VeroScriptProgram`. Program handle peut exécuter plusieurs fois avec nouvel input, argument et backend selection sans reparsing source graph. Chaque run a fresh execution state ; compilation diagnostic reste dans compile result.

`VeroScriptProgram.dispose()` libère native handle et est idempotent à SDK boundary. Runtime suit live program et les libère lors de runtime disposal. Interactive compiled execution exige artifact Asyncify, provider `readLine` et aucune autre interactive run active sur ce WebAssembly module.

## Surface source et compiled-program complète

Le SDK TypeScript mappe runSource vers `vero_web_run_source_with_context`, avec script argument, selected backend et virtual files in-memory appariés. compileSource mappe vers `vero_web_compile_source_with_context`; un success result possède VeroScriptProgram. VeroScriptProgram.run utilise `vero_web_run_program` et runInteractive `vero_web_run_program_interactive` réservé Asyncify. Program run reçoit fresh VM state et peut choisir input, argument, backend.

La surface C++ native propose les opérations correspondantes `FRuntime::RunSource`, `RunSourceWithInputStream`, `CheckSource`, plus `SetLanguage` et `GetAvailableLanguages`. L’ABI C offre en plus argument, option, context, compile, program, interactive et context-check entry point. Les arrays/string C ABI sont empruntés pendant leur call ; JSON/result pointer retournés suivent runtime-owned lifetime rule. VeroScriptResult sépare backend, output, error output, diagnostic, timing, program handle et per-run JIT statistic.

## Mapping des champs result et JIT

`VeroScriptResult.jit` est null pour les vérifications, la compilation, les erreurs API et les exécutions dont le chargement échoue. Une exécution chargée renvoie des compteurs même si elle est rejetée avant l’initialisation du script, notamment pour une demande explicite de `jit` sans backend. Tous les compteurs sont nuls dans les modules publiés. `directNativeCalls` et `compileAttempts` sont des statistiques, pas des versions de langage ou d’ABI.

## Mémoire gérée

Voir [SDK WebAssembly/WASM — Mémoire gérée](../../User/fr-FR/index.md#managed-memory-contract).

## Nettoyage après échec et instantanés

Un échec d’allocation de l’entrée ou un dépassement de la taille Wasm32 reprend la pile native et renvoie un résultat API en échec ; le module reste utilisable pour les exécutions suivantes. L’injection de fautes couvre les sources et les programmes compilés avec `vm` et `mixed`, ainsi que la reprise après chaque échec. Le comportement normal de fin de fichier reste identique.

Le nettoyage conserve les règles de propriété en cas d’échec : la mémoire de la ligne interactive est libérée même si sa copie lève une exception, et un programme compilé est supprimé si son résultat JSON ne peut pas être produit avant de remettre son handle à l’appelant. Une lease en lecture seule crée un seul instantané indépendant ; `toArray` le renvoie sans seconde copie complète. Les régressions vérifient la durée de vie indépendante des instantanés et la réinitialisation des variables globales de mémoire gérée après un échec d’exécution compilée.

## Compatibilité du noyau et diagnostics

Voir [SDK WebAssembly/WASM — Compatibilité du noyau et diagnostics](../../User/fr-FR/index.md#compiler-diagnostics).

## Propriété de l’entrée interactive

Les appels interactifs revérifient le module après la lecture des options. Une requête réentrante ou liée à un runtime détruit est refusée avant de prendre le contrôle du fournisseur d’entrée ; une exécution active conserve son callback et ses entrées suivantes.

Les tables de pointeurs utilisent le nombre réel de chaînes transférées, afin qu’une itération personnalisée ou changeante ne dépasse pas une allocation fondée sur la longueur initiale. Les helpers synchrones et asynchrones vérifient la limite Wasm32. La destruction parcourt les programmes suivis sans créer un second tableau.

Voir [SDK WebAssembly/WASM — Propriété de l’entrée interactive](../../User/fr-FR/index.md#input-provider-failures).

## Sortie console et capacités

Voir [SDK WebAssembly/WASM — Sortie console et capacités](../../User/fr-FR/index.md#console-capabilities).

Les noms de couleur, modes d’effacement, coordonnées du curseur, types ou nombres d’arguments invalides produisent des diagnostics avant le repli lié à l’absence de terminal. Le runtime reste réutilisable après le rejet. Le texte capturé est renvoyé dans `VeroScriptResult` à la fin de l’exécution.

## Fonctionnalités actuelles du langage

Voir [SDK WebAssembly/WASM — Fonctionnalités actuelles du langage](../../User/fr-FR/index.md#compiler-and-application-services).

Le C ABI publié et les options TypeScript n’exposent ni `FModuleEnvironment`, ni l’enregistrement d’extensions natives, ni les fournisseurs de ressources, ni `FEngineConfig::I18nServices`. Core permet l’enregistrement natif statique dans des builds C++ WebAssembly personnalisés ; les moteurs publiés ne chargent pas de bibliothèques dynamiques de bureau. Ces intégrations hôtes exigent un artefact configuré séparément. Les contrats des sources virtuelles et de propriété des programmes restent applicables.

## Correspondance des noms d’annotation

Voir [SDK WebAssembly/WASM — Correspondance des noms d’annotation](../../User/fr-FR/index.md#annotation-name-matching).

Des enregistrements d’annotation incompatibles dont les noms acceptés se chevauchent produisent un diagnostic de compilation. Les deux moteurs appliquent la même règle lors de la vérification, de l’exécution du source et de la création d’un programme compilé. Un programme compilé conserve les règles de ses définitions ; recompilez-le pour les modifier. Le C ABI Web et les options TypeScript restent inchangés. Régénérez depuis le source les anciens paquets et caches de développement après un changement du contrat de métadonnées ; les handles de programme du SDK restent en mémoire.

## Exécution Core et limites

Voir [SDK WebAssembly/WASM — Exécution Core et limites](../../User/fr-FR/index.md#numeric-and-library-limits).

Les exécutions répétées d’un programme compilé peuvent réutiliser les espaces internes tout en recréant les variables globales, locales et les arguments du script, y compris après un échec. Après un changement Core, reconstruisez les deux variantes Wasm et relancez les vérifications de publication du SDK. Les anciennes validations du SDK couvrent uniquement les artefacts enregistrés.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/runtime-and-bindings.md) · [fr-FR](runtime-and-bindings.md) · [ja-JP](../ja-JP/runtime-and-bindings.md) · [ko-KR](../ko-KR/runtime-and-bindings.md) · [zh-CN](../zh-CN/runtime-and-bindings.md) · [zh-TW](../zh-TW/runtime-and-bindings.md)
