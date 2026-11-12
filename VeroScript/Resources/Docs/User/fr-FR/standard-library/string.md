# Module de chaîne

Fichier du module : `string.vs`

Importez-le avec :

```VeroScript
import "string.vs";
```

## Requêtes et découpage

| Fonction | Descriptif |
| --- | --- |
| `std::strings::substring(string s, int64 start, int64 count)` | Renvoie au plus `count` octets UTF-8 à partir du décalage en octets `start`. |
| `std::strings::find(string haystack, string needle)` | Renvoie le premier décalage UTF-8 correspondant comme `int64`, ou `-1` en cas d’absence. |
| `std::strings::char_code(string s, int64 index)` | Renvoie l’octet à `index` comme `int` (`0`–`255`). |
| `std::strings::contains(string s, string sub)` | Teste si une sous-chaîne est présente. |
| `std::strings::starts_with(string s, string prefix)` | Teste si une chaîne commence par un préfixe. |
| `std::strings::ends_with(string s, string suffix)` | Teste si une chaîne se termine par un suffixe. |

Les indices de chaîne sont des décalages d’octets UTF-8 commençant à `0`, et non
des indices de points de code Unicode. Un index hors plage produit une erreur
d’exécution. `std::strings::find`, `std::strings::last_find`, `std::strings::substring` et `std::strings::char_code` utilisent cette
même convention de décalage en octets.

## Fractionnement, jointure et transformation

| Fonction | Descriptif |
| --- | --- |
| `std::strings::split(string s, string separator)` | Divise une chaîne en `array<string>`. |
| `std::strings::join(array<string> parts, string separator)` | Joint un tableau de chaînes à l'aide d'un séparateur. |
| `std::strings::trim_left(string s)` | Supprime à gauche les espaces ASCII, tabulations, sauts de ligne, tabulations verticales, sauts de page et retours chariot. |
| `std::strings::trim_right(string s)` | Supprime les espaces à droite. |
| `std::strings::trim(string s)` | Supprime les espaces des deux côtés. |
| `std::strings::to_lower(string s)` | Convertit le texte en minuscules. |
| `std::strings::to_upper(string s)` | Convertit le texte en majuscules. |
| `std::strings::replace(string s, string from, string to)` | Remplace les sous-chaînes correspondantes ; renvoie la chaîne d'origine lorsque `from` est vide. |
| `std::strings::last_find(string haystack, string needle)` | Renvoie le dernier décalage en octets trouvé, ou `-1` s'il est absent. Une aiguille vide renvoie `len(haystack)`. |
| `std::strings::repeat(string value, int64 count)` | Répète une chaîne `count` fois. Un compte négatif est une erreur d'exécution. |
| `std::strings::remove_prefix(string value, string prefix)` | Supprime `prefix` s'il est présent ; sinon renvoie la chaîne originale. |
| `std::strings::remove_suffix(string value, string suffix)` | Supprime `suffix` s'il est présent ; sinon renvoie la chaîne originale. |

## Formatage

Ce module fournit deux fonctions de formatage : `format` utilise des champs d'accolades et `formatf` utilise des conversions de style printf. Les deux grammaires sont indépendantes et ne doivent pas être mélangées.

Les signatures publiques sont :

```text
string format(string fmt, array<string> args);
string formatf(string fmt, array<string> args);
```

Le code de script peut transmettre directement `string`, des booléens, des valeurs à virgule flottante et des entiers signés ou non signés ; le compilateur convertit ces scalaires en chaînes et les collecte dans `array<string>`. Un `array<string>` existant peut également être transmis directement comme deuxième argument.

```VeroScript
import "string.vs";

string text = format("value={}, name={}", 42, "VeroScript");
string hex = format("{:8X}", 255);
string legacy = formatf("value=%d", 42);
```

### `format` champs

| Formulaire | Signification |
| --- | --- |
| `{}` | Utilise l'argument automatique suivant. |
| `{N}` | Utilise un index de base zéro explicite. |
| `{:spec}` / `{N:spec}` | Utilise une spécification sous la forme `[alignment][width][.precision][type]`. |
| `{{` / `}}` | Émet des accolades littérales. |

