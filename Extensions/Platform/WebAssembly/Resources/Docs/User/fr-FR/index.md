# SDK WebAssembly/WASM

`VeroScriptWebAssembly` est un SDK WebAssembly sans interface graphique pour les navigateurs et les autres hôtes JavaScript. Il vérifie et exécute du code en mémoire ; l'affichage, l'éditeur et l'intégration au framework sont du ressort de l'hôte.

<a id="runtime-factory-configuration"></a>

Les factories TypeScript copient la configuration de l’appelant avant de créer une instance Emscripten. Un même objet d’options, même gelé, peut servir à créer des runtimes indépendants sans partager leurs exports, leur mémoire ou la propriété des ressources. Les options déclarées `locateFile` et `wasmBinary` sont aussi conservées lorsqu’elles sont héritées.

## Artefacts et déploiement

Utilisez ensemble les artefacts issus de la même construction :

| Artefact | Rôle |
| --- | --- |
| `veroscript-engine.mjs` | Module Emscripten avec prise en charge de l'entrée interactive. |
| `veroscript-engine.wasm` | Binaire WebAssembly du runtime normal. |
| `veroscript-engine-fast.mjs` | Module sans instrumentation Asyncify pour les exécutions à faible latence. |
| `veroscript-engine-fast.wasm` | Binaire WebAssembly du runtime rapide. |
| `veroscript-webassembly.ts` | Wrapper TypeScript. |
| `veroscript-webassembly.d.ts` | Déclarations TypeScript du wrapper et du C ABI Web. |
| `veroscript-engine.d.mts`, `veroscript-engine-fast.d.mts` | Déclarations des options de module Emscripten. |

La bibliothèque standard et les catalogues de localisation sont intégrés au système de fichiers virtuel du module. Les modules `.mjs`, `.wasm`, les déclarations et l'ABI doivent rester appariés ; les artefacts générés ne doivent pas être modifiés à la main. Le point d'entrée TypeScript charge à la demande le moteur Emscripten choisi : l'import du wrapper ne télécharge aucun moteur, le runtime rapide ne charge que le moteur rapide et le runtime normal ne charge que le moteur interactif.

## Runtime normal et runtime rapide

Utilisez `createVeroScriptRuntime` si le script peut attendre une entrée asynchrone. Utilisez `createFastVeroScriptRuntime` lorsque l'entrée est déjà disponible :

```TypeScript
const fast = await createFastVeroScriptRuntime();
console.log(fast.supportsInteractiveInput); // false
fast.dispose();

const normal = await createVeroScriptRuntime();
console.log(normal.supportsInteractiveInput); // true
normal.dispose();
```

Le runtime rapide ne contient pas l'instrumentation Asyncify complète et rejette `runSourceInteractive`. Le runtime normal suspend le script jusqu'à la résolution de la promesse `readLine`.

Pour les scripts utilisant `read_line`, les fonctions de saisie typée ou `pause`, utilisez le runtime normal et `runSourceInteractive`. Le runtime rapide convient lorsque les données d’entrée sont déjà disponibles et ne permet pas d’attendre une saisie asynchrone.

## Backends et API

`checkSource` valide la source sans appeler `main` ; `runSource` la valide puis
exécute. Les indicateurs `supportsCompiledPrograms` et `supportsThreads`
permettent de détecter les capacités de l'artefact ; les versions publiées
renvoient respectivement `true` et `false`. Le fast runtime ne prend pas en
charge l'entrée interactive.

Les modules publiés ne sélectionnent aucun backend JIT. `vm` interprète le bytecode ; `mixed` reste le défaut et utilise également la VM. `jit` renvoie le diagnostic structuré `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory` avant les initialisations applicatives, `main` ou une demande de saisie. Les appelants qui utilisaient `jit` doivent choisir `vm` ou `mixed`. Les noms des politiques et les identifiants C ABI restent stables ; `result.backend` indique la politique demandée et tous les compteurs JIT sont nuls.

