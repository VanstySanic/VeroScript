# Module de concurrence

Fichier du module : `concurrent.vs`

Importez-le avec :

```VeroScript
import "concurrent.vs";
```

## Canaux

`channel<T>` est un conteneur partagé utilisé pour la communication entre les tâches. La bibliothèque standard expose des opérations génériques pour chaque type d'élément transférable :

| Fonction | Descriptif |
| --- | --- |
| `std::concurrent::channel_send<T>(channel<T> ch, T value)` | Envoie une valeur à un canal. |
| `std::concurrent::channel_recv<T>(channel<T> ch)` | Reçoit une valeur d’un canal. |
| `std::concurrent::channel_close<T>(channel<T> ch)` | Ferme une chaîne. |
| `std::concurrent::make_channel<T>(int64 capacity = 0)` | Crée un canal non borné ou borné. |
| `ch.try_send(value)` / `ch.send_for(value, timeout_ms)` | Envoie sans bloquer ou avec un délai ; renvoie `false` en cas d’échec. |
| `ch.try_recv()` / `ch.recv_for(timeout_ms)` | Renvoie `optional<T>` ; une valeur vide indique un canal vide, fermé ou arrivé à expiration. |
| `ch.pending()` / `ch.capacity()` / `ch.is_closed()` | La profondeur et la capacité renvoient un `int64` non négatif (capacité `0` : non bornée) ; l’état fermé renvoie un `bool`. |

Les formulaires plus courts `send`, `recv` et `close` prennent en charge à la fois les appels d'espace de noms et la syntaxe d'extension : `ch.send(value)`, `ch.recv()` et `ch.close()`. `T` peut être un scalaire transférable pris en charge par le canal (largeurs entières signées/non signées, `float`, `double`, `bool`, `string` ou `wstring`) ou un autre `channel<U>`. Les canaux imbriqués transmettent des capacités synchronisées entre des workers isolés sans partager d'objets mutables ordinaires. La réception depuis un canal ouvert peut attendre ; ne poursuivez pas les opérations qui nécessitent un envoi ou une réception valide après la fermeture d'un canal.

Les valeurs de canal utilisent une sémantique de handle partagé.

## Tâches isolées

`std::concurrent::spawn` démarre une tâche isolée et renvoie un ID de tâche. Le point d'entrée du travailleur doit correspondre au type de canal :

```VeroScript
import "concurrent.vs";

void worker(channel<int> ch)
{
	int value = ch.recv();
	ch.send(value + 1);
}

int main()
{
	channel<int> ch = channel<int>();
	int task_id = std::concurrent::spawn("worker", ch);
	ch.send(41);
	std::concurrent::join_task(task_id);
	int result = ch.recv();
	print(result);
	return 0;
}
```

`std::concurrent::join_task` attend la fin d'une tâche. Les tâches doivent échanger les valeurs prises en charge via des canaux au lieu de partager directement les scripts locaux ordinaires.

`std::concurrent::spawn<T>` accepte tout `channel<T>` pris en charge lorsque le travailleur nommé possède la signature d'entrée correspondante exacte :

```text
int std::concurrent::spawn<T>(string name, channel<T> ch);
```

## Résultats de tâches typés

`std::concurrent::spawn_task<T>` renvoie un `task<T>` pour les travailleurs qui publient un résultat via un canal de résultats :

```VeroScript
import "concurrent.vs";

void compute(channel<int> output)
{
	output.send(42);
}

int main()
{
	std::task<int> result = std::concurrent::spawn_task<int>("compute");
	int value = result.await();
	print(value);
	return 0;
}
```

Une entrée chaîne désigne exactement `void compute(channel<T> output)`. Le worker envoie un résultat unique avant son retour. Après sa fin, un résultat absent ou multiple est une erreur ; un worker encore bloqué nécessite une annulation coopérative.

`task<T>` fournit ces membres pour chaque type de canal transférable pris en charge :

| Membre | Descriptif |
| --- | --- |
| `valid()` | Teste si la tâche est toujours active. |
| `done()` | Teste si la tâche est terminée sans attendre. |
| `await()` | Attend la tâche, reçoit un résultat et la marque comme inactive. |
| `await_for(timeout_ms)` | Seul un délai dépassé renvoie un optional vide et conserve la tâche active. Après sa fin, le worker doit avoir fourni exactement un résultat ; sinon une erreur est signalée. |
| `cancel()` | Annule la tâche de manière coopérative et ferme son canal de résultats. La VM conserve le worker jusqu'à sa fin. |

L’annulation coopérative ne termine ni ne détache de force un thread. Le worker doit observer la fermeture du canal et quitter ; la destruction de la VM/session attend les workers qui ignorent l’annulation afin de préserver la durée de vie des callbacks hôtes.

## Entrées de tâche callable

