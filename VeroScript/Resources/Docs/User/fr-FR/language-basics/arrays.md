# Tableaux

VeroScript a deux formes de tableau : le tableau dynamique `array<T>` et le tableau natif de taille fixe `T[N]`. Les deux prennent en charge l’indexation et l’itération de plage, mais il s’agit de types différents avec une sémantique d’exécution différente.

## Tableaux dynamiques

`array<T>` est un conteneur dynamique paramétré par un élément de type `T` :

```VeroScript
array<int> values = array<int>();
values.push(10);
values.push(20);

print(values[0]);
print(len(values));
```

L'initialisation des accolades est également prise en charge :

```VeroScript
array<int> values = array<int>{1, 2, 3};
```

Les opérations courantes comprennent :

| Opération | Descriptif |
| --- | --- |
| `values[index]` | Lire ou modifier un élément à un index |
| `values.push(value)` | Ajoutez un élément et renvoyez le tableau |
| `values.remove(value)` | Supprime le premier élément égal et indique s'il a été supprimé |
| `values.remove_at(index)` | Supprimer l'élément à un index |
| `values.clear()` | Effacez le tableau et renvoyez le tableau |
| `values.reserve(count)` | Réserver de la capacité et renvoyer la baie |
| `values.capacity()` | Renvoie la capacité actuelle |
| `len(values)` | Renvoie le nombre d'éléments |

Chaque élément inséré dans un `array<T>` doit correspondre à `T`. Les structures et les tableaux natifs sont copiés selon la sémantique des valeurs lors de leur insertion ; les classes et autres types de handles conservent leur sémantique de handle.

Les tableaux dynamiques sont stockés via un handle de conteneur partagé. L'attribution d'une variable `array<T>` à une autre fait que les deux variables font référence au même tableau dynamique. Construisez un tableau indépendant et copiez ses éléments lorsqu'une copie indépendante est requise.

## Tableaux natifs de taille fixe

Un tableau natif met sa longueur après le type d'élément :

```VeroScript
int numbers[3] = {10, 20, 30};
```

La longueur fait partie du type. `int[3]`, `int[4]` et `array<int>` sont des types différents. Les tableaux natifs peuvent être des champs de structure et peuvent être multidimensionnels :

```VeroScript
struct GridRow
{
	int cells[3] = {1, 2, 3};
};

int matrix[2][3] = {
	{1, 2, 3},
	{4, 5, 6}
};
```

Un tableau natif multidimensionnel se compose de tableaux natifs imbriqués. `len(matrix)` renvoie la longueur de la première dimension et `len(matrix[0])` renvoie la longueur de la deuxième dimension.

Un type de tableau natif peut avoir au maximum 64 dimensions. Chaque dimension peut contenir au maximum 65 536 éléments, et le produit aplati de toutes les dimensions ne peut pas dépasser 1 048 576 éléments. Toute source dépassant l'une de ces limites est rejetée à la compilation.

## Indexation

Les indices de tableau commencent à `0` :

```VeroScript
array<string> names = array<string>{"Ada", "Linus"};
string first = names[0];
names[1] = "Grace";
```

Le résultat d'une expression d'indexation a le type d'élément. L'index doit être un entier compris dans la plage valide du tableau actuel ; un accès hors de portée produit une erreur d'exécution.

## Itération de plage

Utilisez `for` avec deux points pour parcourir un tableau. Une variable de boucle régulière reçoit la valeur de l'élément :

```VeroScript
array<int> values = array<int>{1, 2, 3};
for (int value : values)
{
	print(value);
}
```

Utilisez une variable de boucle de référence pour modifier les éléments d'origine :

```VeroScript
for (int& value : values)
{
	value += 1;
}
```

Pour un tableau de structures, la modification d'une variable de boucle par valeur n'écrit pas dans le tableau. Utilisez `T&` pour modifier l'élément d'origine. Les tableaux dynamiques et natifs prennent en charge l’itération de référence.

## Choisir une forme de tableau

Utilisez `T[N]` lorsque la longueur est fixe et fait partie de la présentation des données. Utilisez `array<T>` lorsque la longueur change au moment de l'exécution ou lorsque des opérations telles que `push` et `remove_at` sont nécessaires. Utilisez `array<T>` lorsqu'un conteneur mutable doit être partagé entre des fonctions, et un tableau natif lorsqu'une valeur de mise en page fixe est requise.

`len(values)` et `values.capacity()` renvoient un `int64` non négatif. Les indices linéaires, `remove_at(index)` et `reserve(count)` acceptent `int64` ; un indice doit respecter `0 <= index < len(values)` et une capacité réservée doit être non négative et représentable sur l’hôte. Les limites d’allocation restent applicables. Les conversions vérifiées sont décrites dans les [fonctions intégrées](../standard-library/builtins.md).

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/arrays.md) · [fr-FR](arrays.md) · [ja-JP](../../ja-JP/language-basics/arrays.md) · [ko-KR](../../ko-KR/language-basics/arrays.md) · [zh-CN](../../zh-CN/language-basics/arrays.md) · [zh-TW](../../zh-TW/language-basics/arrays.md)
