# Module d'E/S

Fichier du module : `io.vs`

Importez-le avec :

```VeroScript
import "io.vs";
```

## Fichiers et répertoires

| Fonction | Descriptif |
| --- | --- |
| `std::io::read_file(string path)` | Lit un fichier texte complet. |
| `std::io::write_file(string path, string content)` | Écrase un fichier texte. |
| `std::io::append_file(string path, string content)` | Ajoute du texte et crée le fichier lorsqu'il est manquant. |
| `std::io::read_bytes(string path)` | Lit le fichier complet dans un tampon possédé. |
| `std::io::read_bytes(string path, int64 max_bytes)` | Lit avec un maximum explicite ; un dépassement est une erreur. |
| `std::io::write_bytes(string path, region source)` | Écrase avec les octets d’un tampon ou d’une vue. |
| `std::io::append_bytes(string path, region source)` | Ajoute les octets d’un tampon ou d’une vue. |
| `std::filesystem::exists(string path)` | Teste si un chemin existe. |
| `std::filesystem::delete_file(string path)` | Supprime un fichier. |
| `std::filesystem::is_file(string path)` | Vérifie si le chemin désigne un fichier ordinaire. |
| `std::filesystem::is_directory(string path)` | Vérifie si le chemin désigne un répertoire. |
| `std::filesystem::remove_dir(string path)` | Supprime un répertoire vide. |
| `std::filesystem::list_dir(string path)` | Renvoie un tableau de noms d'entrées de répertoire. |
| `std::filesystem::mkdir(string path)` | Crée un répertoire et ses parents. |
| `std::filesystem::cwd()` | Renvoie le répertoire de travail actuel. |
| `std::filesystem::chdir(string path)` | Modifie le répertoire de travail actuel. |

`std::filesystem::exists` ne renvoie `false` que si l’absence du chemin est confirmée ; une erreur d’inspection est signalée. `std::filesystem::delete_file` accepte les fichiers et les liens symboliques, mais refuse les répertoires réels. `std::filesystem::chdir` modifie le répertoire du processus hôte et affecte les autres sessions. Il exige les autorisations d’écriture de l’environnement et de changement du répertoire du processus. Les hôtes C++ utilisent `FScriptCapabilities::bAllowProcessDirectoryChange` ; les hôtes C, C# et Python utilisent le bit `PROCESS_DIRECTORY_CHANGE`.

La portée du système de fichiers accessible dépend des autorisations de l'hôte et du répertoire de travail actuel. Un échec de lecture, d'écriture, de suppression, de création de répertoire ou de changement de répertoire est signalé au moment de l'exécution.

`std::filesystem::is_file` et `std::filesystem::is_directory` suivent les liens symboliques, renvoient `false` si le chemin est absent et exigent la lecture de fichiers. Les autres erreurs sont signalées. `std::filesystem::remove_dir` exige l'autorisation de suppression, renvoie `false` si le chemin est absent et refuse fichiers, répertoires non vides et liens symboliques vers un répertoire. Il n'est pas récursif.

## Entrée standard

| Fonction | Descriptif |
| --- | --- |
| `std::io::read_line()` | Lit une ligne sans sa nouvelle ligne finale ; renvoie une chaîne vide à EOF. |
| `std::io::try_read_line()` | Renvoie un optional vide seulement à EOF normal. |
| `std::io::try_read_token()` | Renvoie le jeton suivant, ou un optional vide à EOF normal. |
| `std::io::pause()` | Attend une ligne d'entrée ; revient immédiatement à EOF dans un hôte non interactif. |
| `std::io::read_token()` | Lit le prochain jeton séparé par des espaces. |
| `std::io::read_string()` | Lit un jeton de chaîne. |
| `std::io::read_int()` | Lit un jeton entier. |
| `std::io::read_double()` | Lit un jeton à virgule flottante. |
| `std::io::read_bool()` | Lit un jeton booléen. |

Les fonctions d'entrée de jeton consomment des valeurs séparées par des espaces. Utilisez `std::io::read_line` lorsque la ligne complète est importante et une fonction `read_*` typée lorsque les valeurs doivent être lues individuellement.


- [Fondations de la bibliothèque standard](foundation.md)

`region` désigne les surcharges buffer, view et readonly_view. Les lectures refusent les répertoires, même vides. Limites binaires, maximum explicite zéro et EOF sont décrits dans [la référence de base](foundation.md#io-contract).

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/io.md) · [fr-FR](io.md) · [ja-JP](../../ja-JP/standard-library/io.md) · [ko-KR](../../ko-KR/standard-library/io.md) · [zh-CN](../../zh-CN/standard-library/io.md) · [zh-TW](../../zh-TW/standard-library/io.md)
