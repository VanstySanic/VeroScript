# L'espace de noms standard `std`

VeroScript fournit un espace de noms `std` réservé au langage pour les conteneurs intégrés, les types de valeurs utilitaires, les types de réflexion et les fonctions d'exécution publique. Le nouveau code doit utiliser l’orthographe qualifiée `std::`. Les utilitaires de module utilisent des espaces de noms enfants basés sur les responsabilités ; les formulaires plats obsolètes tels que `std::contains` et `std::read_file` ne sont pas des alias.

## Règles de résolution

`std::name` et `std.name` sont acceptés. Le code public doit utiliser `std::name`. Le compilateur résout ces noms en déclarations de langage ou de bibliothèque standard existantes, de sorte que les alias ne créent pas de deuxième ABI de conteneur ni n'ajoutent de répartition d'exécution. Les fonctions de formatage sont des points d'entrée publics fournis par le compilateur ; les autres fonctions de la bibliothèque standard source suivent toujours les règles d'importation ordinaires.

```VeroScript
import "math.vs";

int main()
{
	std::array<int> values = {2, 3, 5};
	std::optional<int> selected = std::optional<int>(values[0]);

	std::print("selected={}, root={}", selected.get(), std::sqrt(81.0));
	return 0;
}
```

Les types du langage et les fonctions directes du runtime conservent leur forme non qualifiée. Les API appartenant à un module sont volontairement accessibles uniquement par leur espace de noms, par exemple `std::algorithm::sort` après l'import du module. Il ne s'agit pas d'une couche de compatibilité pour les anciens noms `std`.

`std` est réservé. Un script ne peut pas ajouter de déclarations à `std` ou à l'un de ses espaces de noms enfants. Les alias d'espace de noms ne modifient pas non plus la visibilité du module : un alias vers une fonction de bibliothèque standard source nécessite toujours d'importer le module qui le fournit.

## Alias de type

Le tableau suivant répertorie les alias de type public. Le côté droit est l’identité du langage ou du type de réflexion existant.

| `std::` orthographe | Nom canonique |
| --- | --- |
| `string`, `wstring` | `string`, `wstring` |
| `array`, `map`, `multimap` | Conteneurs du même nom |
| `ordered_map`, `ordered_multimap` | Cartes ordonnées du même nom |
| `set`, `multiset`, `ordered_multiset` | Ensembles du même nom |
| `queue`, `priority_queue`, `stack`, `deque` | Conteneurs de séquence du même nom |
| `channel`, `task`, `concurrent::task_group`, `concurrent::mutex`, `concurrent::counting_semaphore` | Types de canaux, de tâches simultanées, de mutex et de sémaphores |
| `pair` | `pair` |
| `optional`, `weak` | Types facultatifs et faibles du même nom |
| `readonly` | Wrapper de valeur en lecture seule |
| `shared`, `handle` | `handle` |
| `delegate`, `multicast_delegate` | `delegate`, `multicast_delegate` |
| `type`, `field_info`, `method_info` | Types d'informations de réflexion |
| `module_info`, `function_info`, `global_info` | Types de modules, de fonctions et d'informations globales |
| `enum_member_info` | Type d'informations sur le membre Enum |

Les alias modifient uniquement l’orthographe source. Les arguments génériques sont toujours vérifiés à l'aide des règles de type d'origine, donc `std::map<string, int>` reste une carte à deux paramètres.

## Écritures des conversions

`std::readonly<T>` est l’écriture dans l’espace de noms standard de `readonly<T>`. Les intégrés de conversion restent globaux : utilisez `cast<T>`, `try_cast<T>`, `const_cast<T>` et `(T)expression` entre parenthèses sans préfixe `std::`. Des formes telles que `std::cast<T>` et `std::try_cast<T>` ne sont pas des alias documentés. Consultez [Conversions de type](../language-reference/type-conversions.md) pour le comportement et les règles d’échec.

```VeroScript
std::readonly<int> published = std::readonly<int>(41);
double precise = cast<double>(published.get());
optional<int> narrowed = try_cast<int>(precise);
```

## Alias de fonction

### Fonctions d'exécution directe

Ces noms font référence à des éléments intégrés ou à des fonctions fournies directement par le runtime :

| Catégorie | Fonctions |
| --- | --- |
| Sortie et formatage | `print`, `eprint`, `printf`, `format`, `formatf`, `assert`, `std::console::{write, error, flush, clear, is_interactive}` |
| Longueur et conversion | `len`, `to_string`, `to_wstring`, `to_int`, `to_unsigned`, `to_int64`, `to_uint64`, `to_double`, `to_float` |
| Numérique | `abs`, `min`, `max` |
| Utilitaires d'exécution | `range`, `exit` |

Le temps, la console, les threads et l'environnement utilisent des noms basés
sur la responsabilité : `std::chrono::now`, `std::chrono::steady_now`,
`std::this_thread::sleep`, `std::environment::getenv` et
`std::environment::setenv`. Aucun alias `std` plat correspondant n'est conservé :

