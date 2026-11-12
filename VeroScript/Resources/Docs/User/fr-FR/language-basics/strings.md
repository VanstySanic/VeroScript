# Chaînes

VeroScript propose deux types de texte : `string` et `wstring`. Ils représentent tous deux des valeurs de texte, mais ce sont des types distincts et ne peuvent pas être mélangés dans une affectation, une comparaison ou une concaténation sans une conversion explicite. Cette page décrit le comportement des chaînes et les assistants courants dans `string.vs` ; la syntaxe littérale est couverte dans [Literals](literals.md).

## Types de texte

Les littéraux de chaîne ordinaires sont de type `string`, tandis que les littéraux de chaîne larges sont de type `wstring` :

```VeroScript
string name = "VeroScript";
wstring title = L"Language reference";
```

`string` et `wstring` prennent en charge `len`, la comparaison d'égalité et la concaténation avec des valeurs du même type de texte. Les opérandes d'une concaténation doivent appartenir au même type de texte :

```VeroScript
string message = "Hello, " + name;
wstring heading = L"Vero" + L"Script";
```

Utilisez `to_wstring` ou `to_string` pour une conversion explicite entre les deux types de texte :

```VeroScript
wstring wide_name = to_wstring(name);
string narrow_name = to_string(wide_name);
```

Utilisez également `to_string` lors de la conversion de valeurs numériques ou booléennes en texte. Cela maintient la conversion explicite au lieu de s'appuyer sur une concaténation implicite de types mixtes :

```VeroScript
string summary = "count=" + to_string(3);
```

## Bibliothèque standard de chaînes

Les assistants de chaîne sont fournis par `string.vs`. Importez le module avant de les utiliser :

```VeroScript
import "string.vs";
```

Les fonctions communes incluent :

| Fonction | Descriptif |
| --- | --- |
| `std::strings::substring(s, start, count)` | Extrait un nombre spécifié de caractères commençant à `start`. |
| `std::strings::find(s, needle)` | Trouve une sous-chaîne et renvoie sa position ; renvoie une valeur négative lorsqu'il n'est pas trouvé. |
| `std::strings::char_code(s, index)` | Renvoie le code de caractère à un index. |
| `std::strings::contains(s, sub)` | Teste si une sous-chaîne est présente. |
| `std::strings::starts_with(s, prefix)` | Teste si une chaîne commence par un préfixe. |
| `std::strings::ends_with(s, suffix)` | Teste si une chaîne se termine par un suffixe. |
| `std::strings::trim_left(s)` / `std::strings::trim_right(s)` | Supprime les espaces du côté gauche ou droit. |
| `std::strings::trim(s)` | Supprime les espaces des deux côtés. |
| `std::strings::split(s, separator)` | Divise une chaîne en `array<string>`. |
| `std::strings::join(parts, separator)` | Joint un tableau de chaînes à l'aide d'un séparateur. |
| `std::strings::to_lower(s)` / `std::strings::to_upper(s)` | Convertit le texte en minuscules ou en majuscules. |
| `std::strings::replace(s, from, to)` | Remplace les sous-chaînes correspondantes par le texte spécifié. |
| `std::strings::last_find(s, needle)` | Renvoie le dernier décalage en octets correspondant, ou `-1` s'il est absent. |
| `std::strings::repeat(s, count)` | Répète `s` ; un compte négatif est une erreur d'exécution. |
| `std::strings::remove_prefix(s, prefix)` / `std::strings::remove_suffix(s, suffix)` | Supprime un préfixe ou suffixe correspondant ; sinon renvoie `s`. |

Ces fonctions renvoient de nouvelles valeurs et ne modifient pas la chaîne d'entrée. Par exemple:

```VeroScript
import "string.vs";

string raw = "  red,green,blue  ";
array<string> colors = std::strings::split(std::strings::trim(raw), ",");
string normalized = std::strings::join(colors, " | ");
print(normalized);
```

`trim_left`, `trim_right` et `trim` suppriment les octets d’espacement ASCII `0x09` à `0x0D` et l’espace (`0x20`). Les indices de chaîne sont des décalages d’octets UTF-8 à partir de zéro, pas des indices de points de code Unicode. `len`, `find` et `last_find` renvoient `int64`, et une recherche sans résultat renvoie `-1`. Les positions et nombres d’octets, les indices de `char_code` et les nombres de répétitions utilisent `int64`. Consultez le [module String](../standard-library/string.md) pour les limites et types de retour.

## Formatage

VeroScript propose deux dialectes de formatage indépendants. `format` utilise des champs d'accolade ; `formatf` et `printf` utilisent des conversions de style printf. Ne mélangez pas les deux dialectes dans une seule chaîne de format.

### Formatage des accolades

La signature publique dans `string.vs` est :

```text
string format(string fmt, array<string> args);
```

Le code de script transmet normalement les arguments scalaires directement. Le compilateur convertit `string`, les booléens, les valeurs à virgule flottante et les entiers signés ou non signés en chaînes et les collecte dans le tableau d'arguments :

```VeroScript
import "string.vs";

string message = format("name={}, score={}", "Ada", 97);
print(message);
```

Si un `array<string>` existe déjà, il peut être passé directement comme deuxième argument :

```VeroScript
import "string.vs";

array<string> args = array<string>();
args.push("Ada");
args.push("97");
string message = format("name={}, score={}", args);
```

Les formulaires de terrain sont :

| Formulaire | Signification |
| --- | --- |
| `{}` | Utilise l'argument automatique suivant, commençant à l'index `0`. |
| `{N}` | Utilise un index d'argument explicite de base zéro, tel que `{0}`. |
| `{:spec}` | Utilise l'argument automatique suivant avec une spécification de format. |
| `{N:spec}` | Utilise un index explicite avec une spécification de format. |
| `{{` / `}}` | Émet un `{` / `}` littéral. |

