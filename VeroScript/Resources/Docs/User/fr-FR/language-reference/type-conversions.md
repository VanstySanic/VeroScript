# Conversions de types

La conversion de type détermine si une expression peut être utilisée comme valeur d'un autre type. La validité d'une conversion dépend du type source, du type cible et du contexte de l'expression.

## Types numériques

Les types entiers et à virgule flottante compatibles peuvent participer à l'affectation, au passage d'arguments et aux opérations conformément aux règles de conversion numérique du langage. Les entiers à largeur fixe sont utiles aux limites explicites de largeur de données ; lors de la conversion vers un type plus étroit ou non signé, vérifiez que la plage de valeurs ne sera pas tronquée ou modifiée de manière inattendue.

```VeroScript
int count = 10;
double total = count;
int rounded = to_int(total);
```

Utilisez une construction ou un formulaire de conversion de type cible explicite lorsque l'intention est importante. N'utilisez pas la concaténation de chaînes ou la conversion implicite comme substitut à la conversion numérique.

## Contextes de conversion

VeroScript distingue les conversions implicites des conversions qui doivent être demandées explicitement :

| Contexte | Usage | Règles principales |
| --- | --- | --- |
| Affectation, arguments et valeurs de retour | Conversion implicite | Les conversions numériques compatibles et les conversions vers une base ou une interface compatible sont prises en compte. Un constructeur à un seul paramètre de valeur peut aussi participer s’il n’est pas `explicit`. |
| `cast<T>(value)` | Conversion ou construction explicite | Prend en charge les conversions numériques, les énumérations à portée, les références, les interfaces, les constructeurs explicites et les opérateurs de conversion définis par l'utilisateur. Un échec de cast de référence vérifié produit une erreur d’exécution. |
| `try_cast<T>(value)` | Conversion susceptible d’échouer | Renvoie `optional<T>`. Un cast de référence incompatible devient un optional vide et aucun constructeur `explicit` n’est appelé. |
| `const_cast<T>(value)` | Retrait de la capacité d’accès const | La source doit être const et avoir le même type canonique que `T`. Il ne s’agit pas d’une conversion de valeur et aucun contrôle de type à l’exécution n’est effectué. |

Le compilateur n’enchaîne pas les conversions définies par l’utilisateur : il ne convertit pas silencieusement `A` en `B`, puis `B` en `C` simplement parce que ces deux conversions existent.

## Types de texte

`string` et `wstring` sont des types distincts et ne peuvent pas être directement mélangés lors d'une affectation, d'une comparaison ou d'une concaténation. Utilisez les fonctions de conversion explicites de la bibliothèque standard :

```VeroScript
string text = "hello";
wstring wide = to_wstring(text);
string restored = to_string(wide);
```

Utilisez `to_string` lors de la conversion de valeurs numériques ou booléennes en texte au lieu de vous fier à une concaténation implicite de type mixte.

## Constructeurs de conversion et opérateurs de conversion

Un constructeur qui reçoit un seul paramètre de valeur peut participer à une conversion implicite s’il n’est pas marqué `explicit` :

```VeroScript
struct Meter
{
	int value;

	Meter(int input)
	{
		value = input;
	}
};

Meter assigned = 7;
```

Marquez le constructeur `explicit` lorsqu’il ne doit être appelé que par construction directe ou par `cast<T>` :

```VeroScript
struct UserId
{
	int value;

	explicit UserId(int input)
	{
		value = input;
	}
};

UserId direct(7);
UserId converted = cast<UserId>(8);
// UserId invalid = 9; // Compile error.
```

Un opérateur de conversion utilisateur tel que `operator bool()` peut également participer aux conversions autorisées par le langage. Utilisez-les avec mesure afin que les conversions implicites restent prévisibles.
`cast<T>` peut également demander explicitement un opérateur de conversion défini par l'utilisateur correspondant. `try_cast<T>` est limité aux cas documentés concernant les primitives, les énumérations à portée et les références/interfaces ; il n'appelle ni les constructeurs ni les opérateurs de conversion définis par l'utilisateur.

## `cast<T>`

`cast<T>(value)` demande une conversion explicite. Il prend en charge les conversions numériques compatibles, les conversions vers et depuis l’entier sous-jacent d’une énumération à portée, les conversions vérifiées de références et d’interfaces, ainsi que la construction explicite d’une structure ou d’une classe cible.

```VeroScript
double precise = cast<double>(7);
int narrowed = cast<int>(2.9);

enum class Mode
{
	Idle = 3
};

Mode mode = cast<Mode>(3);
int raw_mode = cast<int>(mode);
```

Pour les références de classes et d’interfaces, `cast<T>` vérifie la relation d’exécution lorsqu’un downcast ou un cast transversal est nécessaire. Un échec est une erreur d’exécution. Une conversion structure-interface suit la représentation de valeur de la structure ; une conversion classe-interface conserve l’identité partagée de l’objet.

`cast<T>` ne retire pas l’accès const. Si la source est const, le résultat reste accessible comme const. Utilisez `const_cast<T>` uniquement pour l’échappement const volontaire décrit ci-dessous.

Un cast réussi vers une valeur de structure respecte la sémantique de valeur.

