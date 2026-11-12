# Opérateurs

Les opérateurs calculent, comparent, combinent ou modifient les valeurs d'expression. VeroScript prend en charge les opérateurs arithmétiques, de comparaison, logiques, au niveau du bit, d'affectation, d'indexation, d'accès aux membres et conditionnels.

## Catégories d'opérateurs

| Catégorie | Opérateurs |
| --- | --- |
| Arithmétique | `+`, `-`, `*`, `/`, `%` |
| Comparaison | `==`, `!=`, `<`, `>`, `<=`, `>=` |
| Logique | `!`, `&&`, `||` |
| Au niveau du bit | `&`, `|`, `^`, `<<`, `>>` |
| Affectation | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` |
| Incrémenter/décrémenter | `++`, `--` |
| Conditionnel | `condition ? when_true : when_false` |
| Accès | `.`, `::`, `[]`, `()` |

## Priorité et associativité

Les niveaux suivants vont de la priorité la plus faible à la plus forte :

| Niveau | Opérateurs | Associativité |
| --- | --- | --- |
| Affectation | `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` | droite |
| Conditionnel | `?:` | droite |
| OU logique | `||` | gauche |
| ET logique | `&&` | gauche |
| OU binaire | `|` | gauche |
| XOR binaire | `^` | gauche |
| ET binaire | `&` | gauche |
| Comparaison | `==`, `!=`, `<`, `>`, `<=`, `>=` | gauche |
| Décalage | `<<`, `>>` | gauche |
| Addition | `+`, `-` | gauche |
| Multiplication | `*`, `/`, `%` | gauche |
| Préfixe | `!`, `~`, `+`/`-` unaires, `++`/`--` préfixes, `new`, `delete`, conversions et `ref` | droite |
| Postfixe/accès | appels `()`, indexation `[]`, `.`, `::`, `++`/`--` postfixes | gauche |

`&&` et `||` évaluent d'abord leur opérande gauche et court-circuitent. `?:` évalue d'abord sa condition puis exactement une branche. Hors de ces garanties, si les effets de bord comptent, ne dépendez pas de l'ordre relatif des opérandes ou arguments : utilisez des instructions séparées. Une surcharge ne change jamais la priorité ni l'associativité. Il n'existe pas d'opérateur virgule général ; les listes d'expressions séparées par des virgules sont limitées aux clauses d'initialisation et d'itération d'un `for` classique.

Utilisez des parenthèses pour rendre explicite l'ordre de calcul :

```VeroScript
int result = (a + b) * c;
bool valid = (value >= minimum) && (value <= maximum);
```

## Opérateurs arithmétiques

`+`, `-`, `*`, `/` et `%` effectuent des opérations numériques. `+` peut également concaténer des valeurs du même type de texte :

```VeroScript
int total = 10 + 2;
int remainder = total % 3;
string message = "value: " + to_string(total);
```

`string` et `wstring` ne peuvent pas être concaténés directement entre eux.

## Comparaison et opérateurs logiques

Les opérateurs de comparaison produisent des résultats booléens. Les opérateurs logiques combinent des expressions booléennes :

```VeroScript
bool in_range = value >= 0 && value <= 100;
bool invalid = value < 0 || value > 100;
bool enabled = !disabled;
```

## Opérateurs d'affectation

`=` effectue une affectation ordinaire. L'affectation composée combine une opération avec une affectation :

```VeroScript
value = 10;
value += 2;
value *= 3;
```

Les deux côtés d'un devoir doivent satisfaire aux règles de conversion de la langue. L'affectation ne modifie pas le type statique de la variable de gauche.

## Opérateurs d'accès

L'opérateur d'indexation `[]` accède à un élément de tableau ou de conteneur. L'opérateur d'accès aux membres `.` accède à un objet membre :

```VeroScript
int first = values[0];
point.x = 10;
```

Les espaces de noms et les membres de type peuvent également utiliser `::` :

```VeroScript
Color color = Color::Red;
```

Utilisez `.` pour l’accès aux membres d’une valeur et préférez `::` pour les espaces de noms, les types, les membres d’énumération et les membres statiques. Un `::` initial commence la recherche dans la portée globale et ignore l’espace de noms courant, les alias locaux et le repli de `using namespace`.

## Surcharge des opérateurs

Les types définis par l'utilisateur peuvent déclarer des méthodes d'opérateur afin que le type participe aux opérations sélectionnées. Une surcharge doit satisfaire aux règles de paramètre et de type de retour du compilateur ; ne surchargez pas un opérateur avec une signification surprenante pour le type.

Un opérateur membre utilise l’objet actuel comme opérande gauche implicite. Une fonction opérateur non membre peut déclarer explicitement ses opérandes :

```VeroScript
class Vec
{
public:
	int x;

	Vec(int value)
	{
		x = value;
	}

	Vec operator+(Vec other)
	{
		return Vec(x + other.x);
	}

	void operator+=(Vec other)
	{
		x = x + other.x;
	}

	bool operator==(Vec other)
	{
		return x == other.x;
	}

	int operator[](int index)
	{
		return x + index;
	}
};

