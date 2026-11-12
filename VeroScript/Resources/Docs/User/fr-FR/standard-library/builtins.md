# Fonctions intégrées

Voir [Fondations de la bibliothèque standard](foundation.md#foundation-imports).

La frontière entre les modules intégrés et les [modules de bibliothèque standard](index.md) est intentionnelle : utilisez les modules intégrés pour les plus petites primitives d'exécution et préférez les modules correspondants pour les fonctionnalités de chaîne, de fichier, de chemin, de concurrence et d'événement de niveau supérieur.

## Sortie

| Fonction | Descriptif |
| --- | --- |
| `print(value)` | Écrit une valeur sur la sortie standard. Avec une chaîne de format et des arguments supplémentaires, il utilise le formatage par accolades ; `{{` et `}}` émettent des accolades littérales. |
| `printf(format, ...)` | Écrit la sortie formatée avec des conversions de style printf ; sa syntaxe de format est différente de `print`. |
| `eprint(value)` | Écrit une valeur dans le flux d'erreurs. |

```VeroScript
int main()
{
	int score = 97;
	print("score={}", score);
	printf("score=%03d", score);
	eprint("diagnostic");
	return 0;
}
```

Utilisez les fonctions `format` ou `formatf` de la bibliothèque standard lorsque le résultat formaté doit être stocké dans une variable. Consultez [Comportement du langage de chaîne](../language-basics/strings.md#formatage) et le [module de chaîne](string.md) pour les champs, les conversions, la largeur, la précision et les limites d'erreur.

## Flux de console

Les fonctions `std::console` sont fournies par le runtime et ne nécessitent
aucune importation. Elles sont limitées à cet espace de noms afin de rendre
explicites leurs sémantiques de flux et de terminal interactif :

| Fonction | Descriptif |
| --- | --- |
| `std::console::write(value)` | Écrit une valeur sur la sortie standard sans ajouter de nouvelle ligne. |
| `std::console::error(value)` | Écrit une valeur dans le flux d'erreurs sans ajouter de nouvelle ligne. |
| `std::console::flush()` | Vide la sortie standard en attente. |
| `std::console::clear()` | Efface la sortie interactive lorsque l'hôte le permet ; renvoie si l'opération a été effectuée. |
| `std::console::is_interactive()` | Indique si l'entrée et la sortie standard sont interactives. |

Utilisez `std::console::flush()` lorsqu'un fragment de progression doit être
visible avant la prochaine nouvelle ligne ou avant de rendre la main à un outil
hôte. Les adaptateurs de débogage transmettent séparément la sortie standard
et la sortie d'erreur vidées.

### Contrôle du terminal

| API | |
| --- | --- |
| `supports_color()`, `supports_cursor()`, `supports_clear()`, `is_output_terminal()` | Capacités distinctes : couleurs, curseur, effacement et sortie terminal. |
| `set_color(string foreground, string background)`, `reset_style()` | Définir les couleurs de texte/fond ; réinitialiser le style. |
| `clear_line(string mode)` | Effacer après/avant le curseur ou toute la ligne, cellule courante comprise, sans déplacer le curseur. |
| `move_cursor(int column, int row)`, `set_cursor_visible(bool visible)` | Position colonne/ligne à partir de zéro ; afficher/masquer le curseur. |
| `width()`, `height()` | Colonnes/lignes visibles ; 0 si inconnues. |

Les contrôles renvoient `bool`, avec `false` si non disponibles ou en échec. Une sortie redirigée ne reçoit aucune séquence de contrôle automatique. Une entrée interactive n’est pas nécessaire. `clear()` efface la fenêtre visible et revient à son origine sans effacer l’historique. Les coordonnées sont des entiers dans `0..32766` ; hors fenêtre connue, le résultat vaut `false`. Types, couleurs et modes invalides produisent une erreur. Les modifications exigent la permission de sortie console. Couleurs : `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`, variantes `bright_` et `default`.

Le style et la visibilité sont restaurés au mieux en fin d’exécution, même après erreur ou annulation. POSIX/hôtes personnalisés restaurent le style par défaut et un curseur visible ; la VM et les trois AOT Windows restaurent l’état natif initial. Les hôtes intégrés restent sans contrôles par défaut. Les interfaces C++, C ABI, C# et Python proposent des callbacks de requête/contrôle et de texte exact sans saut de ligne. L’hôte synchronise ses callbacks de travail. Mobile/navigateur restent en texte brut. Exemple : `console_demo.vs`.

```VeroScript
if (std::console::supports_color())
{
	std::console::set_color("bright_green", "default");
}
std::console::write("Ready\n");
std::console::flush();
std::console::reset_style();
```

## Conversions

| Fonction | Descriptif |
| --- | --- |
| `to_int(value)` | Convertit en `int`. Les valeurs à virgule flottante sont tronquées vers zéro ; les valeurs hors limites et non finies sont des erreurs. |
| `to_double(value)` | Convertit en `double`. |
| `to_float(value)` | Convertit en `float` ; le résultat est stocké au format binaire32. |
| `to_unsigned(value)` | Convertit en `unsigned int`, rejetant les valeurs négatives, hors plage et non valides. Les valeurs à virgule flottante sont tronquées vers zéro. |
| `to_int64(value)` | Convertit en `int64` avec vérification de plage. |
| `to_uint64(value)` | Convertit en `uint64` avec vérification de plage. |
| `to_string(value)` | Convertit une valeur en chaîne. |
| `to_wstring(value)` | Convertit explicitement une valeur `string` ou `wstring` en une chaîne large. |

```VeroScript
int main()
{
	int count = to_int(12.75);
	double ratio = to_double(3);
	unsigned int flags = to_unsigned(true);
	string text = to_string(count);
	wstring wide = to_wstring(text);
	print("count={}, ratio={}, flags={}", count, ratio, flags);
	print(wide);
	return 0;
}
```

Les fonctions de conversion n'analysent pas le texte arbitraire sous forme de nombres. Utilisez `std::io::read_int` ou `std::io::read_double` pour la saisie numérique ; pour le texte d'un fichier ou d'un protocole, définissez les règles d'analyse explicitement dans le script.

## Fonctions numériques et collections

| Fonction | Descriptif |
| --- | --- |
| `abs(value)` | Renvoie la valeur absolue d'une valeur numérique. |
| `min(a, b)` | Renvoie la plus petite de deux valeurs numériques. |
| `max(a, b)` | Renvoie la plus grande de deux valeurs numériques. |
| `len(value)` | Renvoie le nombre d'éléments d'une chaîne, d'un tableau ou d'un conteneur standard. Pour un multiset, il renvoie le nombre total d'occurrences. |
| `range(int64 count)` | Accepte un nombre `int64` et renvoie un `array<int>` contenant `0` à `count - 1`. Un nombre négatif produit un tableau vide ; une valeur supérieure à `1000000` provoque une erreur d’exécution. |

```VeroScript
int main()
{
	array<int> values = range(4);
	print("size={}", len(values));
	print("low={}, high={}, distance={}", min(-2, 5), max(-2, 5), abs(-9));
	return 0;
}
```

`len(value)` renvoie un `int64` non négatif (`0` à `9223372036854775807`). Les valeurs nulles, types non pris en charge et longueurs non représentables provoquent une erreur d’exécution. Les requêtes existantes de taille, de capacité et de nombre d’éléments des conteneurs renvoient aussi `int64`. Utilisez `int64` pour les longueurs et indices linéaires. Une affectation à `int` effectue la conversion numérique vérifiée habituelle et échoue si la valeur ne tient pas ; un type plus large ne supprime pas les limites d’allocation natives ou des conteneurs du backend.

## Environnement, temps et flux de contrôle

| Fonction | Descriptif |
| --- | --- |
| `std::environment::getenv(name)` | Lit une variable d'environnement de processus hôte ; une variable manquante renvoie une chaîne vide. Le nom ne doit pas être vide. |
| `std::environment::setenv(name, value)` | setenv(name, "") conserve une valeur vide existante ; unsetenv supprime, même si la variable est absente. try_getenv distingue absence et chaîne vide. Nom non vide, sans = ni NUL ; valeur sans NUL. La casse des noms suit Windows ou POSIX. |
| `std::chrono::now()` | Renvoie le nombre de secondes, sous la forme `double`, depuis l'époque Unix en fonction de l'horloge système. |
| `std::chrono::steady_now()` | Renvoie une valeur d'horloge monotone en secondes. Utilisez les différences entre deux lectures pour mesurer une durée ; ce n'est pas un horodatage civil. |
| `std::this_thread::sleep(milliseconds)` | Suspend le thread de script en cours pendant le nombre de millisecondes spécifié. L'argument doit être non négatif. |
| `assert(condition[, message])` | Exige une condition `bool` et un message `string` facultatif. Une condition `false` signale une erreur d'exécution ; une condition `true` ne produit aucune sortie. |
| `exit(code)` | Demande la fin du script avec le code de sortie entier spécifié. |

```VeroScript
int main()
{
	std::environment::setenv("VERO_MODE", "docs");
	print(std::environment::getenv("VERO_MODE"));

	double started = std::chrono::steady_now();
	std::this_thread::sleep(1);
	assert(std::chrono::steady_now() >= started);
	exit(0);
	return 0;
}
```

`assert` accepte un ou deux arguments. Le premier doit être de type `bool` et, s'il est présent, le second doit être de type `string`. En cas d'échec, un message non vide est inclus dans le diagnostic ; un message vide utilise le diagnostic d'assertion par défaut. L'échec est une erreur d'exécution et non une exception de script : il n'est donc intercepté ni par `catch (...)` ni par un gestionnaire typé.

Les variables d'environnement appartiennent au processus hôte. Le fait qu'un changement affecte ultérieurement la logique de l'hôte dépend de la plate-forme hôte et du mode d'intégration.

## Entrée standard

| Fonction | Descriptif |
| --- | --- |
| `std::io::read_token()` | Lit le prochain jeton délimité par des espaces à partir de l'entrée standard. |
| `std::io::read_string()` | Lit le jeton de chaîne suivant. |
| `std::io::read_int()` | Lit et analyse le prochain jeton entier. |
| `std::io::read_double()` | Lit et analyse le prochain jeton à virgule flottante. |
| `std::io::read_bool()` | Lit et analyse `true` ou `false`. |

La fin de la saisie ou un jeton invalide produit un diagnostic. Ces fonctions sont destinées à une simple saisie en ligne de commande ; les protocoles d'entrée plus complexes appartiennent à l'hôte ou aux API de bibliothèque standard orientées fichier.

## Noms d'implémentation uniquement

Les noms commençant par `__` (par exemple, `__read_file` et `__format`) sont réservés au compilateur, au runtime et à l’implémentation de la bibliothèque standard. Les modules de script ordinaires ne peuvent pas les appeler ; utilisez les fonctions publiques répertoriées dans la [référence de la bibliothèque standard](index.md).

- [Fondations de la bibliothèque standard](foundation.md)

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/builtins.md) · [fr-FR](builtins.md) · [ja-JP](../../ja-JP/standard-library/builtins.md) · [ko-KR](../../ko-KR/standard-library/builtins.md) · [zh-CN](../../zh-CN/standard-library/builtins.md) · [zh-TW](../../zh-TW/standard-library/builtins.md)
