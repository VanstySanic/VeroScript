# 02 : Vérifier, exécuter et lire les arguments

Dès qu'un script accepte une entrée différente, le codage en dur des données dans la source devient gênant. Ce didacticiel lit les arguments de ligne de commande et utilise `--check` pour valider le programme avant de l'exécuter.

## Étape 1 : accepter les arguments

Créez `argv_echo.vs` :

```VeroScript
int main(string[] args)
{
	print("argc=" + to_string(len(args)));
	for (int i = 0; i < len(args); i = i + 1)
	{
		print(args[i]);
	}
	return 0;
}
```

`main(string[] args)` reçoit les arguments après le chemin du script. Le nom du fichier de script n'est pas inclus dans `args`, donc `len(args)` correspond au nombre de valeurs transmises au script.

## Étape 2 : passer les arguments

```powershell
veroscript --check argv_echo.vs
veroscript argv_echo.vs red blue
```

Le résultat est similaire à :

```text
argc=2
red
blue
```

Les arguments sont des chaînes. Les conversions numériques telles que `to_int` acceptent des valeurs numériques ou booléennes ; elles n’analysent pas les chaînes. Si un argument doit contenir un nombre, définissez et validez son format textuel avant de l’utiliser dans un calcul. Voir les [fonctions de conversion](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/builtins.md).

## Étape 3 : créer un outil de salutation

Enregistrez le programme suivant dans `greet.vs`, dans le même répertoire que `argv_echo.vs` :

```VeroScript
int main(string[] args)
{
	string name = "world";
	if (len(args) > 0)
	{
		name = args[0];
	}

	print("hello, " + name);
	return 0;
}
```

Vérifiez le programme, puis exécutez-le sans argument et avec un argument :

```powershell
veroscript --check greet.vs
veroscript greet.vs
veroscript greet.vs VeroScript
```

La première exécution utilise la valeur par défaut ; la seconde utilise le premier argument à la place.

## Étape 4 : choisissez un backend d'exécution

L'exécution normale utilise le backend mixte par défaut. Vous pouvez sélectionner explicitement un backend lors de la comparaison des modes d'exécution :

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm argv_echo.vs one two
veroscript --jit-backend="/path/to/Jit" --backend=jit argv_echo.vs one two
```

Utilisez la commande VM avec l’interpréteur fourni par Runtime. Avant la commande JIT, remplacez `/path/to/Jit` par un backend compatible ; un échec de chargement arrête la commande avant le script. En cas de succès, les deux commandes transmettent `one` et `two` à `argv_echo.vs` comme précédemment. Voir la [référence des moteurs CLI](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md#cli-backend-policy) pour les valeurs du projet, la sélection automatique et la désactivation de JIT.

Le débogage utilise le chemin de débogage de la VM. Pour l'instant, n'oubliez pas le workflow : utilisez `--check` pendant le développement et sélectionnez un backend uniquement lorsque vous avez une raison de comparer ou de limiter l'exécution.

## Exercices

1. Si aucun argument n'est fourni, imprimez `usage: greet.vs <name>` et renvoyez un code différent de zéro.
2. Vérifiez qu’un deuxième argument existe, puis affichez sa longueur avec `len`.
3. Comparez les diagnostics de `--lang en-US` et `--lang fr-FR`.

Suivant : [Valeurs, conditions et boucles](../language/03-values-and-control-flow.md).

L'exemple complet est [`examples/argv_echo.vs`](../../../../../VeroScript/Resources/Scripts/examples/argv_echo.vs). Consultez la [référence CLI](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md) pour les options.

Pour comparer les performances, utilisez une exécution normale avec les mêmes entrées et le même backend, après échauffement. Le débogage, l’initialisation conservée et le nettoyage borné utilisent la VM ; les fonctions ordinaires admissibles d’une session persistante peuvent utiliser le JIT choisi. Le cache de bytecode réduit la compilation au démarrage. Le [guide d’intégration C++](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/embedding.md#threads-and-hot-reload) décrit le cache JIT de session et ses limites.

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/getting-started/02-cli-and-arguments.md) · [fr-FR](02-cli-and-arguments.md) · [ja-JP](../../ja-JP/getting-started/02-cli-and-arguments.md) · [ko-KR](../../ko-KR/getting-started/02-cli-and-arguments.md) · [zh-CN](../../zh-CN/getting-started/02-cli-and-arguments.md) · [zh-TW](../../zh-TW/getting-started/02-cli-and-arguments.md)
