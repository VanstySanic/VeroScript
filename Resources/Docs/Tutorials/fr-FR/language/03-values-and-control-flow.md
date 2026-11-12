# 03 : Valeurs, conditions et boucles

Ce didacticiel combine des variables, des types statiques, des branchements et des répétitions dans un petit calculateur de score. L’objectif est d’apprendre à diviser un problème en état, décisions et actions répétées.

## Étape 1 : préparer l'état

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	print("total={}", total);
	return 0;
}
```

`total` et `count` sont tous deux `int`. `i` existe uniquement dans la boucle `for` ; une portée locale empêche l'état temporaire de s'infiltrer dans du code sans rapport.

## Étape 2 : ajouter une décision

```VeroScript
int main()
{
	int total = 0;
	int count = 5;

	for (int i = 1; i <= count; i = i + 1)
	{
		total = total + i;
	}

	if (total >= 15)
	{
		print("pass: {}", total);
	}
	else
	{
		print("retry: {}", total);
	}
	return 0;
}
```

La condition `if` doit produire un résultat booléen. Les comparaisons produisent `bool` ; utilisez des comparaisons explicites au lieu de compter sur un entier traité comme une condition.

## Étape 3 : déplacer les données dans un tableau

```VeroScript
int main()
{
	int scores[4] = {72, 88, 91, 65};
	int total = 0;

	for (int i = 0; i < len(scores); i = i + 1)
	{
		total = total + scores[i];
	}

	double average = to_double(total) / len(scores);
	print("average={}", average);
	return 0;
}
```

`scores` est un tableau natif de taille fixe. Ses index commencent à `0` et `len(scores)` renvoie sa longueur. Utilisez `array<int>` lorsque le nombre d'éléments doit changer au moment de l'exécution.

## Étape 4 : utiliser des littéraux fractionnaires

La partie entière peut être omise dans un littéral à virgule flottante lorsque `.` est immédiatement suivi d'un chiffre décimal. Le suffixe détermine toujours le type de stockage : `.5f` est un `float`, tandis que `.5` et `.5e2` sont des valeurs `double`.

```VeroScript
int main()
{
	float ratio = .5f;
	double half = .5;
	double fifty = .5e2;

	print("ratio={}, half={}, fifty={}", ratio, half, fifty);
	return 0;
}
```

`0.5f` et `.5f` ont la même valeur et le même type. Conservez le chiffre décimal après le point ; un `.` isolé reste utilisé pour l'accès à un membre.

## Étape 5 : gérer une exception de script

Utilisez `throw` pour signaler un échec que le script peut décrire et traiter.
La valeur d'exception peut franchir un appel de fonction et être traitée par un
`catch` correspondant. `finally` sert au nettoyage qui doit être effectué que
l'opération réussisse ou qu'elle se termine par une exception :

```VeroScript
void validate_score(int score)
{
	if (score < 0)
	{
		throw "score must not be negative";
	}
}

int main()
{
	try
	{
		validate_score(-1);
	}
	catch (const string& message)
	{
		print("handled: {}", message);
	}
	finally
	{
		print("cleanup");
	}
	return 0;
}
```

Ce programme affiche le message traité, puis `cleanup`. Utilisez un gestionnaire
typé lorsque le type de l'exception est important ; utilisez `catch (...)`
uniquement comme dernier recours. Un `throw;` sans expression dans un
gestionnaire relance la valeur d'origine.

Pour un exemple complet d'exception, voir [`examples/exceptions.vs`](../../../../../VeroScript/Resources/Scripts/examples/exceptions.vs).

## Étape 6 : vérifier un invariant avec `assert`

`assert(condition[, message])` effectue une vérification à l'exécution. La
condition doit être `bool` et le message facultatif doit être `string` :

```VeroScript
int main()
{
	int total = 15;
	assert(total >= 0, "total must be non-negative");
	print("ready");
	return 0;
}
```

Une condition vraie continue sans sortie. Une condition fausse signale une
erreur d'exécution et arrête l'exécution en cours ; ce n'est pas une exception
de script et `catch (...)` ne peut donc pas la traiter.

## Les idées de flux de contrôle que vous avez utilisées

- `if` choisit un chemin en fonction d'une condition.
- `for` répète le travail avec une initialisation, une condition et un incrément explicites.
- `try`/`catch` traite les valeurs de script levées par `throw`, et `finally` effectue le nettoyage.
- `assert` vérifie un invariant booléen et signale une erreur d'exécution en cas d'échec.
- `return` quitte une fonction avec un résultat ; le prochain tutoriel l'utilise beaucoup.

## Exercices

1. Calculez le score le plus élevé et le plus bas.
2. Comptez uniquement les scores supérieurs ou égaux à `60`.
3. Remplacez le tableau fixe par `array<int>` et utilisez une itération basée sur la plage.
4. Remplacez un littéral décimal tel que `0.5` par `.5`, puis utilisez `.5f` lorsqu'une valeur `float` est nécessaire.
5. Ajoutez un `catch` typé et un bloc `finally` à une fonction qui refuse un score négatif.
6. Ajoutez un `assert` accompagné d'un message pour un invariant de votre calcul de score.

Pour un exemple de tableau complet, voir [`examples/arrays.vs`](../../../../../VeroScript/Resources/Scripts/examples/arrays.vs). Les pages de référence sont [Variables](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/variables.md), [Flux de contrôle](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/control-flow.md) et [Arrays](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/arrays.md).

Suivant : [Fonctions et collections](04-functions-and-collections.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/language/03-values-and-control-flow.md) · [fr-FR](03-values-and-control-flow.md) · [ja-JP](../../ja-JP/language/03-values-and-control-flow.md) · [ko-KR](../../ko-KR/language/03-values-and-control-flow.md) · [zh-CN](../../zh-CN/language/03-values-and-control-flow.md) · [zh-TW](../../zh-TW/language/03-values-and-control-flow.md)
