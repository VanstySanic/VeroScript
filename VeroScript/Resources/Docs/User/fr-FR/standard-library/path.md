# Module Chemin

Fichier du module : `path.vs`

Importez-le avec :

```VeroScript
import "path.vs";
```

## Fonctions

| Fonction | Descriptif |
| --- | --- |
| `std::filesystem::basename(string path)` | Renvoie le nom après le séparateur de chemin final. |
| `std::filesystem::dirname(string path)` | Renvoie le répertoire avant le séparateur final ; renvoie `"."` lorsqu'il n'y a pas de séparateur. |
| `std::filesystem::join_path(string left, string right)` | Joint deux composants de chemin avec un séparateur. |
| `std::filesystem::extension(string path)` | Renvoie l'extension incluant son point, ou une chaîne vide lorsqu'il n'y en a pas. |
| `std::filesystem::change_extension(string path, string new_ext)` | Remplace ou ajoute une extension. |

Le module de chemin reconnaît `/` et `\` comme séparateurs. `std::filesystem::join_path` gère un séparateur existant à l'extrémité du composant gauche et un composant vide de chaque côté.

`std::filesystem::change_extension` accepte une nouvelle extension avec ou sans point initial ; `"json"` et `".json"` produisent le même résultat. Un point de début dans un nom de fichier n’est pas traité comme une extension en soi.
`std::filesystem::dirname` conserve le séparateur de la racine d’un lecteur Windows : `std::filesystem::dirname("C:\\foo.txt")` renvoie `C:\`.
`std::filesystem::join_path` est purement lexical : un côté vide laisse l'autre inchangé. Un côté droit commençant par `/` ou `\\`, ou par un préfixe de lecteur ASCII comme `C:`, remplace le côté gauche. Sinon, `/` est inséré sauf si le côté gauche se termine déjà par un séparateur ou vaut exactement `C:` (`C:` et `file` donnent `C:file`). Aucun accès disque ni normalisation de `.` ou `..`.


- [Fondations de la bibliothèque standard](foundation.md)

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/path.md) · [fr-FR](path.md) · [ja-JP](../../ja-JP/standard-library/path.md) · [ko-KR](../../ko-KR/standard-library/path.md) · [zh-CN](../../zh-CN/standard-library/path.md) · [zh-TW](../../zh-TW/standard-library/path.md)
