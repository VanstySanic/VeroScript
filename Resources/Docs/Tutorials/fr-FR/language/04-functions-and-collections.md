# 04 : Fonctions et collections

Lorsque `main` commence à croître, extraire du code avec une responsabilité claire est généralement plus utile que d'ajouter davantage de commentaires. Ce didacticiel écrit une fonction somme et stocke un nombre variable de valeurs dans `array<int>`.

## Étape 1 : écrire une fonction somme

```VeroScript
int sum(array<int> values)
{
	int result = 0;
	for (int value : values)
	{
		result = result + value;
	}
	return result;
}

int main()
{
	array<int> values = array<int>();
	values.push(10);
	values.push(20);
	values.push(30);

	print("sum={}", sum(values));
	return 0;
}
```

Une déclaration de fonction donne le type de retour, le nom et les paramètres. `sum` reçoit un `array<int>` et renvoie un `int`.

## Étape 2 : laissez la collection s'agrandir

`array<int>()` crée un tableau dynamique vide et `push` ajoute un élément à la fin. Vous pouvez également utiliser l'initialisation par accolade :

```VeroScript
int main()
{
	array<int> values = array<int>{10, 20, 30};

	values.push(40);
	for (int& value : values)
	{
		value = value + 1;
	}

	print("size={}", len(values));
	print("first={}", values[0]);
	return 0;
}
```

La boucle de plage ordinaire `for (int value : values)` reçoit une copie d'élément. L'ajout de `&` crée un alias à l'élément, de sorte que les modifications sont réécrites dans le conteneur.

## Étape 3 : utiliser un paramètre par défaut

```VeroScript
void print_values(array<int> values, string label = "values")
{
	print(label);
	for (int value : values)
	{
		print(value);
	}
}

int main()
{
	array<int> values = array<int>{1, 2, 3};
	print_values(values);
	print_values(values, "numbers");
	return 0;
}
```

Les paramètres par défaut permettent aux appelants d'omettre la configuration commune. La valeur par défaut doit être compatible avec le type de paramètre.

## Choisir un conteneur

Commencez par `array<T>` pour des données ordonnées et extensibles. Considérez `map<K, V>` pour la recherche de clé et `set<T>` pour l’unicité. N'ajoutez pas de conteneur plus complexe avant que le modèle d'accès ne l'exige.

## Exercices

1. Écrivez une fonction `average` qui renvoie `0.0` pour un tableau vide.
2. Écrivez une fonction qui modifie chaque valeur négative en `0` à l'aide d'une boucle de plage d'alias.
3. Stockez les partitions dans `map<string, int>` et imprimez la partition pour un nom.

Voir [`examples/dynamic_array.vs`](../../../../../VeroScript/Resources/Scripts/examples/dynamic_array.vs) et [`examples/ref_foreach.vs`](../../../../../VeroScript/Resources/Scripts/examples/ref_foreach.vs). La page [Types de données](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/data-types.md) récapitule les conteneurs disponibles.

Suivant : [Structs, classes et interfaces](05-structs-classes-and-interfaces.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/language/04-functions-and-collections.md) · [fr-FR](04-functions-and-collections.md) · [ja-JP](../../ja-JP/language/04-functions-and-collections.md) · [ko-KR](../../ko-KR/language/04-functions-and-collections.md) · [zh-CN](../../zh-CN/language/04-functions-and-collections.md) · [zh-TW](../../zh-TW/language/04-functions-and-collections.md)