La spécification de format a la forme `[alignment][width][.precision][type]` :

| Partie | Valeurs acceptées | Signification |
| --- | --- | --- |
| Alignement | `<`, `>`, `^` | Alignement à gauche, à droite ou au centre ; l'alignement à droite est la valeur par défaut. |
| Largeur | Entier décimal `0`–`1024` | Remplit un résultat court avec des espaces ; il ne tronque jamais la valeur. |
| Précision | `.decimal-integer`, `0`–`1024` | Analyse une valeur numérique finie et émet une sortie à virgule fixe avec autant de chiffres. |
| Type | `x`, `X` | Convertit un entier décimal en hexadécimal minuscule ou majuscule. |

La conversion `x`/`X` dans `format` analyse un entier décimal signé de 32 bits, puis émet sa représentation hexadécimale de 32 bits. Utilisez `%x` ou `%X` avec `formatf` lorsque la plage complète de 64 bits non signée est requise.

Par exemple:

```VeroScript
import "string.vs";

string right = format("{:>6}", "ok");   // "    ok"
string left = format("{:<6}", "ok");    // "ok    "
string center = format("{:^6}", "ok");  // "  ok  "
string fixed = format("{:.2}", "1.5");  // "1.50"
string hex = format("{:X}", "255");     // "FF"
string repeated = format("{0} / {0}", "same");
string braces = format("{{value}}", array<string>()); // "{value}"
```

L'index automatique avance uniquement lorsqu'un champ automatique est utilisé. Pour éviter des résultats surprenants, utilisez l'indexation automatique ou explicite de manière cohérente dans une chaîne de format. Les arguments manquants, les accolades sans correspondance, les spécifications non valides et les valeurs qui ne peuvent pas être analysées pour une spécification numérique génèrent des erreurs d'exécution. Les arguments supplémentaires qui ne sont pas référencés par un champ sont ignorés.

### formatage printf

La signature publique de `formatf` est :

```text
string formatf(string fmt, array<string> args);
```

Le code de script peut également transmettre directement des arguments scalaires. `printf` utilise la même grammaire mais écrit directement sur la sortie standard. Une conversion a la forme suivante :

```text
%[flags][width][.precision][length]conversion
```

| Partie | Valeurs ou limites acceptées |
| --- | --- |
| Drapeaux | `-` s'aligne à gauche, `+` affiche un signe positif, un espace préfixe les nombres positifs avec un espace, `#` active des préfixes alternatifs et `0` active un remplissage nul. |
| Largeur | Entier décimal `0`–`1024` ; la largeur dynamique `*` n’est pas prise en charge. |
| Précision | `.digits`, `0`–`1024` ; la précision dynamique `.*` n’est pas prise en charge. |
| Longueur | `h`, `hh`, `l`, `ll`, `j`, `z` et `t` sont acceptés pour la compatibilité des sources, mais ne modifient pas la largeur de la valeur VeroScript ou l'ABI. |
| Transformation | `d`, `i`, `u`, `o`, `x`, `X`, `f`, `F`, `e`, `E`, `g`, `G`, `c`, et `s`. |

Les conversions nécessitent ces formes d'argument :

| Transformation | Argument et résultat |
| --- | --- |
| `%d`, `%i` | Entier signé. |
| `%u` | Entier non négatif. |
| `%o` | Entier octal. |
| `%x`, `%X` | Entier hexadécimal minuscule ou majuscule. |
| `%f`, `%F` | Virgule flottante à virgule fixe ; la précision par défaut est `6`. |
| `%e`, `%E` | Notation scientifique ; les variantes majuscules utilisent des lettres majuscules. |
| `%g`, `%G` | Représentation à virgule flottante par défaut ; les variantes majuscules utilisent des lettres majuscules. |
| `%c` | Un entier compris entre `0` et `255`, ou une chaîne d'un octet ; la précision n’est pas valide. |
| `%s` | Une chaîne ; la précision limite le nombre d'octets émis. |
| `%%` | Émet un `%` littéral sans consommer d'argument. |

`%c` et `%s` autorisent uniquement l'indicateur et la largeur `-` ; pour les conversions numériques, la précision a priorité sur le remplissage de zéros entiers. Un `%` de fin, des arguments manquants, des conversions non prises en charge ou des valeurs qui ne peuvent pas être analysées pour la conversion demandée génèrent des erreurs d'exécution. Les conversions à virgule flottante nécessitent des valeurs numériques finies. Les arguments supplémentaires qui ne sont pas consommés par une conversion sont ignorés.

```VeroScript
import "string.vs";

string text = formatf("d=%+06d x=%#08X f=%.2f s=%-5.3s c=%c", -42, 255, 3.14159, "abcdef", 65);
printf("%s%% done", text);
```

### Relation avec les fonctions de sortie

```VeroScript
import "string.vs";

print("name={}, score={}", "Ada", 97); // print's multi-argument form uses braces
printf("score=%03d", 7);                // printf always uses printf syntax
string value = format("score={}", 7);   // use format when a string is needed
```

Avec une valeur, `print` l'écrit directement ; avec une chaîne de format et des arguments supplémentaires, il utilise le formatage par accolades. `format` et `formatf` renvoient des chaînes, tandis que `printf` écrit sur la sortie standard. `format`, `formatf` et `printf` n'acceptent pas les arguments de référence ou les expressions `void` comme arguments de formatage.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/strings.md) · [fr-FR](strings.md) · [ja-JP](../../ja-JP/language-basics/strings.md) · [ko-KR](../../ko-KR/language-basics/strings.md) · [zh-CN](../../zh-CN/language-basics/strings.md) · [zh-TW](../../zh-TW/language-basics/strings.md)
