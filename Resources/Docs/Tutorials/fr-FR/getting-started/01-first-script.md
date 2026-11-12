# 01 : Votre premier script

Ce didacticiel a un seul objectif : créer un script, l'exécuter et afficher une ligne de sortie.

## Ce que vous apprendrez

- Ce que contient un programme VeroScript minimal.
- Comment exécuter un script.
- Comment vérifier un script sans l'exécuter.
- Que font `main` et `print`.

## Étape 1 : créer un fichier

Créez `hello.vs` dans votre répertoire de travail :

```VeroScript
int main()
{
	print("hello, VeroScript");
	return 0;
}
```

Il y a deux éléments importants : `main` est le point d'entrée du script et `int` renvoie l'état de sortie du processus. `print` écrit une chaîne sur la sortie standard. `void main` reste pris en charge, mais les programmes complets utilisent généralement `int main` et renvoient explicitement `0` en cas de succès.

## Étape 2 : vérifiez, puis exécutez

Exécutez ces commandes à partir du répertoire contenant `hello.vs` :

```powershell
veroscript --check hello.vs
veroscript hello.vs
```

Le résultat devrait ressembler à :

```text
hello, VeroScript
```

`--check` compile le fichier et signale les problèmes sans exécuter `main`. Utilisez-le d'abord pendant le développement, puis exécutez le script une fois la vérification réussie.

## Étape 3 : rendre le résultat utile

Changez le script en :

```VeroScript
int main()
{
	string name = "Ada";
	print("hello, " + name);
	return 0;
}
```

Vous devriez maintenant voir :

```text
hello, Ada
```

`string name` déclare une variable chaîne. L'opérateur `+` concatène les deux chaînes.

## Si quelque chose ne va pas

- Si `veroscript` est introuvable, confirmez que la CLI est installée et se trouve sur `PATH`.
- Si `hello.vs` est introuvable, vérifiez le répertoire courant du terminal ou transmettez un chemin absolu.
- Exécutez `veroscript --lang en-US --check hello.vs` pour demander des diagnostics en anglais.

## Exercices

1. Remplacez le nom par le vôtre.
2. Ajoutez `string language = "VeroScript";` et imprimez une phrase en utilisant les deux variables.
3. Supprimez un point-virgule, exécutez `--check`, lisez le diagnostic et corrigez le fichier.

Suivant : [Vérification, exécution et lecture des arguments](02-cli-and-arguments.md).

L'exemple minimal complet est [`examples/hello.vs`](../../../../../VeroScript/Resources/Scripts/examples/hello.vs). Pour la syntaxe, voir [Syntaxe de base](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/basic-syntax.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/getting-started/01-first-script.md) · [fr-FR](01-first-script.md) · [ja-JP](../../ja-JP/getting-started/01-first-script.md) · [ko-KR](../../ko-KR/getting-started/01-first-script.md) · [zh-CN](../../zh-CN/getting-started/01-first-script.md) · [zh-TW](../../zh-TW/getting-started/01-first-script.md)
