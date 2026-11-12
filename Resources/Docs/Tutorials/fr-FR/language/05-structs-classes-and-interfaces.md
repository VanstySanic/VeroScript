# 05 : Structures, classes et interfaces

Ce didacticiel utilise un petit exemple d'objet lisible pour comparer trois types définis par l'utilisateur : les structures pour les valeurs, les classes pour l'identité partagée et les interfaces pour le comportement remplaçable.

## Étape 1 : représenter une valeur avec une structure

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	int distance_score() const
	{
		return x * x + y * y;
	}
};

int main()
{
	Point point = Point();
	point.x = 3;
	point.y = 4;
	print(point.distance_score());
	return 0;
}
```

Une structure représente généralement un groupe de valeurs. L'affectation d'une structure à une autre variable est normalement comprise comme une copie de valeur, ce qui fonctionne bien pour les coordonnées, les paramètres et les petits enregistrements.

## Étape 2 : représenter l'identité partagée avec une classe

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}

	void add(int amount)
	{
		value = value + amount;
	}
};

int main()
{
	Counter first = Counter(10);
	Counter second = first;
	second.add(5);
	print(first.value);
	return 0;
}
```

Cela imprime `15` car `first` et `second` font référence au même objet de classe. Une affectation de classe copie l'identité de l'objet, pas un objet indépendant. Construisez un autre objet lorsque vous avez besoin d’un état séparé.

Les membres de la classe sont privés par défaut, donc l'exemple utilise `public:` pour le constructeur et les membres. Dans un programme plus vaste, gardez les champs privés lorsque les appelants doivent plutôt utiliser des opérations contrôlées.

### Constructeurs de conversion et `explicit`

Un constructeur non `explicit` avec un seul paramètre de valeur peut participer à une conversion implicite. Marquez-le `explicit` lorsque la construction ne doit être possible que directement ou via `cast<T>` :

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
```

`UserId invalid = 9;` est rejeté. Cela rend la résolution des surcharges prévisible et évite la construction accidentelle d'un type enveloppe.

`(T)expression` est un sucre syntaxique pour `cast<T>(expression)` et suit exactement les mêmes règles de conversion :

```VeroScript
double b = 3.75;
int a = (int)b;
int same = cast<int>(b);
```

Préférez `cast<T>` lorsqu'un nom simple de type utilisateur peut être confondu avec une expression de valeur homonyme. `(void)expression;` est un abandon explicite du résultat limité aux instructions. L'expression est évaluée une fois, ses effets de bord et ses erreurs d'exécution sont conservés, puis son résultat est ignoré. Une instruction `expression;` ordinaire a le même comportement à l'exécution. Cette forme est également valide dans l'initialiseur d'une boucle `for`, mais `(void)expression` ne peut pas être utilisé comme valeur imbriquée ou argument de fonction.

```VeroScript
int calls = 0;
int touch(double value)
{
	calls += 1;
	return (int)value;
}

int main()
{
	(void)touch(2.75);
	print(calls);
	return 0;
}
```

### Champs de cache `mutable`

Utilisez le mot contextuel `mutable` uniquement sur un champ d'instance non statique de `struct` ou de `class` lorsqu'une méthode `const` doit mettre à jour un état d'implémentation, tel qu'un cache. Il ne rend pas l'objet entièrement modifiable, ne garantit pas la sûreté des accès concurrents et ne peut pas être combiné avec `const`, `static` ou un champ de référence.

```VeroScript
struct Label
{
	string source;
	mutable string cached;

