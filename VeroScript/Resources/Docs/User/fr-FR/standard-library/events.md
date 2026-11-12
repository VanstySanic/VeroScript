# Module événements

Fichier du module : `delegate.vs`

Importez-le avec :

```VeroScript
import "delegate.vs";
```

Utilisez `std::delegate` et `std::multicast_delegate` dans les scripts publics. Les noms non qualifiés désignent les mêmes types ; les deux formes nécessitent `delegate.vs`. Tous les membres utilisent des noms en minuscules.

## Délégués Unicast

`delegate` stocke un gestionnaire avec jusqu'à trois arguments. Son premier paramètre de type est le type de retour, suivi des types d'argument d'appel :

```VeroScript
std::delegate<int, int> mitigate = std::delegate<int, int>();
mitigate.bind(function(int value) -> int => value / 2);
int result = mitigate.execute(10);
```

Le module fournit `delegate<R>`, `delegate<R, A>`, `delegate<R, A, B>` et `delegate<R, A, B, C>`. Les membres comprennent :

| Membre | Descriptif |
| --- | --- |
| `bind(handler)` | Lie ou remplace le gestionnaire. |
| `unbind()` | Supprime le gestionnaire actuel. |
| `is_bound()` | Teste si un gestionnaire est lié. |
| `execute(...)` | Exécute le gestionnaire ; affirme lorsqu’il n’est pas lié. |
| `execute_if_bound(...)` | S'exécute uniquement lorsqu'il est lié et renvoie s'il a été exécuté. |

`execute_if_bound` indique si l'exécution a eu lieu ; il ne renvoie pas le résultat du gestionnaire.

Le stockage du gestionnaire de `delegate` est privé. `unbind()` efface la référence au gestionnaire conservée par le délégué ; utilisez `is_bound()` pour consulter l’état.

## Délégués multidiffusion

`multicast_delegate` stocke une liste de gestionnaires `void` et prend en charge zéro à trois arguments :

```VeroScript
std::multicast_delegate<int> on_score = std::multicast_delegate<int>();
on_score.add(function(int value) -> void => print(value));
on_score.broadcast(42);
```

Les membres comprennent :

| Membre | Descriptif |
| --- | --- |
| `add(handler)` | Ajoute un gestionnaire. |
| `remove(handler)` | Supprime un gestionnaire correspondant. |
| `broadcast(...)` | Appelle tous les gestionnaires dans l’ordre d’enregistrement. |
| `count()` | Renvoie le nombre de gestionnaires comme `int64` non négatif. |
| `clear()` | Supprime tous les gestionnaires. |

La diffusion prend d'abord un instantané de la liste des gestionnaires, de sorte que les modifications apportées au cours d'une diffusion ne modifient pas l'appel défini pour cette diffusion.

Le champ `handlers` de `multicast_delegate` est public dans le module source. Préférez `add`, `remove` et `clear` lorsque l’ordre d’enregistrement et la sémantique des instantanés doivent être préservés.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/events.md) · [fr-FR](events.md) · [ja-JP](../../ja-JP/standard-library/events.md) · [ko-KR](../../ko-KR/standard-library/events.md) · [zh-CN](../../zh-CN/standard-library/events.md) · [zh-TW](../../zh-TW/standard-library/events.md)
