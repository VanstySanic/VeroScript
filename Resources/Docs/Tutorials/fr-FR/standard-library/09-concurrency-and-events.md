# 09 : Concurrence et événements

C’est dans l’état partagé que la concurrence devient difficile. Ce didacticiel envoie une valeur via un canal au lieu de partager une variable locale ordinaire, puis utilise un délégué de multidiffusion pour notifier plusieurs gestionnaires.

## Étape 1 : laisser une tâche traiter une valeur

```VeroScript
import "concurrent.vs";

void worker(channel<int> input)
{
	int value = input.recv();
	input.send(value + 1);
}

int twice(int value)
{
	return value * 2;
}

int main()
{
	channel<int> pipe = channel<int>();
	int task_id = std::concurrent::spawn(worker, pipe);

	pipe.send(41);
	std::concurrent::join_task(task_id);

	int result = pipe.recv();
	assert(result == 42);
	task<int> doubled = std::concurrent::spawn_task<int>(twice, 21);
	assert(doubled.await() == 42);
	print(result);
	return 0;
}
```

La tâche reçoit et envoie des valeurs via `channel<int>`. `std::concurrent::spawn` accepte une fonction de script résolue, une méthode statique ou une valeur fonction et renvoie un ID de tâche ; `join_task` attend la fin. `spawn_task<T>` accepte les mêmes formes d'appelable et renvoie un résultat typé via `task<T>.await()`. Les arguments et captures de lambda doivent être transférables, par exemple des scalaires, des chaînes ou des handles de canal ; chaque tâche utilise une VM isolée. `send`, `recv` et `close` sont des fonctions libres visibles ordinaires exposées via la syntaxe uniforme d'appel d'extension de VeroScript. Les variables locales ordinaires ne remplacent pas un canal de communication de tâche. La prise en charge des tâches appelables dépend du backend ; voir la référence Concurrency pour ses limites.

L'annulation est coopérative. Une tâche en cours peut vérifier `std::concurrent::cancel_requested()` après l'appel de `std::concurrent::cancel_task(id)` par une autre partie du programme. La demande n'interrompt pas une opération bloquée et ne force pas l'arrêt de la tâche ; `task<T>.cancel()` ferme aussi le canal de résultat de cette tâche.

## Étape 2 : diffuser un résultat

```VeroScript
import "delegate.vs";

int main()
{
	multicast_delegate<int> on_result = multicast_delegate<int>();
	on_result.add(function(int value) -> void => print("log={}", value));
	on_result.add(function(int value) -> void => print("double={}", value * 2));

	on_result.broadcast(21);
	print("listeners={}", on_result.count());
	return 0;
}
```

Un délégué de multidiffusion stocke plusieurs gestionnaires `void`. `broadcast` les appelle dans leur ordre d'enregistrement actuel, ce qui est utile lorsqu'un résultat doit déclencher plusieurs réponses indépendantes.

## Choisir un mécanisme

- Un appel de fonction : l'appelant connaît la fonction et a besoin d'un résultat immédiat.
- `delegate` : un gestionnaire remplaçable.
- `multicast_delegate` : notification pour plusieurs gestionnaires.
- `channel<T>` : valeurs échangées entre tâches.
- `spawn_task<T>` : une tâche appelable qui produit un résultat typé que vous pouvez attendre.
- `make_synchronized<T>`, mutex et sémaphores : handles synchronisés explicites pour un état transférable partagé.

Faites attention à la commande en attente. Un expéditeur attendant un destinataire tandis que le destinataire attend l'expéditeur est le modèle de blocage le plus courant. Concevez le flux de messages avant d'ajouter `spawn` et `join_task`.

## Exercices

1. Modifiez `worker` pour recevoir deux entiers et renvoyer leur somme.
2. Ajoutez un gestionnaire qui s'imprime uniquement lorsque le résultat est supérieur à `40`.
3. Envoyez une chaîne d'état via `channel<string>` et fermez le canal dans la fonction principale.

Exemples exécutables : [`examples/concurrent_demo.vs`](../../../../../VeroScript/Resources/Scripts/examples/concurrent_demo.vs) et [`examples/delegates.vs`](../../../../../VeroScript/Resources/Scripts/examples/delegates.vs). Voir [Concurrency](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/concurrent.md) et [Events](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/events.md) pour les règles complètes.

Suivant : [Création de paquets et distribution d'une application](../toolchain/10-packages-and-distribution.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/standard-library/09-concurrency-and-events.md) · [fr-FR](09-concurrency-and-events.md) · [ja-JP](../../ja-JP/standard-library/09-concurrency-and-events.md) · [ko-KR](../../ko-KR/standard-library/09-concurrency-and-events.md) · [zh-CN](../../zh-CN/standard-library/09-concurrency-and-events.md) · [zh-TW](../../zh-TW/standard-library/09-concurrency-and-events.md)
