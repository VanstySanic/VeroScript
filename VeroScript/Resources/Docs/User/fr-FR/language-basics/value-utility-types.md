# Types utilitaires de valeurs

VeroScript fournit un petit groupe de types génériques fournis par le compilateur pour les valeurs éventuellement absentes, les observations non propriétaires d’objets de classe, les charges utiles de structures partagées et les entrées de carte. Ils sont disponibles sans importation. Les écritures `std::` décrites dans [L’espace de noms `std`](standard-namespace.md) sont des alias de compatibilité vers ces mêmes types.

## `optional<T>`

`optional<T>` est un type valeur qui contient soit aucune valeur, soit une valeur de type `T`. Son état est explicite ; un optional construit par défaut est vide.

### Construction et méthodes

| Forme | Signification |
| --- | --- |
| `optional<T>()` | Construit un optional vide. |
| `optional<T>(value)` | Construit un optional engagé contenant `value`. |
| `has_value() const` | Indique si une valeur est présente. |
| `get() const` | Renvoie la valeur stockée. Appelé vide, il échoue via `assert`. |
| `value_or(fallback) const` | Renvoie la valeur stockée ou `fallback` si l’optional est vide. |
| `reset()` | Rend l’optional vide. |

```VeroScript
optional<int> score = optional<int>();
print(score.has_value());
print(score.value_or(0));

score = optional<int>(95);
if (score.has_value())
{
	print(score.get());
}

score.reset();
```

Un optional conserve la sémantique de valeur de `T`. Pour une structure, la copie d’un optional engagé copie la structure. Pour une classe, elle copie le handle de classe et conserve donc l’identité partagée. `reset()` libère également le handle de classe stocké, le cas échéant.

`try_cast<T>` renvoie un `optional<T>` pour une conversion susceptible d’échouer. Vérifiez `has_value()` avant d’appeler `get()` ; consultez [Conversions de type](../language-reference/type-conversions.md).

## `weak<T>`

`weak<T>` est une observation non propriétaire d’un objet de classe. `T` doit être une classe de script, une interface ou un type de classe hôte pris en charge ; les types primitifs et les structures ne sont pas des cibles valides. Une valeur faible n’empêche pas la destruction de sa cible.

### Méthodes de la référence faible

| Forme | Signification |
| --- | --- |
| `weak<T>()` | Construit une référence faible vide. |
| `weak<T>(target)` | Observe le handle de classe contenu dans `target`. |
| `lock() const` | Renvoie un handle fort `T` si la cible existe, sinon un handle nul. |
| `expired() const` | Indique si la cible ne peut plus être verrouillée. |
| `reset()` | Efface cette référence faible. |

```VeroScript
class Session
{
public:
	int id = 0;
};

Session owner = Session();
weak<Session> observer = weak<Session>(owner);

Session locked = observer.lock();
if (locked != nullptr)
{
	print(locked.id);
}
```

Copier une valeur faible crée une autre observation faible de la même cible. Réinitialiser une copie ne réinitialise pas les autres. Lorsque tous les handles de classe forts ont disparu, `expired()` devient vrai et `lock()` renvoie un handle nul. Vérifiez toujours le résultat avant d’accéder aux membres.

## `shared<T>` / `handle<T>`

`handle<T>` est une classe générique fournie par le compilateur avec un paramètre de type et un champ public nommé `value`. Elle est utile lorsqu’une valeur de structure doit être placée derrière une identité de classe partagée. Le constructeur par défaut crée le handle et construit `value` par défaut pour un type valeur ; pour une classe `T`, la charge utile suit la sémantique des handles de classe et vaut null par défaut. `handle<T>(value)` copie la charge utile dans un nouvel objet partagé.

`shared<T>` est la notation recommandée de `handle<T>` : les deux noms désignent exactement le même type, y compris dans les conteneurs, les fonctions génériques et `weak<T>`. `std::shared<T>` et `std::handle<T>` partagent cette identité. Un appel explicite à `shared<T>()` construit un wrapper non nul ; affecter `nullptr` vide le handle. Une structure est copiée dans le wrapper lors de sa construction ; les copies ultérieures du handle partagent le même objet. `weak<shared<T>>` observe ce wrapper ; `lock()` renvoie un handle fort du même objet, ou `nullptr` après expiration. Vérifiez ce résultat avant d’accéder à `.value`. `shared` est un alias de type intégré réservé, pas un mot-clé syntaxique. Selon les règles génériques souples existantes, si l’utilisateur déclare un `handle<T>` de même nom et de même arité, les deux notations désignent ce type utilisateur.

