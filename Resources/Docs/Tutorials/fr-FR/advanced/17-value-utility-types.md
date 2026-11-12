# 17 : Valeurs optionnelles, références faibles et handles de structures partagées

Ce didacticiel construit un petit exemple de partage d’état avec les types utilitaires fournis par le compilateur VeroScript. Vous apprendrez quand utiliser `optional<T>`, `weak<T>`, `handle<T>` et `pair<T, U>`, ainsi que leurs différences de valeur et de propriété.

`shared<T>` est la notation recommandée de `handle<T>` : les deux noms désignent exactement le même type, y compris dans les conteneurs, les fonctions génériques et `weak<T>`. `std::shared<T>` et `std::handle<T>` partagent cette identité. Un appel explicite à `shared<T>()` construit un wrapper non nul ; affecter `nullptr` vide le handle. Une structure est copiée dans le wrapper lors de sa construction ; les copies ultérieures du handle partagent le même objet. `weak<shared<T>>` observe ce wrapper ; `lock()` renvoie un handle fort du même objet, ou `nullptr` après expiration. Vérifiez ce résultat avant d’accéder à `.value`. `shared` est un alias de type intégré réservé, pas un mot-clé syntaxique. Selon les règles génériques souples existantes, si l’utilisateur déclare un `handle<T>` de même nom et de même arité, les deux notations désignent ce type utilisateur.

## Objectif

À la fin, vous devriez savoir :

- représenter un résultat éventuellement absent sans valeur sentinelle ;
- observer une classe sans la maintenir en vie ;
- partager la charge utile d’une structure via un handle de classe ;
- distinguer les snapshots d’entrées de carte des vues d’entrées modifiables.

La référence complète est [Types utilitaires de valeurs](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/value-utility-types.md). Exécutez `--check` pour chaque exemple avant de l’exécuter.

## Étape 1 : représenter un résultat optionnel

Utilisez `optional<T>()` pour un résultat vide et `optional<T>(value)` pour un résultat contenant une valeur. Vérifiez `has_value()` avant d’appeler `get()` :

```VeroScript
optional<int> find_score(bool available)
{
	if (available)
	{
		return optional<int>(95);
	}
	return optional<int>();
}

int main()
{
	optional<int> missing = find_score(false);
	print(missing.value_or(0));

	optional<int> found = find_score(true);
	if (found.has_value())
	{
		print(found.get());
	}

	return 0;
}
```

`value_or` convient lorsqu’une valeur par défaut suffit. `get()` convient après une vérification d’état explicite ; l’appeler sur un optional vide échoue via `assert`. `try_cast<T>` utilise le même modèle pour les conversions susceptibles d’échouer.

## Étape 2 : observer une classe sans la posséder

Une variable de classe est un handle fort. Une valeur `weak<T>` observe cet objet sans le maintenir en vie :

```VeroScript
class Session
{
public:
	int id = 0;
};

int main()
{
	Session owner = Session();
	owner.id = 42;

	weak<Session> watcher = weak<Session>(owner);
	if (!watcher.expired())
	{
		Session locked = watcher.lock();
		print(locked.id);
	}

	owner = nullptr;
	print(watcher.expired());
	return 0;
}
```

`lock()` renvoie un handle fort lorsque l’objet existe encore, sinon un handle nul. Vérifiez toujours `expired()` ou le résultat de `lock()` avant d’accéder aux membres. Copier une valeur faible crée une autre observation ; réinitialiser une copie ne réinitialise pas l’autre.

## Étape 3 : partager une charge utile de structure

L’affectation d’une structure copie normalement une valeur. Lorsque plusieurs parties du programme doivent modifier une même charge utile de structure, placez-la dans `handle<T>` :

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void move_right(handle<Point> point)
{
	point.value.x += 1;
}

int main()
{
	shared<Point> first = shared<Point>(Point());
	handle<Point> second = first;

	first.value.x = 10;
	move_right(second);
	print(first.value.x);
	print(second.value.x);

	return 0;
}
```

Affecter un `handle<T>` à un autre copie le handle de classe : les deux variables voient donc le même `value`. `handle<T>(source)` copie au contraire la charge utile dans un nouvel objet partagé. Affecter `nullptr` efface le handle. L’exemple complet est [examples/handle_struct.vs](../../../../../VeroScript/Resources/Scripts/examples/handle_struct.vs).

## Étape 4 : choisir une vue de `pair`

Les API de carte utilisent `pair<K, V>`. Le résultat de `entries()` est un tableau de snapshots par valeur ; modifier un snapshot ne modifie donc pas la carte. Parcourir directement la carte avec `pair<K, V>&` fournit une vue d’entrée modifiable :

```VeroScript
int main()
{
	ordered_map<string, int> scores = ordered_map<string, int>();
	scores["alice"] = 95;
	scores["bob"] = 88;

	for (pair<string, int> snapshot : scores.entries())
	{
		print("{} -> {}", snapshot.first, snapshot.second);
	}

	for (pair<string, int>& entry : scores)
	{
		entry.second += 1;
	}

	for (auto& [key, value] : scores)
	{
		print("{} -> {}", key, value);
	}

	return 0;
}
```

Utilisez `m.entries()` lorsque vous avez besoin d’une collection de valeurs d’entrées indépendantes. Utilisez l’itération directe de la carte lorsque vous devez écrire dans une entrée. L’exemple complet est [examples/pair_map.vs](../../../../../VeroScript/Resources/Scripts/examples/pair_map.vs).

## Exercices

1. Modifiez `find_score` pour renvoyer un message `optional<string>` au lieu d’un entier.
2. Ajoutez un second observateur `weak<Session>` et vérifiez que réinitialiser l’un ne réinitialise pas l’autre.
3. Ajoutez une fonction de déplacement selon `y` à l’exemple `handle<Point>` et appelez-la via le second handle.
4. Ajoutez un `map<string, Point>` et modifiez un champ imbriqué via `pair<string, Point>&`.

## À retenir

- `optional<T>` représente une valeur absente ou présente et est un type valeur.
- `weak<T>` observe une classe sans prolonger sa durée de vie.
- `handle<T>` partage un objet de classe dont le champ public `value` contient la charge utile.
- `pair<K, V>` provenant de `entries()` est un snapshot ; une référence de pair issue de l’itération directe écrit dans la carte.

Suite : [Utiliser l’espace de noms standard std](14-standard-namespace.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/advanced/17-value-utility-types.md) · [fr-FR](17-value-utility-types.md) · [ja-JP](../../ja-JP/advanced/17-value-utility-types.md) · [ko-KR](../../ko-KR/advanced/17-value-utility-types.md) · [zh-CN](../../zh-CN/advanced/17-value-utility-types.md) · [zh-TW](../../zh-TW/advanced/17-value-utility-types.md)