Les types publics principaux sont `VeroScriptDiagnosticSeverity`, `VeroScriptExecutionBackend`, `VeroScriptLanguage`, `VeroScriptDiagnostic`, `VeroScriptResult`, `VeroScriptJitStatistics`, `VeroScriptVirtualFile`, `VeroScriptSourceOptions`, `VeroScriptRunOptions`, `VeroScriptInputRequest`, `VeroScriptInteractiveRunOptions`, `VeroScriptProgram`, `VeroScriptCompileResult`, `VeroScriptProgramRunOptions`, `VeroScriptProgramInteractiveRunOptions`, `VeroScriptRuntime` et `VeroScriptEngineModuleOptions`. `VeroScriptInputRequest.index` est l'index de requête d'entrée, à partir de zéro. `VeroScriptProgram` expose `disposed`, `run`, `runInteractive` et `dispose`. Les fonctions de création sont `createVeroScriptRuntime(options?)` et `createFastVeroScriptRuntime(options?)`; `runtime.supportsCompiledPrograms` indique si la compilation réutilisable est disponible.

`checkSource` vérifie sans appeler `main`; `runSource` vérifie puis exécute. Le résultat contient `abiVersion`, `operation`, `success`, `exitCode`, `exitRequested`, `backend`, `output`, `errorOutput` et `diagnostics`. Il peut aussi contenir `programHandle`, `timings` (`timings.compileMs` et `timings.executeMs`) et `jit`. Les statistiques JIT exposent `compileAttempts`, `compiledEntries`, `bridgeTrampolineEntries`, `linearTrampolineEntries`, `vmTrampolineEntries`, `unsupportedEntries`, `dispatchAttempts`, `dispatches`, `fallbacks`, `hotFunctions` et `directNativeCalls`. Ce dernier compte les appels JIT à portée de génération qui contournent le pont de valeurs de la VM. `success` n'est pas synonyme de `exitCode === 0` et `exitRequested` n'est vrai qu'après un appel explicite à `exit(n)`.

Les appels de méthodes liées et les autres réentrées natives dans la VM sont soumis à la protection de pile du cœur, propre à la plateforme. Une imbrication excessive renvoie `diag.call_stack_overflow`. Cette protection est distincte de la limite de profondeur de la récursion interprétée ordinaire. Réduisez les réentrées natives imbriquées avant de réessayer.

## Compiler une fois, exécuter plusieurs fois

Pour une source inchangée exécutée plusieurs fois, appelez `compileSource` une
seule fois et réutilisez le programme retourné :

```TypeScript
const compiled = runtime.compileSource(
	`int main(string[] args)
{
	return len(args);
}`,
	{ fileLabel: "main.vs" },
);

if (compiled.program) {
	const first = compiled.program.run({ args: ["one", "two"], backend: "mixed" });
	const second = compiled.program.run({ args: ["one"], backend: "vm" });
	console.log(first.exitCode, first.timings?.executeMs, first.jit?.dispatches);
	console.log(second.exitCode, second.timings?.executeMs);
	compiled.program.dispose();
}
```

La compilation accepte `fileLabel` et `files`, mais pas l'entrée, les arguments
ou le backend d'exécution. Chaque appel à `program.run` reçoit un état neuf et
peut choisir ses propres paramètres ; le bytecode compilé est partagé, tandis
que l'état VM et les statistiques JIT sont propres à l'exécution. Le runtime
normal prend aussi en charge `program.runInteractive({ args, backend, readLine })`;
le runtime rapide ne prend pas en charge l'entrée interactive. Vérifiez
`supportsCompiledPrograms` et `supportsInteractiveInput` lorsque les artefacts
peuvent provenir de versions différentes. Libérez le handle avec
`program.dispose()` ; la destruction du runtime libère les programmes restants.

