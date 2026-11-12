# Références et sémantique des références

Une référence donne un alias à un emplacement de stockage ou à un emplacement de handle existant. Il ne crée pas d'objet indépendant et ne possède pas l'objet dont il donne l'alias. Les références VeroScript sont utilisées pour les paramètres, les alias locaux, les itérations de plage, l'accès aux éléments, les retours de référence et les captures de référence lambda. Ce ne sont pas des pointeurs de script à usage général et ne prennent pas en charge l'arithmétique des pointeurs.

## Paramètres de référence

L'orthographe recommandée utilise un `&` final :

```VeroScript
void increment(int& value)
{
	value += 1;
}

int main()
{
	int value = 1;
	increment(&value);
	print(value);
	return 0;
}
```

`ref T` et `const ref T` sont des orthographes de style mot-clé équivalentes :

```VeroScript
void reset(ref int value)
{
	value = 0;
}

int main()
{
	int value = 1;
	reset(ref value);
	return 0;
}
```

Les marqueurs de déclaration et d'appel doivent correspondre : `T&` utilise `&expression`, tandis que `ref T` utilise `ref expression`. Un argument de référence doit identifier le stockage, tel qu'une variable, un élément de tableau, un élément de carte ou un champ d'objet.

## Références en lecture seule

Utilisez `const T&` ou `const ref T` pour une référence en lecture seule. Il peut lire la cible mais ne peut pas la modifier via l'alias :

```VeroScript
int read_value(const int& value)
{
	return value;
}

int main()
{
	const int value = 42;
	print(read_value(&value));
	return 0;
}
```

Une référence mutable ne peut pas se lier à un objet const ou à un retour de référence const. Préférez une référence en lecture seule lorsqu’une fonction n’a besoin que d’inspecter un objet.

## Alias de référence locale

Une variable locale peut être déclarée comme alias d'un objet existant :

```VeroScript
int main()
{
	int value = 1;
	int& alias = value;
	const int& view = value;

	alias = 2;
	print(view);
	return 0;
}
```

`alias` et `view` font tous deux référence au stockage de `value`. Une variable de référence ne devient pas un objet indépendant et ne copie pas sa cible.

## Références dans l'itération de plage

Une variable de plage normale reçoit une valeur d'élément. Pour les types valeur tels que les structures, la modification de la variable de boucle ne réécrit pas dans le conteneur d'origine :

```VeroScript
for (Point point : points)
{
	point.x = 0;
}
```

Utilisez `T&` pour faire de la variable de boucle un alias de l'élément d'origine :

```VeroScript
for (Point& point : points)
{
	point.x = 0;
}
```

Les tableaux, les tableaux natifs de taille fixe, les deques et les valeurs de carte prennent en charge l'itération de la plage de référence. Utilisez `const T&` pour une collection en lecture seule.

Lorsque l’itération de la carte nécessite à la fois une clé et une valeur, utilisez `pair<K, V>&` ; lorsque seule la valeur est nécessaire, parcourez `V&` :

```VeroScript
for (pair<string, int>& entry : counts)
{
	entry.second += 1;
}
```

## Références d'éléments

Un élément de tableau ou de carte peut être passé directement comme argument de référence :

```VeroScript
void bump(int& value)
{
	value += 1;
}

bump(&values[0]);
bump(&counts["total"]);
```

Une référence d'élément ne copie pas l'élément, donc les modifications apportées via celui-ci restent dans le conteneur d'origine après l'appel. Les champs imbriqués et les chemins d'éléments peuvent également produire des références tant que chaque objet du chemin reste valide.

## Retours de référence

Une fonction peut renvoyer une référence mutable ou en lecture seule. Une expression renvoyant une référence reste une lvalue assignable et peut être incrémentée ou liée à une autre référence :

```VeroScript
int Counter = 1;
array<int> Values = array<int>();

int& counter_ref()
{
	return Counter;
}

int& first_value()
{
	return Values[0];
}

const int& read_counter()
{
	return Counter;
}

int main()
{
	Values.push(40);
	counter_ref() += 4;
	first_value() += 2;
	print(read_counter());
	return 0;
}
```

Un retour de référence doit pointer vers un stockage qui reste valide. Il peut renvoyer un élément global, un conteneur, un champ d'objet ou une référence existante ; il ne peut pas renvoyer une variable locale de fonction ordinaire dont le stockage de pile se termine par la fonction. Une référence mutable ne peut pas se lier à un retour de référence const.

Une variable de handle de classe peut également être renvoyée via un retour de référence : la référence pointe vers l'emplacement du handle plutôt que de copier un nouvel objet. Ceci est différent du comportement d'identité partagée de la classe elle-même et doit être documenté explicitement dans une API.

## Références et types de fonctions

La direction de référence fait partie d’un type de fonction. Les valeurs de fonction et les lambdas peuvent utiliser `int&` ou `const int&`, et l'appel a toujours besoin d'un argument `&` :

```VeroScript
void add_one(int& value)
{
	value += 1;
}

int main()
{
	function(int&) -> void mutate = add_one;
	int value = 3;
	mutate(&value);
	print(value);
	return 0;
}
```

## Restrictions et durée de vie

- Les références modifiables ne peuvent pas avoir de valeur par défaut. Les références en lecture seule acceptent les valeurs littérales par défaut prises en charge, via des temporaires conservés par l’appelant. Les références ne peuvent pas être variadiques.
- Les références ne possèdent pas d'objets, ne prennent pas en charge l'arithmétique des pointeurs et ne prolongent pas automatiquement la durée de vie des objets ordinaires.
- `ref`, `const ref`, `T&` et `const T&` affectent les types de fonctions, les surcharges et les vérifications de liaison.
- Une référence ordinaire doit être utilisée tant que son stockage cible reste valide.
- Les captures de référence Lambda utilisent des cellules de référence partagées gérées, de sorte qu'une fermeture peut survivre en toute sécurité à la fonction qui l'a créée ; voir [Lambdas et valeurs de fonction](lambdas.md).

Des exemples de référence exécutables sont répertoriés dans l'[index des exemples](../examples.md).

Une référence à un élément emprunte le conteneur et l’entrée d’origine. Réaffecter la variable du conteneur ne change pas sa cible. Une affectation sur place, un ajout en fin ou l’insertion d’une autre clé préservent la cible ; une entrée expirée ou remplacée produit un diagnostic de référence invalide. Les retours par référence gérés et les captures Lambda conservent leurs règles de durée de vie.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/references.md) · [fr-FR](references.md) · [ja-JP](../../ja-JP/language-basics/references.md) · [ko-KR](../../ko-KR/language-basics/references.md) · [zh-CN](../../zh-CN/language-basics/references.md) · [zh-TW](../../zh-TW/language-basics/references.md)
