# Fonctions

Une fonction est une déclaration appelable nommée avec une liste de paramètres et un type de retour. Les fonctions peuvent être définies au niveau du module ou en tant que membres d'une structure, d'une classe ou d'une interface.

## Déclarations de fonctions

Une déclaration de fonction contient un type de retour, un nom, une liste de paramètres et un corps :

```VeroScript
int add(int left, int right)
{
	return left + right;
}
```

`void` indique que la fonction ne renvoie pas de valeur :

```VeroScript
void print_message(string message)
{
	print(message);
}
```

## Le point d'entrée `main`

Un point d'entrée d'application peut renvoyer `int` ou `void` et peut prendre au plus un paramètre `string[]`. Une valeur de retour `int` devient le code de sortie du processus ; `void` est traité comme `0`. Le paramètre d'entrée ne peut pas être un paramètre de référence, variadique ou par défaut :

```VeroScript
int main(string[] args)
{
	if (len(args) == 0)
	{
		return 2;
	}
	print(args[0]);
	return 0;
}
```

Seul le module d'entrée peut fournir `main` ; un module de bibliothèque ne peut pas définir un point d'entrée d'application.

Lorsque `int main` renvoie normalement ou atteint son accolade fermante sans retour explicite, le code résultat est la valeur renvoyée ou `0`. `exit(n)` est prioritaire sur la valeur de retour `main` et `exit(0)` enregistre toujours que le script a explicitement demandé une sortie. Pour les API du moteur, un `ExitCode` différent de zéro ne signifie pas en soi que le moteur est en panne ; un hôte doit inspecter `bSuccess`, `ExitCode` et `bExitRequested` ensemble.

Les paramètres et les variables locales dans le corps de la fonction suivent les règles normales de fonction et de portée de bloc.

## Paramètres et valeurs de retour

Les arguments sont mis en correspondance avec les paramètres dans l'ordre de déclaration. Leurs types doivent satisfaire aux règles de conversion du langage :

```VeroScript
double average(double total, int count)
{
	return total / count;
}
```

Le type d'une expression `return` doit être compatible avec le type de retour de la fonction. Une fonction `void` peut utiliser `return` sans expression pour mettre fin à l'exécution plus tôt.

## Arguments par défaut

Un paramètre peut fournir une valeur par défaut. Si l'appelant omet cet argument, l'expression par défaut déclarée est utilisée :

```VeroScript
int clamp(int value, int minimum = 0, int maximum = 100)
{
	if (value < minimum)
	{
		return minimum;
	}
	if (value > maximum)
	{
		return maximum;
	}
	return value;
}

int main()
{
	print(clamp(120));
	print(clamp(-1, -10, 10));
}
```

Les paramètres par défaut doivent former une séquence finale dans la liste des paramètres. Ne placez pas un paramètre obligatoire après un paramètre facultatif.

## Surcharge de fonctions

Plusieurs fonctions portant le même nom peuvent être déclarées dans une seule portée lorsque leurs listes de paramètres les distinguent :

```VeroScript
int abs_value(int value)
{
	return value < 0 ? -value : value;
}

double abs_value(double value)
{
	return value < 0.0 ? -value : value;
}
```

Lorsqu'une fonction est appelée, le compilateur sélectionne une surcharge en utilisant le nom, le nombre d'arguments, les types d'arguments et les conversions disponibles. Si plusieurs candidats correspondent également, la compilation échoue avec une ambiguïté de surcharge.

## Paramètres de référence

Les paramètres de référence permettent à une fonction de modifier un objet appartenant à son appelant. Les exemples publics préfèrent `T&` ; `ref T` est l'orthographe équivalente de style mot-clé :

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
}
```

L'argument utilise le marqueur `&` ou `ref` correspondant sur le site d'appel. VeroScript n'a pas de type de pointeur de script à usage général et les références ne prennent pas en charge l'arithmétique des pointeurs.

## Paramètres variadiques

Une fonction peut utiliser `T... name` pour déclarer une séquence d'arguments variadiques avec un type d'élément :

```VeroScript
void print_all(string... values)
{
	for (string value : values)
	{
		print(value);
	}
}
```

À l'intérieur de la fonction, le paramètre variadique se comporte comme un tableau de son type d'élément. Un paramètre variadique doit être le dernier.

## Valeurs de fonction et lambdas

Les fonctions peuvent être transmises sous forme de valeurs et les lambdas peuvent créer des fonctions anonymes. Un type de fonction décrit sa liste de paramètres et son type de retour :

```VeroScript
int apply(function(int) -> int operation, int value)
{
	return operation(value);
}

int main()
{
	function(int) -> int increment = function(int value) -> int => value + 1;
	print(apply(increment, 41));
}
```

`fn` est une orthographe abrégée pour `function`. Les règles de capture pour les valeurs de fonction et les lambdas, ainsi que les fonctions génériques et les fonctions membres, sont couvertes dans des références dédiées.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/functions.md) · [fr-FR](functions.md) · [ja-JP](../../ja-JP/language-basics/functions.md) · [ko-KR](../../ko-KR/language-basics/functions.md) · [zh-CN](../../zh-CN/language-basics/functions.md) · [zh-TW](../../zh-TW/language-basics/functions.md)