Vec operator+(int value, Vec vector)
{
	return Vec(value + vector.x);
}

Vec sum = Vec(3) + Vec(4);
Vec shifted = 1 + Vec(2);
int item = sum[0];
```

Les surcharges d'opérateurs peuvent couvrir les opérations binaires et unaires, l'affectation composée, l'indexation et la syntaxe des appels de fonction. Un type peut également déclarer un opérateur de conversion tel que `bool operator bool()` afin qu'un objet puisse être converti explicitement en valeur booléenne :

```VeroScript
class Token
{
public:
	int value = 42;
	bool operator bool()
	{
		return value != 0;
	}
};

int main()
{
	Token token = Token();
	bool available = token;
	print(available);
	return 0;
}
```


Les opérateurs booléens intégrés `&&` et `||` restent toujours court-circuités ; les surcharges définies par l'utilisateur ne les remplacent pas. Un candidat d'opérateur défini par l'utilisateur n'est pris en compte que si au moins un opérande est un type utilisateur ou hôte pouvant être surchargé ; un opérateur qui nécessiterait de convertir deux opérandes primitifs n'est donc pas sélectionné.

Certains formulaires de comparaison peuvent obtenir un comportement en miroir à partir d'un opérateur de base déclaré, par exemple en utilisant `operator<` pour prendre en charge le sens inverse. Préférez déclarer des opérateurs qui expriment directement la sémantique du type et gardez les opérateurs surchargés cohérents avec leur signification familière.

## Références dans les surcharges

Les opérandes lient implicitement les paramètres de référence, comme en C++ : `a + b`, `a[i]` et `f(x)` ne nécessitent ni `&` ni `ref`. Les appels ordinaires de fonctions et de méthodes conservent leurs marqueurs explicites. `T&` exige un stockage modifiable du même type canonique, après résolution des alias ; les constantes, temporaires et conversions numériques sont refusés. `const T&` accepte un stockage en lecture seule et les temporaires issus de conversions.

À conversions égales, une lvalue modifiable préfère `T&` à `const T&`. Les surcharges par valeur et par référence aux conversions indiscernables sont ambiguës et refusées.

Un résultat `T&` peut être une cible d'affectation ou la source d'un alias local ; un résultat `const T&` est en lecture seule. La qualification const du récepteur et les droits d'accès s'appliquent. Les contrôles de durée de vie restent actifs ; `return this` référence le handle de l'objet courant.

La forme membre préfixée de ++/-- ne prend aucun paramètre explicite ; la forme postfixée prend un marqueur `int` par valeur. Les formes libres prennent l'opérande, puis le marqueur pour la forme postfixée. La surcharge détermine le résultat : un retour `void` ne constitue pas une valeur objet et un résultat non void n'est pas automatiquement réaffecté à la cible.

`operator()` accepte des arguments par défaut et les paramètres variadiques typés du langage. Les autres opérateurs les refusent. `operator=` est membre et prend un paramètre ; la forme libre est refusée. L'initialisation dans une déclaration conserve son comportement existant.

Seuls `operator()` et `operator[]` peuvent être déclarés `static`. Les opérateurs hérités utilisent les arguments génériques de leur type déclarant. Les champs statiques et les variables globales d’espace de noms peuvent se lier aux paramètres par référence ; un champ de structure temporaire ne peut pas se lier à une référence modifiable.

Les formes libres `operator[]`/`operator()`, les comparaisons miroir et les affectations composées synthétisées restent des extensions VeroScript. Les handles de classe et les valeurs struct conservent leur sémantique ; les pointeurs, références rvalue et déplacements C++ ne sont pas intégralement pris en charge.

Ces exemples ciblent les moteurs VM, mixed et JIT. Les exportateurs AOT de code source ont des limites distinctes : les nouvelles combinaisons de références, affectations surchargées, marqueurs postfixés et arguments par défaut/variadiques de l’opérateur d’appel sont explicitement refusés.

```VeroScript
class Cell
{
public:
	int value = 0;
	int& operator[](int index)
	{
		return value;
	}
	const int& operator[](int index) const { return value; }
	void operator()(int& target, const int& delta = 1) const { target += delta; }
	Cell& operator=(const int& next)
	{
		value = next;
		return this;
	}
	Cell& operator++()
	{
		value++;
		return this;
	}
	Cell operator++(int tag)
	{
		Cell old = Cell();
		old.value = value;
		value++;
		return old;
	}
};

void main()
{
	Cell cell = Cell();
	cell[0] = 3;
	cell[0] += 2;
	int number = 4;
	cell(number);             // number == 5
	cell(number, 2);          // number == 7
	print((cell++).value);    // 5
	print((++cell).value);    // 7
	(cell = 9)[0] = 10;
	const Cell view = cell;
	print(view[0]);           // 10
}
```

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/operators.md) · [fr-FR](operators.md) · [ja-JP](../../ja-JP/language-basics/operators.md) · [ko-KR](../../ko-KR/language-basics/operators.md) · [zh-CN](../../zh-CN/language-basics/operators.md) · [zh-TW](../../zh-TW/language-basics/operators.md)