Les options de fabrique sont transmises au module Emscripten généré. Utilisez
`locateFile(path, prefix)` pour servir le fichier `.wasm` depuis une URL
personnalisée, ou `wasmBinary` avec un `ArrayBuffer` ou `Uint8Array` déjà chargé.

## Langues, fichiers et entrée

Le runtime commence avec `en-US`. Découvrez les catalogues réellement intégrés via `availableLanguages`, puis appelez `setLanguage`. L'état de langue bas niveau étant partagé dans un module Wasm, utilisez des Web Workers séparés pour isoler des langues concurrentes.

L'option `files` fournit des modules importés temporaires sans écrire dans le système de fichiers réel :

```TypeScript
const result = runtime.runSource(
	`import "helper.vs";

int main()
{
	return answer();
}`,
	{
		fileLabel: "main.vs",
		files: [{ path: "helper.vs", source: `export int answer()
{
	return 42;
}` }],
	},
);
```

Pour une entrée interactive, utilisez le runtime normal :

```TypeScript
const result = await runtime.runSourceInteractive(source, {
	args: [],
	readLine: async ({ index }) => terminal.readLine(index),
});
```

Retournez `null` pour la fin de fichier. Une seule exécution interactive peut attendre dans un module. Le wrapper TypeScript refuse les autres opérations et la destruction pendant cette attente. Au niveau du C ABI brut, le module entier est protégé : les fonctions de création et les getters de texte renvoient `null`, les opérations textuelles avec état renvoient une erreur API, les getters numériques et opérations de programme renvoient zéro et la destruction est ignorée jusqu'à la fin de l'appel interactif. Les opérations mémoire renvoient Busy (9), leurs getters de résultat renvoient zéro et la libération des leases reste disponible. Les exécutions longues doivent être placées dans un Worker afin de ne pas bloquer la page. Consultez le [tutoriel sur l'entrée interactive](../../../../../../../Resources/Docs/Tutorials/fr-FR/toolchain/16-web-interactive-input.md) et le [SDK Interop](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md) pour les couches plus basses.

Un échec d’allocation de l’entrée ou une ligne dépassant la taille Wasm32 renvoie un résultat API en échec. Le module reste utilisable pour les exécutions suivantes ; `null` indique toujours une fin de fichier normale.

## Limites de build et du navigateur

Les deux artefacts publiés prennent en charge les programmes compilés et
n'utilisent pas les pthreads du navigateur ; ils ne
requièrent donc pas d'en-têtes COOP/COEP. La propriété TypeScript
`runtime.supportsThreads` vaut `false`. `void main(string[] args)` et
`int main(string[] args)` sont valides ; un `int main` non nul peut rester
`success: true` et est renvoyé par `exitCode`. `spawn` n'est pas disponible dans
la baseline navigateur et produit `diag.failed_to_spawn_task_thread` ; les
opérations de canal, mutex, sémaphore et valeur synchronisée qui ne créent pas
de tâche restent utilisables dans une exécution. Pour l'isolation, utilisez un
Web Worker par programme.

## Façade C++ et C ABI

Les exécutions de sources réussies via `FRuntime` exposent également les mêmes
statistiques JIT que le résultat JSON, notamment `directNativeCalls`.

La façade C++ `VeroScript::WebAssembly::FRuntime` couvre la sélection de langue, la vérification et l'exécution de sources, les fichiers virtuels, les flux d'entrée, les noms de backend et la conversion du résultat en JSON. La façade C++ actuelle ne fournit pas directement la propriété des programmes compilés ; utilisez le wrapper TypeScript ou le C ABI pour la compilation réutilisable. Les hôtes qui n'utilisent pas le wrapper TypeScript peuvent inclure `VeroScriptWebAssemblyC.h` : le C ABI expose aussi `vero_web_compile_source_with_context`, `vero_web_run_program`, `vero_web_run_program_interactive` et `vero_web_destroy_program`. Le handle appartient au runtime et doit être libéré. Les identifiants de backend sont `0 = VM`, `1 = Mixed`, `2 = JIT`. La chaîne de langue reste valide jusqu'à un changement de langue réussi ou à la destruction du runtime ; le JSON des langues disponibles reste valide jusqu'à la prochaine requête correspondante ou à la destruction. Les autres JSON retournés appartiennent au runtime jusqu'au prochain appel qui renvoie un texte possédé par le runtime ou jusqu'à sa destruction ; les chaînes et tableaux d'entrée ne sont empruntés que pendant l'appel.

## Mémoire gérée

API hôte : `runtime.memory` / `runtime.memory.setLimits` (bigint). La référence [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md) décrit la propriété, les quotas, l'expiration des vues et les emprunts.

<a id="managed-memory-contract"></a>

La façade hôte expose tampons, tranches, copies, lectures/écritures scalaires, instantanés et baux explicites. Tailles, décalages, quotas, identifiants et entiers de 64 bits utilisent `bigint`; les scalaires acceptent les deux ordres des octets. `fromArray` copie la source, y compris un `Buffer` Node.js; `toArray` retourne une copie. Libérez les régions après usage. Un bail fixe le stockage, interdit son redimensionnement et reste libérable après destruction du runtime. `withBytes` exige un callback synchrone : ne libérez pas ce bail, ne conservez pas les vues modifiables et ne faites pas grandir le tas Wasm dans le callback. Les baux en lecture seule exposent des copies; les vues modifiables sont reconstruites entre callbacks après agrandissement du tas. Les opérations mémoire avec état sont refusées pendant une attente interactive.

Régions hôtes et sessions de script ont des quotas séparés : initialement 16 MiB par tampon et 32 MiB retenus par contexte. `runtime.memory.setLimits` ne change que les quotas hôtes; zéro signifie illimité. Ces quotas couvrent le stockage géré des octets, pas tout le tas Wasm ni un bac à sable complet.

## Compatibilité du noyau et diagnostics

<a id="compiler-diagnostics"></a>

Le compilateur embarqué prépare les déclarations entre fichiers et accepte les annotations structurées ; les annotations inconnues restent des métadonnées. Le SDK navigateur ne registre pas de processeurs natifs d’annotations. Importez `delegate.vs` pour utiliser `std::delegate` et `std::multicast_delegate`, dont les membres sont nommés en minuscules. Les diagnostics conservent les niveaux `error`, `warning`, `info` et `hint` ; un résultat réussi peut contenir des diagnostics non bloquants. L’exécution du source conserve les diagnostics de compilation ; les programmes réutilisables les exposent dans le résultat de compilation. Le bytecode utilise l’identifiant fixe `VSBC` et la version produit `0.1.1` ; reconstruisez les paquets et caches issus d’anciens builds de développement.

## Propriété de l’entrée interactive

Les appels interactifs revérifient le module après la lecture des options. Une requête réentrante ou liée à un runtime détruit est refusée avant de prendre le contrôle du fournisseur d’entrée ; une exécution active conserve son callback et ses entrées suivantes.

<a id="input-provider-failures"></a>

Les erreurs du fournisseur de saisie du SDK et les promesses rejetées interrompent l’exécution courante et rejettent avec l’erreur d’origine ; les types de résultat invalides et les lignes contenant NUL rejettent avec `TypeError`. L’échec reprend la pile native pour effectuer le nettoyage, sans être traité comme EOF suivi d’une exécution normale du script. Le runtime et le programme compilé restent réutilisables. `null` signifie toujours EOF normal. Les fournisseurs bruts non marqués conservent leur repli EOF pour une saisie rejetée ou invalide.

## Sortie console et capacités

<a id="console-capabilities"></a>

`std::console::write` ajoute du texte sans saut de ligne à `output` ; `flush` vide la sortie standard. `std::console::error` utilise le canal de lignes de diagnostic de l’hôte : un saut de ligne est ajouté dans `errorOutput` à la dernière ligne non terminée. Le SDK ne fournit pas de terminal : les requêtes de capacités renvoient `false`, les dimensions sont nulles et les opérations valides de couleur, de curseur et d’effacement renvoient `false`, sans séquences d’échappement ni suppression du texte capturé. La saisie asynchrone est indépendante des capacités du terminal.

Les noms de couleur, modes d’effacement, coordonnées du curseur, types ou nombres d’arguments invalides produisent des diagnostics avant le repli lié à l’absence de terminal. Le runtime reste réutilisable après le rejet. Le texte capturé est renvoyé dans `VeroScriptResult` à la fin de l’exécution.

## Fonctionnalités actuelles du langage

<a id="compiler-and-application-services"></a>

Les deux moteurs prennent en charge `pair<T, U>` / `std::pair` en minuscules, `std::attribute<T>`, `std::bind_method` et les modules embarqués `attribute.vs`, `callable.vs`, `i18n.vs` et `compiler.vs`. Le contexte applicatif `std::i18n::context` propose les textes littéraux, le formatage nommé avec paramètres typés et des instantanés indépendants ; sa langue ne modifie pas celle des diagnostics du SDK. Le SDK ne fournit aucun fournisseur de ressources applicatives ou de formatage.

`import compiler "builder.vs";` peut charger une extension de compilation depuis `files` pendant `checkSource`, `compileSource` ou `runSource`. La vérification peut donc exécuter du code d’extension sans appeler le `main` applicatif. Core applique ses limites et permissions par défaut aux extensions. Utilisez un Web Worker si le frontend doit pouvoir interrompre la compilation. Un échec d’extension produit des diagnostics et laisse le runtime réutilisable.

## Espace de sauvegarde interactif

La saisie interactive peut suspendre l’exécution dans des appels imbriqués de méthodes liées. La réentrée native profonde reste limitée par la pile de l’hôte ; le moteur rapide exige les entrées avant l’exécution.

## Correspondance des noms d’annotation

<a id="annotation-name-matching"></a>

Les noms d’annotation sont sensibles à la casse par défaut. Une extension de compilation peut définir `annotation_definition.case_sensitive = false` avant l’enregistrement pour activer une comparaison ASCII indépendante des paramètres régionaux pour cette définition. Les abonnements des processeurs et la réflexion via `has_annotation`, `find_annotations` et `annotation_info.matches_name` respectent cette règle. `name()` et `raw_text()` conservent la graphie source ; `definition().case_sensitive()` indique la règle. Pour les annotations de forme `@Name`, `find_annotations` et `matches_name` exigent le préfixe `@` ; `has_annotation` autorise son omission. Les noms d’arguments, les clés de métadonnées et les annotations non enregistrées restent sensibles à la casse.

## Exécution Core et limites

<a id="numeric-and-library-limits"></a>

Core fournit `std::bigint` et `std::rational` via `bigint.vs`, `rational.vs` et `core.vs`. Ces types conservent les valeurs entières et rationnelles exactes dans la VM ; utilisez `to_string()` pour exporter un texte exact. Ils sont distincts du `bigint` JavaScript utilisé pour les arguments Wasm ABI de largeur fixe. Les types génériques imbriqués, les types imbriqués hérités et les types locaux suivent les règles de portée et d’accès de Core.

`runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts)` accepte des limites `bigint` non signées sur 64 bits pour les octets de texte, les octets lus en bloc et le nombre de fragments, par opération. Zéro n’ajoute aucune limite hôte ; les arguments omis reviennent à zéro. Les changements concernent les exécutions source et interactives suivantes ainsi que la prochaine exécution des programmes déjà compilés. Ils sont refusés pendant une attente d’entrée interactive. Ces limites sont indépendantes des quotas de mémoire gérée et des limites numériques Core ; elles ne bornent pas tout le tas Wasm. L’API Web utilise les limites numériques finies par défaut de Core.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