Avec un seul argument de contenu, `auto h = shared(value)` déduit `T` ; une construction sans argument nécessite un type cible explicite ou des arguments de type.

L’émission de code source comporte des limites selon le backend : C++ prend en charge les enveloppes partagées et les références faibles. C# prend en charge les enveloppes partagées, mais la construction avec une charge utile de type struct ou un paramètre générique ouvert exige une valeur explicite, comme `shared<Point>(Point())` ; `weak<T>` n’est pas encore pris en charge. L’émission Python autonome ne prend pas en charge les enveloppes partagées ni les références faibles ; la liaison Python les prend en charge via le cœur. Les exemples complets de durée de vie fonctionnent avec les backends VM, Mixed et JIT.

### Construction et partage

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;
};

void bump_x(handle<Point> point)
{
	point.value.x += 1;
}

shared<Point> first = shared<Point>(Point());
handle<Point> second = first;
first.value.x = 10;
bump_x(second);
print(first.value.x);
```

Affecter `first` à `second` copie le handle de classe ; les deux variables observent et modifient donc le même `value`. Affecter `nullptr` efface le handle. Pour un type struct `T`, construire `handle<T>(source)` crée une nouvelle copie de la charge utile ; les modifications ultérieures de `source` ne modifient pas cette copie.

`handle<T>` est un wrapper partagé, pas un remplacement général d’une classe. Convertir `h.value` d’une structure vers une interface crée une copie de valeur. Si une interface doit exposer une identité mutable partagée, utilisez une classe qui implémente l’interface.

L’exemple exécutable complet est [examples/handle_struct.vs](../../../../Scripts/examples/handle_struct.vs).

## `pair<T, U>`

`pair<T, U>` est une structure générique fournie par le compilateur avec deux champs publics :

| Champ | Type |
| --- | --- |
| `first` | `T` |
| `second` | `U` |

Les API de carte utilisent `pair` pour représenter une entrée clé/valeur. `m.entries()` renvoie un tableau de snapshots `pair<K, V>` par valeur :

```VeroScript
map<string, int> scores = map<string, int>();
scores["alice"] = 95;

for (pair<string, int> entry : scores.entries())
{
	print("{} -> {}", entry.first, entry.second);
}
```

Parcourir directement la carte peut exposer une vue d’entrée modifiable. `pair<K, V>&` écrit dans la carte, tandis que `pair<K, V>` est une copie :

```VeroScript
for (pair<string, int>& entry : scores)
{
	entry.second += 1;
}

for (auto& [key, value] : scores)
{
	value += 1;
}
```

Ne considérez pas un `pair` renvoyé par `entries()` comme une référence vers l’entrée originale. Pour les règles complètes d’itération, consultez [Références](references.md) et [Conteneurs](containers.md). L’exemple exécutable de mappage est [examples/pair_map.vs](../../../../Scripts/examples/pair_map.vs).

## Nommage et compatibilité

`optional`, `readonly`, `weak` et les types d’information de réflexion sont des noms de types intégrés strictement réservés. Un script ne peut pas les redéclarer.

`pair<T, U>` et `handle<T>` sont des génériques intégrés souples. Le compilateur fournit `pair` avec deux paramètres et `handle` avec un paramètre uniquement lorsqu’aucune déclaration utilisateur de même nom et de même arité générique n’existe. Un `pair` ou `handle` non générique, ou une déclaration d’une autre arité, peut coexister avec la forme générique souple correspondante.

Les alias de l’espace de noms standard sont `std::optional`, `std::weak`, `std::shared`, `std::handle` et `std::pair`. Ces alias ne créent pas de types distincts.

## Références associées

- [Types de données](data-types.md) : types intégrés, conteneurs et wrappers en lecture seule.
- [Génériques](generics.md) : types génériques définis par l’utilisateur et génériques souples.
- [Conversions de type](../language-reference/type-conversions.md) : `cast`, `try_cast`, `const_cast` et résultats de conversion.
- [Conteneurs](containers.md) : opérations sur les cartes et itération de plage.

Pour un objet script, le début de la destruction termine sa durée de vie faible : lock() renvoie null et expired() renvoie true pendant et après la destruction. Faire échapper this depuis le destructeur ne réactive pas l’objet ; les accès ultérieurs aux champs et méthodes sont refusés. Les objets hôtes conservent leur contrat de durée de vie.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/value-utility-types.md) · [fr-FR](value-utility-types.md) · [ja-JP](../../ja-JP/language-basics/value-utility-types.md) · [ko-KR](../../ko-KR/language-basics/value-utility-types.md) · [zh-CN](../../zh-CN/language-basics/value-utility-types.md) · [zh-TW](../../zh-TW/language-basics/value-utility-types.md)