## Syntaxe de conversion entre parenthèses

`(T)expression` est une autre écriture de `cast<T>(expression)`. Elle suit
exactement les mêmes règles de conversion explicite, notamment pour les
conversions numériques, les énumérations à portée, les références/interfaces,
la construction explicite et les opérateurs de conversion utilisateur
correspondants :

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);

int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print("a={}, calls={}", a, calls);
	return 0;
}
```

L'analyseur reconnaît cette forme en position d'expression unaire. Un nom de
type utilisateur simple peut être ambigu avec une expression de valeur ;
préférez donc `cast<T>` lorsque le type cible n'est pas évident.

`(void)expression;` est un abandon explicite du résultat limité aux
instructions. L'expression est évaluée exactement une fois, ses effets de
bord et ses erreurs d'exécution sont conservés, puis son résultat est ignoré.
Cette forme est également acceptée dans l'initialiseur d'une boucle `for`.
Il ne s'agit pas d'une expression de valeur : `(void)expr` ne peut pas être
utilisé comme argument, valeur d'affectation, opérande arithmétique ou
expression imbriquée. Une instruction `expression;` ordinaire a le même
comportement à l'exécution ; le préfixe `(void)` documente l'intention.

## `try_cast<T>`

`try_cast<T>(value)` est la variante tolérante aux échecs pour les conversions primitives, les énumérations à portée et les références/interfaces :

```VeroScript
class Base
{
};

class Derived : public Base
{
};

class Other
{
};

Base base = Derived();
optional<Derived> found = try_cast<Derived>(base);
optional<Other> missing = try_cast<Other>(base);

if (found.has_value())
{
	print("derived");
}

if (!missing.has_value())
{
	print("not compatible");
}
```

`try_cast` renvoie un optional engagé pour une conversion réussie. Une conversion de référence vérifiée qui échoue renvoie un optional vide. Une conversion de référence nulle reste une conversion réussie vers une cible nullable et peut donc produire un optional engagé contenant `nullptr`. Les conversions numériques produisent la valeur convertie ; par exemple, `try_cast<int>(2.9)` contient `2`.

`try_cast<T>` ne sélectionne pas de constructeur `explicit`. Utilisez `cast<T>` lorsque l’opération voulue est une construction explicite.

## `const_cast<T>`

`const_cast<T>(value)` est une échappatoire const de portée étroite. L’expression source doit être const et son type canonique doit correspondre exactement à `T` après résolution des alias :

```VeroScript
class Counter
{
public:
	int value = 41;
};

int main()
{
	readonly<Counter> stored = Counter();
	const_cast<Counter>(stored.get()).value = 42;
	print(stored.get().value);
	return 0;
}
```

Cet exemple modifie un champ de l’objet partagé. `const_cast<int>(view)` produit une valeur scalaire, pas une référence assignable ; `const_cast<int>(view) = 42` est donc invalide. La conversion ne transforme pas une copie scalaire en alias du stockage d’origine.

L’opération retire uniquement la capacité d’accès const à la compilation. Elle ne convertit pas la valeur, ne change pas son type à l’exécution et n’effectue pas de cast vérifié. Réservez-la aux stockages volontairement modifiables, comme un wrapper `readonly<T>` contrôlé. L’appliquer à un stockage véritablement immuable annule la protection que `const` devait fournir.

Les formes suivantes sont rejetées :

```VeroScript
const int number = 1;
// const_cast<double>(number); // The target type must be exactly int.
// const_cast<int>(mutable_value); // The source must be const.
```

## Structures, classes, interfaces et conteneurs

Les conversions de structure suivent la sémantique des valeurs et créent une copie de la valeur de structure. Un champ struct qui est lui-même un handle partagé conserve toujours la sémantique du handle de ce champ. Les conversions de classe préservent l'identité de l'objet partagé :

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Point : IReadable
{
	int value;
	int read() const override
	{
		return value;
	}
};

Point point = Point();
IReadable readable = point;
```

La conversion d'une structure en interface crée la représentation de valeur requise par l'interface ; la conversion d'une classe en interface préserve l'identité partagée de l'objet de classe. Le type concret doit implémenter toutes les méthodes virtuelles pures requises par l'interface.

Les types d’éléments de conteneur, de clés et de valeurs doivent satisfaire aux règles de leurs déclarations. Une fonction ou un type générique ne peut être instancié que lorsque ses arguments satisfont ses contraintes déclarées ; une contrainte `where` n’assouplit pas autrement les règles de conversion.

## Conversions sur lesquelles ne pas se fier

`nullptr` s’applique uniquement aux types de handles nullables. Les types de texte ne peuvent pas être échangés via une affectation ordinaire, et une valeur de structure ne peut pas être échangée avec un descripteur de classe simplement parce que leurs champs se ressemblent. Préférez les types de cibles explicites aux limites de liaison d’hôte et d’interface.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-reference/type-conversions.md) · [fr-FR](type-conversions.md) · [ja-JP](../../ja-JP/language-reference/type-conversions.md) · [ko-KR](../../ko-KR/language-reference/type-conversions.md) · [zh-CN](../../zh-CN/language-reference/type-conversions.md) · [zh-TW](../../zh-TW/language-reference/type-conversions.md)