	string text() const
	{
		if (cached == "")
		{
			cached = source;
		}
		return cached;
	}
};
```

### Valeurs portables en lecture seule

`readonly<T>` stocke un `T` comme une valeur ordinaire qui peut être copiée ou placée dans des conteneurs, tandis que `get()` n'expose qu'un `const T&`. Construire un `readonly<Struct>` capture la valeur de la structure ; construire un `readonly<Class>` conserve la poignée de classe.

N'utilisez `const_cast<T>(value)` que lorsque l'expression source est constante et que son type d'exécution canonique est identique à `T`. Il retire la capacité d'accès constante à la compilation sans effectuer de conversion ni de vérification du type à l'exécution. Un `cast<T>` ordinaire conserve volontairement l'accès constant.

Conservez la déclaration `Label` précédente et utilisez ce `main` pour cette étape. Il crée `label` avant le wrapper en lecture seule ; la première sortie est `source`.

```VeroScript
int main()
{
	Label label = Label();
	label.source = "source";
	readonly<Label> published = readonly<Label>(label);
	print(published.get().text());

	// Deliberate escape: mutate the payload stored by the wrapper.
	const_cast<Label>(published.get()).cached = "recomputed";
	return 0;
}
```

### Choisir entre `cast` et `try_cast`

Utilisez `cast<T>` lorsqu’une conversion est requise et qu’un échec de référence doit être une erreur. Utilisez `try_cast<T>` lorsque la relation peut ne pas exister et que l’appelant doit inspecter un `optional<T>` :

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

int main()
{
	Base value = Derived();
	optional<Derived> found = try_cast<Derived>(value);
	optional<Other> missing = try_cast<Other>(value);

	if (found.has_value())
	{
		print("derived");
	}
	if (!missing.has_value())
	{
		print("not compatible");
	}
	return 0;
}
```

`try_cast<T>` couvre les conversions primitives, d’énumérations à portée et de références/interfaces. Il n’appelle pas de constructeur `explicit`. Voir [Conversions de types](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-reference/type-conversions.md) pour les règles complètes.


## Étape 3 : décrire le comportement avec une interface

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Sensor : public IReadable
{
public:
	int value = 42;

	int read() const override
	{
		return value;
	}
};

int read_twice(IReadable value)
{
	return value.read() + value.read();
}

int main()
{
	Sensor sensor = Sensor();
	print(read_twice(sensor));
	return 0;
}
```

Une interface ne crée pas d'objets ; il indique ce qu'une implémentation doit fournir. `read_twice` dépend du contrat `IReadable` plutôt que d'un type de capteur concret.

## Étape 4 : définir un type utilitaire statique

Utilisez `static class` ou `static struct` lorsqu’un type sert uniquement de conteneur nommé pour des opérations ou des constantes. Chaque membre déclaré est traité comme statique ; les appelants utilisent donc le nom du type et `::` sans créer d’objet :

```VeroScript
public static class Math
{
	public int Base = 40;
	public int Add(int value)
	{
		return Math::Base + value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Add(2));
	print(Limits::MaxRetries);
}
```

Les types utilitaires statiques ne peuvent être construits, hérités ou déclarés comme types génériques. Ils ne peuvent pas contenir de constructeurs, de destructeurs, de membres `using`, de bloc d’initialisation par défaut, de champs `mutable` ou de modificateurs d’instance tels que `virtual` et `const`. Utilisez une `class` ou une `struct` ordinaire si le type nécessite un état par objet ou l’héritage.

## Un choix pratique

- Utilisez un `struct` lorsque des copies de valeurs indépendantes sont utiles.
- Utilisez un `class` lorsque plusieurs parties d'un programme doivent partager une identité mutable.
- Utilisez un `interface` lorsque les appelants doivent dépendre du comportement plutôt que d'une implémentation concrète.
- Utilisez un type utilitaire statique lorsqu'aucune identité ni aucun état d'instance n'est nécessaire.

## Exercices

1. Ajoutez une méthode `move` à `Point`.
2. Ajoutez `reset` à `Counter`, puis gardez `value` privé.
3. Créez une deuxième implémentation `IReadable` nommée `FixedSensor`.

Voir [Structs](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/structs.md), [Classes](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/classes.md) et [Interfaces](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/interfaces.md). Un exemple combiné est [`examples/object_basics.vs`](../../../../../VeroScript/Resources/Scripts/examples/object_basics.vs).

Suivant : [Références, retours de référence et captures lambda](06-references-and-captures.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/language/05-structs-classes-and-interfaces.md) · [fr-FR](05-structs-classes-and-interfaces.md) · [ja-JP](../../ja-JP/language/05-structs-classes-and-interfaces.md) · [ko-KR](../../ko-KR/language/05-structs-classes-and-interfaces.md) · [zh-CN](../../zh-CN/language/05-structs-classes-and-interfaces.md) · [zh-TW](../../zh-TW/language/05-structs-classes-and-interfaces.md)
