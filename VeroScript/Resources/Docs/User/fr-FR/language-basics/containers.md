# Conteneurs

Les types de conteneurs VeroScript utilisent des arguments génériques pour décrire leurs types d'élément ou de clé/valeur. À l'exception des tableaux natifs de taille fixe, les conteneurs sont généralement représentés par des descripteurs partagés ; l'attribution d'un conteneur à une autre variable peut faire en sorte que les deux variables fassent référence au même objet conteneur.

## Règles communes

Les types de conteneurs utilisent des crochets angulaires pour les arguments de type et `len` renvoie leur nombre d'éléments ou d'entrées :

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();

print(len(values));
print(len(counts));
```

Les éléments insérés doivent correspondre au type d'élément déclaré. Les types de valeur tels que les structures sont copiés lors de l'insertion ; les classes et autres types de handles conservent leur sémantique de handle.

La plupart des conteneurs prennent en charge l'itération de plage. Une variable de boucle de valeur reçoit une copie d'élément ; une variable de boucle `T&` peut modifier l'élément dans le conteneur. Les cartes prennent également en charge les itérations de valeur, de clé et d’entrées clé/valeur.

## `map` et `ordered_map`

`map<K, V>` associe les clés aux valeurs et utilise l'indexation pour accéder à une valeur :

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;
scores["bob"] = 88;

print(scores["alice"]);
print(scores.has("bob"));
```

Les opérations courantes comprennent :

| Opération | Descriptif |
| --- | --- |
| `m[key]` | Lire ou écrire la valeur d'une clé |
| `m.has(key)` | Tester si une clé existe |
| `m.remove(key)` | Supprimer une clé et indiquer si elle a été supprimée |
| `m.keys()` | Renvoyez les clés sous `array<K>` |
| `m.values()` | Renvoie les valeurs sous la forme `array<V>` |
| `m.entries()` | Renvoie les entrées clé/valeur sous forme de tableau |
| `m.clear()` | Effacer la carte |
| `len(m)` | Renvoie le nombre de clés |

`map` n’est pas ordonné ; l’ordre des clés renvoyées par `keys()` n'est pas spécifiée. `ordered_map` conserve l’ordre de première insertion des clés distinctes. La mise à jour d'une clé existante ne la déplace pas.

## `multimap` et `ordered_multimap`

`multimap<K, V>` permet à une clé d'avoir plusieurs valeurs. Utilisez `add` pour ajouter une valeur ; l'indexation renvoie le `array<V>` associé à la clé :

```VeroScript
multimap<string, int> grouped = multimap<string, int>();
grouped.add("alpha", 1).add("alpha", 2);

print(grouped["alpha"][0]);
print(len(grouped["alpha"]));
```

`remove(key)` supprime toutes les valeurs stockées sous cette clé. `ordered_multimap` conserve en outre l’ordre de première insertion des clés distinctes.

## `set` et `multiset`

`set<T>` stocke des éléments uniques. L'ajout d'un élément existant ne crée pas de deuxième élément :

```VeroScript
set<string> names = set<string>();
names.add("alice");
names.add("alice");
print(names.has("alice"));
```

`remove(value)` supprime un élément et `clear()` vide l'ensemble. `multiset<T>` autorise les valeurs répétées et fournit `count(value)` :

```VeroScript
multiset<int> numbers = {2, 1, 2, 3, 2};
print(numbers.count(2));
numbers.remove(2);
```

Pour un `multiset`, `remove(value)` supprime une occurrence. `ordered_multiset` conserve le premier ordre d’insertion des valeurs distinctes.

## Files d'attente, piles, files d'attente prioritaires et deques

| Type | Sémantique | Opérations communes |
| --- | --- | --- |
| `queue<T>` | Premier entré, premier sorti | `push`, `pop`, `front` |
| `stack<T>` | Dernier entré, premier sorti | `push`, `pop`, `top` |
| `priority_queue<T>` | Supprime par priorité ; l'implémentation actuelle renvoie le maximum en premier | `push`, `pop`, `top` |
| `deque<T>` | Insertion et retrait aux deux extrémités, plus indexation entière | `push_front`, `push_back`, `pop_front`, `pop_back` |

Par exemple, `queue<T>` ajoute à l'arrière et supprime depuis l'avant, tandis que `stack<T>` ajoute et supprime en haut. Utilisez `len` pour obtenir le nombre actuel d’éléments.

## Itération de la carte

Une carte peut être itérée par valeur ou par entrée clé/valeur :

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (int value : scores)
{
	print(value);
}

for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}
```

Vous pouvez également parcourir `scores.keys()` et accéder à chaque valeur avec `scores[key]`. L'ordre d'une carte non ordonnée ne doit pas être utilisé comme logique de programme.

### Snapshots et vues d’entrées

`m.entries()` renvoie un `array<pair<K, V>>` de snapshots par valeur. Un `pair<K, V>&` obtenu en parcourant directement `m` est une vue modifiable de l’entrée de la carte. Ne traitez pas une référence vers un élément de `m.entries()` comme une référence vers la carte ; modifiez-la par une itération directe des entrées ou avec `m[key]`.

## `channel`

`channel<T>` est un handle partagé utilisé pour communiquer entre des tâches simultanées. La bibliothèque standard `concurrent.vs` fournit les fonctions `channel_send`, `channel_recv` et `channel_close` :

```VeroScript
import "concurrent.vs";

channel<int> values = channel<int>();
std::concurrent::channel_send(values, 42);
int result = std::concurrent::channel_recv(values);
std::concurrent::channel_close(values);
```

Les canaux sont normalement utilisés avec `spawn`, `join` et les fonctions de tâche. Le comportement de blocage, les types transférables et la durée de vie des tâches appartiennent à la référence d’exécution simultanée plutôt qu’à la référence générale du conteneur.

Les longueurs et nombres d’éléments, dont `multiset.count(value)`, renvoient un `int64` non négatif. Les indices de deque utilisent `int64`, tandis que les clés de map gardent leur type déclaré. Consultez les [fonctions intégrées](../standard-library/builtins.md) pour les conversions et limites de longueur, et la [concurrence](../standard-library/concurrent.md) pour les nombres et capacités des canaux.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/containers.md) · [fr-FR](containers.md) · [ja-JP](../../ja-JP/language-basics/containers.md) · [ko-KR](../../ko-KR/language-basics/containers.md) · [zh-CN](../../zh-CN/language-basics/containers.md) · [zh-TW](../../zh-TW/language-basics/containers.md)
