# Types de données

Les types déterminent comment une valeur est représentée, quelles opérations peuvent lui être appliquées et quelles conversions sont autorisées. Les types VeroScript sont regroupés en types intégrés, conteneurs et définis par l'utilisateur.

## Types intégrés

| Catégorie | Types |
| --- | --- |
| Aucune valeur | `void` |
| Booléen | `bool` |
| Entiers signés | `char`, `short`, `int`, `long` |
| Entiers non signés | `unsigned char`, `unsigned short`, `unsigned int`, `unsigned long` |
| Entiers à largeur fixe | `int8`, `uint8`, `int16`, `uint16`, `int32`, `uint32`, `int64`, `uint64` |
| Virgule flottante | `float` (binaire32), `double` (binaire64) |
| Texte | `string`, `wstring` |

`void` ne peut être utilisé que comme type de retour de fonction ; il ne peut pas être utilisé pour les variables. Les entiers à largeur fixe et `float`/`double` sont des types de langage distincts plutôt que des alias de documentation uniquement. Ils sont utiles pour les interfaces et les structures de données qui nécessitent une largeur explicite ou une précision en virgule flottante.

Les plages d'entiers à largeur fixe sont `-128` à `127` pour `int8`, `0` à `255` pour `uint8`, `-32768` à `32767` pour `int16` et `0` à `65535` pour `uint16` ; `int32`, `uint32`, `int64` et `uint64` utilisent les plages 32 bits et 64 bits signées et non signées correspondantes. Un littéral en dehors de sa plage cible est une erreur ; ne comptez pas sur la troncature.

```VeroScript
int8 signed_byte = -128;
uint8 byte = 255u;
float single = 2.05f;
double precise = 2.05;
```

Un littéral à virgule flottante avec un suffixe `f`/`F` est stocké sous `float` ; un littéral à virgule flottante sans suffixe est stocké sous `double`. Leur comportement en matière d'arrondi peut donc différer.

## Types de conteneurs

Les types de conteneurs utilisent des arguments de type pour décrire leurs types d'élément ou de clé/valeur :

```VeroScript
array<int> values = array<int>{1, 2, 3};
map<string, int> counts = map<string, int>();
set<string> names = set<string>();
queue<int> pending = queue<int>();
```

VeroScript fournit ces catégories de conteneurs :

- Conteneurs de séquence : `array`, `deque`.
- Conteneurs associatifs : `map`, `multimap`, `ordered_map`, `ordered_multimap`.
- Ensemble de conteneurs : `set`, `multiset`, `ordered_multiset`.
- Adaptateurs : `queue`, `priority_queue`, `stack`.
- Conteneurs simultanés : `channel`.

Les opérations de conteneur, la sémantique des éléments et les règles de concurrence appartiennent aux références de conteneur et de concurrence.

## Tableaux natifs

Les tableaux natifs de taille fixe mettent leurs dimensions après le type d'élément :

```VeroScript
int numbers[3] = {10, 20, 30};
int matrix[2][3] = {{1, 2, 3}, {4, 5, 6}};
```

`T[N]` inclut sa longueur dans le type, tandis que `array<T>` est un conteneur de tableau dynamique. Ils peuvent tous deux être indexés, mais ils sont de types différents.

## Types définis par l'utilisateur

Les utilisateurs peuvent définir de nouveaux types avec `struct`, `class`, `interface` et `enum` :

```VeroScript
struct Point
{
	int x;
	int y;
};

enum class Color
{
	Red,
	Green,
	Blue
};
```

`struct` a une sémantique de valeur, tandis que `class` a une identité partagée. Voir [Structs](structs.md) et [Classes](classes.md) pour les champs, les méthodes et la sémantique des objets ; voir [Interfaces](interfaces.md) et [Enums](enums.md) pour ces formulaires de type, tandis que les génériques sont traités séparément.

## `auto`

`auto` déduit le type d'une variable à partir de son initialiseur :

```VeroScript
auto count = 3;
auto values = array<int>{1, 2, 3};
```

Une déclaration `auto` doit avoir un initialiseur. L'inférence se produit lors de la déclaration ; la variable a toujours un type statique défini par la suite.

## Conversions de types

Les types numériques compatibles peuvent être convertis selon les règles linguistiques. `string` et `wstring` ne peuvent pas être mélangés implicitement ; utilisez la fonction de conversion explicite appropriée lorsque vous vous déplacez entre les types de texte.

L'autorisation d'une conversion dépend du type source, du type cible et du contexte de l'expression. Les liaisons hôtes et les interfaces génériques doivent utiliser des types explicites au lieu de s'appuyer sur des conversions implicites lorsque cela est possible.

## Wrappers en lecture seule

`readonly<T>` est un wrapper de valeur : il stocke un `T`, peut être copié ou placé dans un conteneur, et expose la valeur via `const ref T get() const`.

```VeroScript
readonly<int> count = readonly<int>(41);
const ref int view = count.get();
print(view);
```

Il n’a pas de constructeur par défaut implicite. Pour une structure, il stocke une copie de valeur ; pour une classe, il conserve le handle et donc l’identité partagée. Consultez [la référence des conversions](../language-reference/type-conversions.md) pour l’échappatoire délibérée `const_cast<T>`.

Les noms de types intégrés `optional`, `readonly`, `weak`, `shared`, `type`, `field_info`, `method_info`, `module_info`, `function_info`, `global_info` et `enum_member_info` sont réservés et ne peuvent pas être redéclarés par un script.

## Types utilitaires fournis par le compilateur

VeroScript fournit également `optional<T>`, `weak<T>`, `pair<T, U>` et `shared<T>` / `handle<T>`. Leurs constructeurs, états, règles de propriété et sémantique des entrées de carte sont documentés dans [Types utilitaires de valeurs](value-utility-types.md).

`pair<T, U>` et `handle<T>` sont des génériques intégrés souples : une déclaration utilisateur de même nom et de même arité générique prend la priorité sur la forme fournie par le compilateur. Cela les distingue des noms strictement réservés ci-dessus.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/data-types.md) · [fr-FR](data-types.md) · [ja-JP](../../ja-JP/language-basics/data-types.md) · [ko-KR](../../ko-KR/language-basics/data-types.md) · [zh-CN](../../zh-CN/language-basics/data-types.md) · [zh-TW](../../zh-TW/language-basics/data-types.md)
