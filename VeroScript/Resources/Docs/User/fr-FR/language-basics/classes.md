# Classes

`class` définit un type d'utilisateur avec une identité partagée. Les valeurs de classe sont des handles, peuvent être nulles et prennent en charge les champs, les méthodes, les constructeurs, les destructeurs, l'héritage et les méthodes virtuelles. Contrairement à une structure, l’attribution d’une variable de classe copie normalement le handle de l’objet plutôt que l’objet lui-même.

## Déclaration et contrôle d'accès

Une classe peut contenir des champs et des fonctions membres. Les membres de la classe sont `private` par défaut, utilisez donc une section `public` explicite pour l'interface exposée aux appelants :

```VeroScript
class Counter
{
	int value = 0;

public:
	void add(int amount)
	{
		value += amount;
	}

	int get() const
	{
		return value;
	}
};
```

Les membres peuvent également être `protected` ou `private`. Un spécificateur d'accès contrôle quels membres sont visibles par la classe, les classes dérivées et le code extérieur. `friend` peut accorder un accès supplémentaire à une fonction ou une classe spécifiée.

## Construction et objets nuls

Une classe peut déclarer un ou plusieurs constructeurs. Une expression de construction crée un nouvel objet de classe :

```VeroScript
class Counter
{
public:
	int value = 0;

	Counter(int initial)
	{
		value = initial;
	}
};

Counter counter = Counter(10);
```

Une liste d'initialisation de constructeur peut appeler les constructeurs des bases directes et initialiser les champs d'instance directs avant le corps du constructeur. Les entrées de champ de cette liste sont initialisées dans l'ordre de déclaration des champs. Un champ qui possède un initialiseur de déclaration ne peut pas aussi apparaître dans cette liste ; consultez [Constantes](constants.md) pour les règles des champs `const`.

Pendant la construction ou la destruction, les appels virtuels sur l'objet utilisent l'implémentation du type dont le constructeur ou le destructeur est en cours d'exécution, y compris via une fonction auxiliaire ou une méthode liée. Un constructeur de base n'appelle pas une redéfinition de la partie dérivée encore non initialisée. Appeler à ce stade une méthode virtuelle pure sans implémentation provoque une erreur d'exécution. Après la construction, la répartition dynamique ordinaire utilise le type dérivé réel.

Si la construction échoue, le destructeur du type en échec n'est pas exécuté. Les membres initialisés et les bases dont la construction est terminée sont libérés dans l'ordre inverse de leur construction.

Un handle de classe peut contenir `nullptr` :

```VeroScript
Counter empty = nullptr;
if (empty == nullptr)
{
	print("empty");
}
```

L’appel d’un membre d’instance via un handle nul produit une erreur d’exécution. Assurez-vous qu’un handle fait référence à un objet valide avant de l’utiliser.

## Identité partagée

L'affectation d'une variable de classe à une autre fait que les deux variables font référence au même objet :

```VeroScript
class Box
{
public:
	int value = 0;

	void set(int next)
	{
		value = next;
	}
};

Box first = Box();
Box second = first;
second.set(10);

print(first.value);  // 10
```

Construire un autre objet lorsqu'une identité indépendante est requise ; ne copiez pas la poignée. Les champs de classe, les valeurs de classe dans les conteneurs et les paramètres de classe suivent la même sémantique d'identité partagée.

## Objets Const et méthodes de requête

Une fonction membre peut être qualifiée avec `const`, ce qui signifie qu'elle ne peut pas modifier les membres via l'objet actuel. Un objet const ne peut appeler que des méthodes valides pour les objets const :

```VeroScript
class Counter
{
public:
	int value = 0;

	int get() const
	{
		return value;
	}
};

const Counter counter = Counter();
print(counter.get());
```

Préférez les fonctions membres `const` pour les opérations qui inspectent uniquement l’état de l’objet. Une fonction membre non `const` ne peut pas être appelée via un objet `const`.

### Champs d’implémentation `mutable`

`mutable` est un spécificateur de champ contextuel pour un champ d’instance non statique d’une `struct` ou d’une `class`. Il permet de mettre à jour ce seul champ depuis une fonction membre `const` :

```VeroScript
class Label
{
	string source;
	mutable string cached;

public:
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

`mutable` ne rend pas tout l’objet modifiable, ne garantit pas la sécurité des threads et ne peut pas être combiné avec `const`, `static` ou un champ de référence. Il est contextuel plutôt que globalement réservé : en dehors d’une déclaration de champ, `mutable` peut encore être utilisé comme identifiant ordinaire lorsque la grammaire l’autorise.

## Héritage et méthodes virtuelles

Une classe peut hériter d'une ou plusieurs classes de base et implémenter des interfaces. Utilisez `virtual` pour les méthodes distribuées dynamiquement et `override` pour rendre explicite un remplacement de méthode de base :

```VeroScript
class Base
{
public:
	virtual int value() const
	{
		return 1;
	}
};

class Derived : public Base
{
public:
	int value() const override
	{
		return 2;
	}
};

int main()
{
	Base object = Derived();
	print(object.value());
}
```

Une classe abstraite peut déclarer une méthode virtuelle pure telle que `virtual int value() const = 0;`. Une classe avec un emplacement virtuel pur non implémenté ne peut pas être instanciée. Un destructeur virtuel garantit le chemin de destruction correct lorsqu'un objet dérivé est détruit via une identité de classe de base.

Les règles complètes pour les interfaces, l'accès à l'héritage et l'héritage multiple sont couvertes dans la documentation [Interfaces](interfaces.md).

## Destructeurs

Une classe peut déclarer un destructeur sous la forme `~TypeName()`, qui s'exécute à la fin de la durée de vie de l'objet. Un destructeur peut également être virtuel ou pur virtuel. Lorsqu'un héritage est impliqué, choisissez la conception du destructeur virtuel en fonction de la manière dont l'objet sera détruit.


## Définitions hors classe

Déclarez les membres dans la classe puis définissez-les plus loin dans le même module source avec `Type::member`. Types des paramètres et du retour, qualifications de référence et `const` doivent correspondre à la déclaration. Un constructeur n’a pas de type de retour. Il s’agit de définitions source ; ne répartissez pas les déclarations et implémentations entre modules script liés séparément.

```VeroScript
class Counter
{
public:
	Counter(int seed);
	int Add(int delta) const;
	int value;
};

Counter::Counter(int seed)
{
	this.value = seed;
}

int Counter::Add(int delta) const
{
	return this.value + delta;
}

void main()
{
	Counter counter = Counter(6);
	print(counter.Add(1));
}
```

Les types imbriqués, les déclarations locales, les règles d’accès et les arguments génériques par segment sont décrits dans [Portées des types](scope.md).

## Champs autoréférents

Une classe générique peut conserver un handle vers sa propre spécialisation. Le champ peut être initialisé à `nullptr` ; affecter un autre objet préserve son identité.

```VeroScript
class Node<T>
{
public:
	T value;
	Node<T> next = nullptr;
};
```

Les champs de structure récursifs par valeur, directs ou mutuels, restent interdits. L’héritage cyclique et l’expansion illimitée des templates sont également refusés ; les limites de profondeur de compilation restent applicables.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/classes.md) · [fr-FR](classes.md) · [ja-JP](../../ja-JP/language-basics/classes.md) · [ko-KR](../../ko-KR/language-basics/classes.md) · [zh-CN](../../zh-CN/language-basics/classes.md) · [zh-TW](../../zh-TW/language-basics/classes.md)
