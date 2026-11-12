# Flux Tooling : connecter un éditeur et le débogueur

Ce tutoriel relie un projet au serveur de langage et à l'adaptateur de
débogage VeroScript. Vous vérifierez d'abord le CLI, puis configurerez LSP,
testerez l'assistance de l'éditeur et lancerez finalement une session DAP.

## Étape 1 : vérifier le CLI

Utilisez le CLI et la VM d’une même version de Runtime, le serveur de langage correspondant et un client compatible LSP et DAP. Enregistrez les deux fichiers ci-dessous dans le même dossier de travail, accessible en écriture. Exécutez les commandes depuis ce dossier et ouvrez-le dans l’éditeur comme espace de travail.

### hello.vs

```VeroScript
import "lesson-utils.vs";

namespace Lesson
{
	int twice(int value)
	{
		return value * 2;
	}
}

int main(string[] args)
{
	std::chrono::now();
	int value = Lesson::twice(lessonSeed());
	print("value={}, argc={}", value, len(args));
	return 0;
}
```

### lesson-utils.vs

```VeroScript
export int lessonSeed()
{
	return 21;
}
```

```powershell
veroscript --version
veroscript --check hello.vs
veroscript --vm-backend=vs-bytecode --backend=vm hello.vs red blue
```

La vérification réussit sans exécuter le script. L’exécution affiche `value=42, argc=2` et se termine avec le code zéro.

Corrigez toute erreur avant de configurer l'éditeur. Une différence entre CLI
et LSP indique généralement des chemins, macros ou catalogues différents.

## Étape 2 : lancer LSP

Configurez l'éditeur pour lancer la version correspondante de `veroscript-lsp`
sur l'entrée/sortie standard. Il envoie `initialize`, attend la réponse, puis
envoie `initialized`. Utilisez la racine du projet comme workspace root.

Ajoutez les champs ci-dessous aux `params` de la requête `initialize`, aux côtés de l’espace de travail et des capacités du client. Démarrez le serveur dans le dossier de la leçon pour que `.` désigne ce dossier. Si l’éditeur le lance ailleurs, utilisez le chemin absolu du dossier, par exemple `C:/work/project` sous Windows. Dans un autre projet, indiquez `stdlibPath` si le serveur doit utiliser un dossier de bibliothèque standard distinct.

```json
{
	"locale": "fr-FR",
	"initializationOptions": {
		"veroscript": {
			"importPaths": [
				"."
			]
		}
	}
}
```

Si le client prend en charge l'enregistrement dynamique des fichiers surveillés,
le serveur demande après `initialized` les motifs `**/*.vs` et `**/*.vslib`.
Sinon, configurez ces motifs dans le client ou envoyez explicitement des
notifications `workspace/didChangeWatchedFiles`.

Ouvrez un fichier `.vs`. Les tampons ouverts sont analysés en mémoire et les
imports peuvent donc être résolus avant la sauvegarde. Les changements
incrémentaux dont la version est ancienne sont ignorés.

## Étape 3 : essayer l'assistance de l'éditeur

Ouvrez le fichier `hello.vs` enregistré à l’étape 1.

1. Introduisez temporairement une erreur de syntaxe ou de type, vérifiez les
   diagnostics, puis annulez l'erreur.
2. Remplacez `std::chrono::now();` par `std`, placez le curseur après ce terme,
   lancez la complétion et acceptez le namespace : `std::` est inséré. Saisissez
   `ch` et acceptez `chrono` ; `chrono::` est inséré et vous pouvez en examiner
   les membres. Terminez la ligne par `std::chrono::now();` pour que l'exemple
   reste valide. La complétion n'ajoute `::` que si aucun séparateur ne suit
   déjà la plage remplacée.
3. Remplacez temporairement `Lesson::twice(lessonSeed())` par `Lesson::tw` et demandez la complétion. Si le client annonce `textDocument.completion.completionItem.snippetSupport`, vérifiez l’emplacement du paramètre de `twice`. Rétablissez l’appel d’origine avant de poursuivre.
4. Survolez un namespace, une fonction ou un type.
5. Accédez à la définition de `lessonSeed` depuis son appel dans `hello.vs` et vérifiez que `lesson-utils.vs` s’ouvre. Repérez `Lesson` et `main` dans les symboles du document, puis recherchez `Lesson::twice` avec `workspace/symbol` et résolvez un résultat avec `workspaceSymbol/resolve`.
6. Résolvez un élément de complétion pour consulter sa documentation Markdown si elle est disponible. Ouvrez le lien sur la ligne `import "lesson-utils.vs";`.
7. Demandez les tokens sémantiques, puis une mise à jour delta avec son identifiant
   de résultat précédent.
8. Préparez un renommage sur `Lesson`, renommez-le en `Practice` et vérifiez que la déclaration et l’appel qualifié sont tous deux modifiés.
9. Formatez le document, puis une plage sélectionnée et vérifiez que l’édition couvre ses lignes entières. Essayez `source.formatDocument`. Pour essayer `source.organizeImports`, dupliquez une fois la ligne d’import et vérifiez que l’action supprime le doublon. Enregistrez les deux fichiers avant de relancer `veroscript --check hello.vs`.

Les complétions génériques et appelables peuvent utiliser des snippets LSP
lorsque le contexte s'y prête. Le serveur ne les envoie que si le client annonce
`textDocument.completion.completionItem.snippetSupport` ; les autres insertions
se font en texte brut. Les namespaces de bibliothèque standard
peuvent être navigués mais ne sont pas renommables. Les actions de code incluent le
formatage complet et la suppression prudente des imports exactement dupliqués ;
aucune ne réécrit du code sans rapport.

