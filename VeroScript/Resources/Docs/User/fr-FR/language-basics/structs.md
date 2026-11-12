# Structures

`struct` définit un type d'utilisateur avec une sémantique de valeur. Une structure peut contenir des champs, des méthodes, des constructeurs et des destructeurs. Il peut également hériter d’un type de base ou implémenter des interfaces. L'objet struct lui-même est copié par valeur ; chaque champ suit toujours la sémantique de son propre type.

## Déclaration et membres

Une déclaration struct se compose d'un nom et d'une liste de membres. Les membres de la structure sont `public` par défaut :

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	void translate(int dx, int dy)
	{
		this.x += dx;
		this.y += dy;
	}
};
```

Dans une fonction membre, `this` fait référence à l'objet struct actuel. Un membre accessible par son nom nu est également accessible sous le nom `this.member`.

Les structures prennent en charge le contrôle d’accès `public`, `protected` et `private`. Bien que les membres soient publics par défaut, utilisez un spécificateur d'accès explicite lorsque l'accès doit être restreint :

```VeroScript
struct Account
{
public:
	int id = 0;

private:
	int balance = 0;
};
```

## Construction et initialisation

Les champs peuvent fournir des expressions d'initialisation par défaut. Une structure peut également déclarer des constructeurs ; construction sélectionne une surcharge dans la liste d'arguments :

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point()
	{
	}

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}
};

Point origin = Point();
Point point = Point(3, 4);
Point literal = Point{7, 8};
```

Une liste d'initialisation de constructeur peut initialiser les champs d'instance directs avant le corps du constructeur. Les entrées de champ de cette liste sont initialisées dans l'ordre de déclaration des champs. Un champ qui possède un initialiseur de déclaration ne peut pas aussi apparaître dans cette liste ; consultez [Constantes](constants.md) pour les règles des champs `const`.

La forme entre accolades est une initialisation typée et suit les mêmes règles de correspondance de constructeur. Lorsqu'aucun constructeur explicite n'est déclaré, les valeurs par défaut des champs participent toujours à la construction par défaut.

## Sémantique des valeurs

L'attribution d'une structure copie sa valeur. La modification d'un champ direct sur la copie ne change pas la structure d'origine :

```VeroScript
struct Counter
{
	int value = 0;
};

Counter first = Counter();
Counter second = first;
second.value = 10;

print(first.value);  // 0
print(second.value); // 10
```

La sémantique des valeurs décrit l'objet struct lui-même. Si un champ est de type `array<T>`, `map<K, V>` ou un autre type de handle partagé, ce champ conserve son propre comportement de handle partagé lorsque la structure est copiée. Copiez explicitement le contenu du conteneur lorsqu'un conteneur indépendant est requis.

## Méthodes et références

Une méthode struct peut lire ou modifier l'objet actuel. Utilisez un paramètre de référence lorsqu'une fonction doit modifier directement la structure de l'appelant :

```VeroScript
struct Point
{
	int x = 0;
	int y = 0;

	Point(int new_x, int new_y)
	{
		x = new_x;
		y = new_y;
	}

	void translate(int dx, int dy)
	{
		x += dx;
		y += dy;
	}
};

void reset(ref Point point)
{
	point.x = 0;
	point.y = 0;
}

int main()
{
	Point point = Point{3, 4};
	point.translate(1, 2);
	reset(ref point);
	print("{}, {}", point.x, point.y);
	return 0;
}
```

Les paramètres ordinaires et l'itération de plage de valeurs créent des copies de valeurs de structure. Les paramètres de référence et l'itération de la plage de référence sont écrits dans l'objet d'origine. Voir [Références](references.md) pour les règles détaillées.

## Champs d’implémentation `mutable`

Un champ d’instance non statique peut être marqué `mutable` lorsqu’il représente un état d’implémentation qu’une requête `const` est autorisée à actualiser, le plus souvent un cache :

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

Cette exception ne s’applique qu’au champ marqué. Elle ne rend pas un objet const généralement modifiable et ne garantit pas la sécurité des accès concurrents. Un champ `mutable` ne peut pas être également `const`, `static` ou un champ de référence.

## Héritage et interfaces

Une structure peut hériter d'une base de structure et implémenter une ou plusieurs interfaces :

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

struct Counter : IReadable
{
	int value = 0;

	int read() const
	{
		return value;
	}
};
```

La conversion d'une structure en type d'interface crée la représentation de valeur requise par l'interface. La mutation de cette valeur d'interface ne réécrit pas dans la structure d'origine. L'héritage des interfaces est traité dans la page [Interfaces](interfaces.md) ; les contraintes génériques sont couvertes dans la page [Génériques](generics.md).

## Membres spéciaux par défaut

```VeroScript
struct Record
{
	int value = 0;
	Record() = default;
	Record(const Record& other) = default;
	Record& operator=(const Record& other) = default;
	bool operator==(const Record& other) const = default;
	bool operator!=(const Record& other) const = default;
};
```

Une structure peut demander une construction par copie, une affectation et une égalité membre par membre. La copie ne relance pas les initialiseurs de champs. L’affectation met à jour les sous-objets existants et renvoie une référence à l’objet initial. Un champ const ou référence peut rendre l’affectation par défaut indisponible ; la copie et l’égalité restent indépendantes.

Les classes peuvent demander une égalité par défaut tout en conservant la copie de leurs références partagées. Une opération supprimée ou inaccessible ne déclenche jamais une copie brute. Un constructeur de copie explicite convient à une construction directe, pas à une initialisation implicite. Un résultat de construction neuf ne nécessite pas de copie supplémentaire. Aucune opération de déplacement du langage n’est ajoutée.

Ces déclarations utilisent l’exécution du cœur, y compris le repli vers la machine virtuelle avec les stratégies mixte ou JIT, ainsi que l’AOT du cœur. Les exports autonomes C++, C# et Python refusent les membres par défaut non pris en charge. Exemple : `default_special_members.vs`.

Les types imbriqués, les déclarations locales, les règles d’accès et les arguments génériques par segment sont décrits dans [Portées des types](scope.md).

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/structs.md) · [fr-FR](structs.md) · [ja-JP](../../ja-JP/language-basics/structs.md) · [ko-KR](../../ko-KR/language-basics/structs.md) · [zh-CN](../../zh-CN/language-basics/structs.md) · [zh-TW](../../zh-TW/language-basics/structs.md)