Après l'import de `concurrent.vs`, une fonction de script résolue, une méthode statique ou une valeur de fonction (y compris une lambda) peut être passée directement à `std::concurrent::spawn(callable, arguments...)` ou `std::concurrent::spawn_task<T>(callable, arguments...)`. Les arguments peuvent avoir des types différents et sont évalués une fois, de gauche à droite. `std::concurrent::spawn` exige un retour `void` ; `std::concurrent::spawn_task<T>` exige un résultat transférable correspondant exactement à `T`. Les entrées par nom de chaîne restent disponibles.

```VeroScript
import "concurrent.vs";

int twice(int value)
{
	return value * 2;
}

int main()
{
	std::task<int> work = std::concurrent::spawn_task<int>(twice, 21);
	int factor = 3;
	std::task<int> captured = std::concurrent::spawn_task<int>(
		[factor](int value) -> int => value * factor, 7);
	print(work.await());
	print(captured.await());
	return 0;
}
```

Les arguments et captures par valeur ne peuvent contenir que des scalaires, chaînes ou poignées de canal transférables. Les captures par référence ou de `this`, méthodes d'instance, objets mutables, tableaux, paramètres `ref` et retours par référence ne franchissent pas la frontière du worker. Le worker utilise une VM isolée et un instantané des entrées ; `await()` bloque toujours le thread courant. L'annulation est coopérative et le worker peut consulter `std::concurrent::cancel_requested()`. Les AOT C++, Python et C# rejettent actuellement les entrées callable à la compilation. Voir l'[exemple de concurrence](../../../../Scripts/examples/concurrent_demo.vs).

## Capacités de synchronisation

`task_group<T>` regroupe des tâches homogènes et fournit `add`, `await_all` et
`cancel_all`. Les helpers équivalents sont `std::concurrent::when_all` et
`std::concurrent::cancel_all`.

`mutex` et `counting_semaphore` sont des handles de canaux synchronisés et
peuvent être transmis par un canal imbriqué. `std::concurrent::make_mutex`, `std::concurrent::mutex_lock`,
`std::concurrent::mutex_try_lock`, `std::concurrent::mutex_lock_for`, `std::concurrent::mutex_unlock`, `std::concurrent::make_semaphore`,
`std::concurrent::semaphore_acquire`, `std::concurrent::semaphore_try_acquire`, `std::concurrent::semaphore_acquire_for` et
`std::concurrent::semaphore_release` contrôlent ces primitives. Un double déverrouillage ou un
dépassement de la capacité du sémaphore est une erreur runtime.

`std::concurrent::make_synchronized<T>(initial)` crée une cellule de canal de capacité un. Les
opérations `std::concurrent::synchronized_load`, `std::concurrent::synchronized_store`, `std::concurrent::synchronized_exchange`
et `std::concurrent::synchronized_fetch_add` sérialisent l’accès ; elles ne promettent pas les
garanties lock-free d’un `atomic<T>` matériel.

## Demande d'annulation

`std::concurrent::cancel_task(int id)` demande l'annulation coopérative d'un identifiant valide sans le consommer ; `std::concurrent::join_task(id)` peut encore attendre la fin. Un identifiant invalide produit une erreur. `std::concurrent::cancel_requested()` vaut `true` dans le worker concerné après la demande et `false` hors d'un worker. Le worker doit le vérifier entre des attentes bornées ; la demande n'interrompt pas une opération bloquée et ne termine pas le thread. `task<T>.cancel()` ferme aussi son canal de résultat.

`pending(ch)`, `capacity(ch)` et `task_group<T>.size()` renvoient un `int64` non négatif. `std::concurrent::make_channel<T>` accepte une capacité `int64` : `0` indique un canal non borné, une valeur positive un canal borné. Les capacités négatives ou non représentables sont des erreurs ; les limites d’allocation de l’hôte restent applicables. Les identifiants de tâche gardent le type `int`.

- [Fondations de la bibliothèque standard](foundation.md)

<a id="concurrency-aot-boundary"></a>

Limite de l’AOT autonome : task<T>, task_group, when_all et mutex/semaphore/synchronized exigent désormais les contrôles Core d’identité, de propriétaire et de consommation, y compris spawn_task avec un nom textuel. L’AOT autonome C++/Python/C# les refuse avant toute sortie ; utilisez Core VM/Mixed/JIT. Les chemins channel ordinaires, spawn textuel et join restent disponibles en C++/Python.

À la fin de la VM Core propriétaire, les attentes sur les canaux Core des workers possédés sont interrompues avant leur jonction. La fermeture existante des canaux transmis directement reste applicable ; la notification ne ferme pas les autres canaux. L’annulation explicite reste coopérative, les workers détachés restent indépendants et les callbacks hôtes bloquants nécessitent la coopération de l’hôte.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/concurrent.md) · [fr-FR](concurrent.md) · [ja-JP](../../ja-JP/standard-library/concurrent.md) · [ko-KR](../../ko-KR/standard-library/concurrent.md) · [zh-CN](../../zh-CN/standard-library/concurrent.md) · [zh-TW](../../zh-TW/standard-library/concurrent.md)