L'alignement est `<`, `>` ou `^`, avec l'alignement à droite par défaut. La largeur et la précision sont limitées à `0`–`1024` et utilisent un remplissage d'espace. Les seuls codes de type sont `x` et `X` pour les caractères hexadécimaux minuscules et majuscules. Precision analyse une valeur numérique finie et émet des chiffres à virgule fixe. Ne mélangez pas les indices automatiques et explicites ; Les arguments manquants, les accolades sans correspondance et les spécifications non valides produisent des erreurs d'exécution.

<a id="integer-formatting"></a>

format x/X couvre désormais 64 bits : jusqu’à UINT64_MAX pour les positifs, INT64_MIN pour les négatifs, ces derniers en complément à deux sur 64 bits. -1 donne 16 f. Les conversions entières printf n’utilisent pas double. Largeur et précision restent limitées à 1024.

### `formatf` conversions

La forme de conversion est `%[flags][width][.precision][length]conversion`. Les indicateurs pris en charge sont `-`, `+`, espace, `#` et `0` ; la largeur et la précision sont limitées à `0`–`1024`, et la dynamique `*` n'est pas prise en charge. Les modificateurs de longueur `h`, `hh`, `l`, `ll`, `j`, `z` et `t` sont acceptés pour la compatibilité des sources, mais ne modifient pas la largeur de la valeur VeroScript ou l'ABI.

Les conversions prises en charge sont :

| Transformation | Signification |
| --- | --- |
| `%d`, `%i` | Entier signé. |
| `%u`, `%o`, `%x`, `%X` | Entier décimal non négatif, octal ou hexadécimal minuscule/majuscule. |
| `%f`, `%F`, `%e`, `%E`, `%g`, `%G` | Virgule flottante ; la précision par défaut est `6`. |
| `%c` | Un entier compris entre `0` et `255` ou une chaîne d'un octet ; la précision n’est pas valide. |
| `%s` | Chaîne; la précision limite les octets émis. |
| `%%` | Littéral `%` sans consommer d’argument. |

`printf` utilise la même grammaire de conversion que `formatf` et écrit directement sur la sortie standard. La forme multi-arguments de `print` utilise la grammaire des accolades de `format`. Les arguments supplémentaires non consommés par une conversion sont ignorés. Pour connaître les règles complètes et les limites d'erreur, consultez [Comportement du langage de chaîne](../language-basics/strings.md#formatage).
`std::strings::to_lower` et `std::strings::to_upper` ne modifient que les lettres ASCII et préservent tous les autres octets UTF-8 ; ils n’effectuent pas de conversion de casse Unicode.
`std::strings::trim_left`, `std::strings::trim_right` et `std::strings::trim` retirent aux extrémités les octets ASCII `0x09` à `0x0D` et l'espace (`0x20`) ; les octets internes et non ASCII sont conservés.

`len(s)`, `std::strings::find` et `std::strings::last_find` renvoient `int64` ; une recherche sans résultat renvoie `-1`. `std::strings::substring` utilise un décalage et un nombre d’octets `int64` : `start` peut être égal à `len(s)`, `count` doit être non négatif et un nombre dépassant les octets restants est limité au suffixe disponible. `std::strings::char_code` accepte un indice d’octet `int64` et renvoie la valeur de l’octet comme `int` (`0`–`255`). `std::strings::repeat` accepte un nombre `int64` et rejette une longueur dépassant la plage `int64` ; les limites d’allocation restent applicables.

- [Fondations de la bibliothèque standard](foundation.md)

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/string.md) · [fr-FR](string.md) · [ja-JP](../../ja-JP/standard-library/string.md) · [ko-KR](../../ko-KR/standard-library/string.md) · [zh-CN](../../zh-CN/standard-library/string.md) · [zh-TW](../../zh-TW/standard-library/string.md)