## Étape 4 : ajouter les catalogues hôte

Suivez cette étape si votre projet utilise des liaisons hôte. Avec les deux fichiers de la leçon, vous pouvez passer directement à l’étape 5.

Ajoutez dans `initializationOptions.veroscript.bindingCatalogPaths` les catalogues utilisés par l'hôte, dans le
même ordre que le projet. Ils alimentent l'analyse, la complétion, le survol,
les symboles et la documentation des fonctions, types, membres, annotations et
templates génériques.

Modifiez un catalogue pendant que le serveur fonctionne : il sera rechargé à la
prochaine requête et les documents ouverts seront réanalysés. Un catalogue ne
fournit toutefois pas l'implémentation runtime.

## Étape 5 : changer la langue

Définissez le tag BCP 47 dans `initialize.locale` avant l'ouverture des fichiers :

```json
{
	"locale": "zh-CN"
}
```

Redémarrez le serveur de langage avec le nouveau `locale`. Réintroduisez la même erreur temporaire qu’à l’étape 3 et comparez le code et le message traduit. Le code reste identique et les traductions manquantes utilisent `en-US`. Corrigez l’erreur et enregistrez le fichier avant de déboguer.

## Étape 6 : lancer DAP

Choisissez un mode de connexion dans le client DAP. Utilisez l’entrée et la sortie standard lorsque le client lance l’adaptateur avec cette commande depuis le dossier de la leçon.

```powershell
veroscript --debug hello.vs
```

Si le client démarre séparément, lancez l’adaptateur TCP depuis le dossier de la leçon, puis connectez le client à `127.0.0.1:4711`.

```powershell
veroscript --debug --dap-port=4711 hello.vs
```

Pour définir les arguments par défaut en mode entrée et sortie standard, utilisez cette commande à la place de la première.

```powershell
veroscript --debug hello.vs red blue
```

Dans le client, lancez `hello.vs` avec le dossier de la leçon comme `cwd`. Placez un point d’arrêt sur la ligne `print` et passez `["red", "blue"]` dans `args`, ou omettez `args` si le CLI les a déjà définis. Lorsque l’exécution s’arrête au point d’arrêt, reprenez-la et vérifiez que `value=42, argc=2` apparaît dans la sortie de débogage. L’ordre des requêtes et les options figurent dans la [référence de débogage](../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/debugging.md).

Dans une requête DAP `launch` ou `attach`, utilisez `program` ou `source`, puis
`args` pour `main(string[] args)`. Utilisez `input` pour l'entrée standard du
script, `env` pour les variables d'environnement, `cwd` pour le répertoire de
travail et `stdlibSearchPaths` pour les racines de la bibliothèque standard.
L'entrée du script est indépendante du flux du protocole DAP. Les chemins
relatifs utilisent `cwd`, ou `workspace` lorsqu'aucun répertoire de travail
n'est fourni. Le débogage, les plages d’initialisation conservées et le nettoyage borné utilisent la VM.
Les fonctions ordinaires d’une Session persistante peuvent utiliser Mixed/JIT ; mesurez les performances en exécution normale.

Les arguments DAP de `initialize` acceptent un `locale` BCP 47 pour les messages
de diagnostic et d'erreur. Le client doit se baser sur les champs de succès et
d'erreur DAP, et non sur le texte traduit.

Les points d'arrêt de ligne source utilisent `setBreakpoints`. Les points
d'arrêt d'exception et de fonction ne sont pas pris en charge ; leurs requêtes
échouent explicitement et le client ne doit pas afficher ces catégories comme
actives.

Après `configurationDone`, un hôte filaire acquitte la requête et exécute la VM
sur un thread dédié. Utilisez `pause` pour demander un arrêt coopératif d'une VM
en cours ; l'événement `stopped` qui suit porte la raison `pause`. Utilisez
`continue`, `next`, `stepIn` ou `stepOut` pour reprendre, et `terminate` pour
demander l'annulation. Le protocole reste disponible pendant l'exécution.
`disconnect` termine par défaut le processus débogué lancé, tandis que le processus attaché continue ; dans un hôte TCP multi-session, `terminateDebuggee=false`
conserve l'écoute disponible.

Pendant l'exécution, `std::console::write` et `std::console::error` produisent
une sortie sans nouvelle ligne. Appelez `std::console::flush()` pour rendre
immédiatement visible un fragment de progression. L'adaptateur transmet la
sortie standard et la sortie d'erreur dans des événements DAP distincts avant
la fin du processus.

Terminez LSP par `shutdown` puis `exit`.

## Exercices

1. Faites résoudre le même import par CLI et LSP.
2. Envoyez une ancienne version après une version plus récente et vérifiez que
   le tampon récent reste actif.
3. Ajoutez un catalogue et trouvez une fonction hôte dans la complétion et le survol.
4. Changez la langue et confirmez que les codes de diagnostic restent identiques.
5. Modifiez un module importé sur disque, envoyez `workspace/didChangeWatchedFiles`
   et vérifiez que les diagnostics des documents ouverts sont actualisés.

Consultez la [référence du serveur de langage](../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/lsp.md)
et la [référence Tooling](../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/index.md) pour le protocole complet.

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/toolchain/17-tooling-workflow.md) · [fr-FR](17-tooling-workflow.md) · [ja-JP](../../ja-JP/toolchain/17-tooling-workflow.md) · [ko-KR](../../ko-KR/toolchain/17-tooling-workflow.md) · [zh-CN](../../zh-CN/toolchain/17-tooling-workflow.md) · [zh-TW](../../zh-TW/toolchain/17-tooling-workflow.md)