```VeroScript
int main()
{
	std::print("elapsed={}", std::chrono::now() > 0.0);
	string mode = std::environment::getenv("MODE");
	std::this_thread::sleep(1);
	return 0;
}
```

### Fonctions des modules

Les noms suivants sont des orthographes d’espace de noms standard des fonctions de bibliothèque standard exportées. Ils suivent toujours les règles `import` ordinaires :

| Module | Fonctions |
| --- | --- |
| `io.vs` | `std::io::{read_file, write_file, append_file, read_line, pause, read_token, read_string, read_int, read_double, read_bool}` |
| `io.vs` | `std::filesystem::{delete_file, exists, is_file, is_directory, remove_dir, list_dir, mkdir, cwd, chdir}` |
| `string.vs` | `std::strings::{substring, find, last_find, char_code, contains, starts_with, ends_with, trim_left, trim_right, trim, split, join, to_lower, to_upper, replace, repeat, remove_prefix, remove_suffix}` |
| `path.vs` | `std::filesystem::{basename, dirname, join_path, extension, change_extension}` |
| `concurrent.vs` | `std::concurrent::{channel_send, channel_recv, channel_close, send, recv, close, make_channel, try_send, send_for, try_recv, recv_for, is_closed, pending, capacity, spawn, spawn_task, join_task, cancel_task, cancel_requested, when_all, cancel_all, make_mutex, mutex_lock, mutex_try_lock, mutex_lock_for, mutex_unlock, make_semaphore, semaphore_acquire, semaphore_try_acquire, semaphore_acquire_for, semaphore_release, make_synchronized, synchronized_load, synchronized_store, synchronized_exchange, synchronized_fetch_add}` |
| `math.vs` | Noms numériques plats : `std::{sqrt, sin, cos, tan, floor, ceil, round, trunc, pow, exp, log, log2, asin, acos, atan2, isfinite, isnan, pi, e, clamp, clamp_int}` |
| `algorithm.vs` | `std::algorithm::{sort, reverse, find_index, count, lower_bound, binary_search, fill, copy, contains, equal, is_sorted, min_index, max_index, rotate, shuffle}` |
| `numeric.vs` | `std::numeric::{accumulate, sum, average}` |
| `random.vs` | `std::random::{make_engine, seed, next_uint, next_bounded_uint, next_int, next_double, next_double_range, next_bool}` |

Par exemple, `std::sqrt` est disponible dans l’exemple précédent car il importe `math.vs`. L'écriture de `std::sqrt` seule ne charge pas un module de bibliothèque standard ; `std::format` et `std::formatf` sont des points d’entrée de formatage fournis par le compilateur et ne nécessitent pas `string.vs` uniquement pour être résolus.

## Syntaxe uniforme d’appel de fonction

Une fonction libre visible peut être appelée avec son premier argument comme récepteur :

```VeroScript
int grow(ref Counter self, int amount)
{
	/* ... */
}

counter.grow(2); // grow(ref counter, 2)
```

Les membres réels, virtuels, hôtes et intégrés gagnent toujours. Ce n'est que lorsqu'aucun membre réel ne correspond que le compilateur essaie l'appel de fonction libre équivalent. L'inférence générique, la résolution de surcharge, les importations, la visibilité des paquets, les valeurs par défaut, les conversions et les règles `ref`/`const ref` sont partagées avec les appels ordinaires. Un récepteur de référence se lie implicitement ; si les deux formes existent, une surcharge de référence-récepteur compatible est préférable à la copie du récepteur. Cette réduction est uniquement au moment de la compilation.

`concurrent.vs` utilise ce mécanisme pour les canaux :

```VeroScript
import "concurrent.vs";

channel<int> messages = channel<int>();
messages.send(42);
int value = messages.recv();
messages.close();
```

## Frontières

- Un script ne peut pas déclarer `namespace std` ou enregistrer un type d'utilisateur ou une fonction sous l'arborescence de noms `std`.
- Les alias `std::` ne sont pas des importations de modules ; les modules restent contrôlés par `import`, `export` et les règles de visibilité des paquets.
- Ne dépendez pas des noms commençant par `__`. Les orthographes `std::` documentées et les pages de bibliothèque standard constituent l'interface publique stable.

La table d'alias complète est conservée par le registre de la bibliothèque standard d'exécution. Voir [Espaces de noms](namespaces.md) pour les règles d'espace de noms ordinaires et [Référence de la bibliothèque standard](../standard-library/index.md) pour les signatures de module.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/standard-namespace.md) · [fr-FR](standard-namespace.md) · [ja-JP](../../ja-JP/language-basics/standard-namespace.md) · [ko-KR](../../ko-KR/language-basics/standard-namespace.md) · [zh-CN](../../zh-CN/language-basics/standard-namespace.md) · [zh-TW](../../zh-TW/language-basics/standard-namespace.md)
