# Membres statiques

Un membre déclaré avec `static` appartient au type lui-même plutôt qu'à une instance d'objet. Un champ statique possède un emplacement de stockage au niveau du type et une méthode statique n'a pas besoin de récepteur d'objet.

## Types utilitaires statiques

`static class` et `static struct` déclarent des conteneurs utilitaires qui ne peuvent pas être instanciés. Chaque champ et chaque méthode déclaré à l'intérieur est traité comme statique et s'utilise avec le nom du type et `::` :

```VeroScript
public static class Math
{
	public double Square(double value)
	{
		return value * value;
	}
}

static struct Limits
{
	int MaxRetries = 3;
}

void main()
{
	print(Math::Square(4.0));
	print(Limits::MaxRetries);
}
```

Une classe ou une structure statique ne peut pas être construite ni utilisée comme type de base. Elle ne peut pas être générique, déclarer de constructeur ou de destructeur, de membres `using` ou de bloc d'initialisation par défaut. Ses champs ne peuvent pas être `mutable` et ses méthodes ne peuvent pas utiliser de modificateurs d'instance tels que `virtual`, `override`, `final`, pur virtuel ou `const`. Les sections d'accès restent applicables : une classe statique suit l'accès par défaut d'une classe, tandis qu'une structure statique suit celui d'une structure. Utilisez une `class` ou une `struct` ordinaire si le type doit avoir des instances, un héritage ou un état d'instance.

## Champs statiques

Accédez à un champ statique via le nom du type et `::`. Le champ `Total` ci-dessous est partagé par chaque instance `Counter` :

```VeroScript
class Counter
{
public:
	static int Total = 0;
	int id;

	Counter()
	{
		Counter::Total = Counter::Total + 1;
		id = Counter::Total;
	}
};

Counter first = Counter();
Counter second = Counter();
print("{} {}", first.id, Counter::Total);
```

Les champs statiques sont toujours soumis au contrôle d'accès. Déclarez un champ dans une section `public` lorsque du code extérieur au type doit y accéder.

## Méthodes statiques

Appelez une méthode statique via le nom du type sans créer d'objet pour l'appel :

```VeroScript
class Counter
{
public:
	static int Total = 0;

	static int Peek()
	{
		return Counter::Total;
	}
};

int current = Counter::Peek();
```

Une méthode statique n'a pas d'objet d'instance implicite, elle ne peut donc pas accéder directement aux champs non statiques ni appeler les membres dépendants de l'instance. Transmettez explicitement un objet lorsqu'un membre d'instance est nécessaire :

```VeroScript
class Formatter
{
public:
	string prefix;

	static string with_prefix(Formatter formatter, string value)
	{
		return formatter.prefix + value;
	}
};
```

## Membres statiques dans les types génériques

Chaque instance de type concret d'un type générique dispose d'un stockage statique indépendant. Par exemple, `Box<int>::Count` et `Box<string>::Count` sont des valeurs distinctes :

```VeroScript
class Box<T>
{
public:
	static int Count = 0;
	T value;
};

Box<int>::Count = Box<int>::Count + 1;
Box<string>::Count = Box<string>::Count + 1;
```

Lorsque vous utilisez un membre statique, préférez le qualifier avec le nom complet du type. Cela indique clairement que le membre appartient au type plutôt qu'à une instance d'objet.

## Valeurs par défaut et initialisation des membres

La valeur par défaut d’un champ d’instance est évaluée pendant la construction normale de cette instance. Elle peut appeler une méthode statique déclarée plus loin dans son type, y compris générique, et lit la valeur statique courante, sans instantané figé à la compilation. Construire une autre instance ne réinitialise pas les champs statiques du type. Accès, constance et règles des initialiseurs de constructeur restent applicables.

```VeroScript
class Sample<T>
{
public:
	static int Seed = 7;
	T snapshot = Sample<T>::Read();

	static T Read()
	{
		return cast<T>(Sample<T>::Seed);
	}
};

int main()
{
	Sample<int> first = Sample<int>();
	Sample<int>::Seed = 11;
	Sample<int> second = Sample<int>();
	print(first.snapshot);
	print(second.snapshot);
	print(first.snapshot);
	return 0;
}
```

La sortie est `7`, `11`, `7`, chacun sur sa ligne.

Dans un type générique ou imbriqué, son nom lexical désigne la spécialisation courante et conserve les arguments de type/valeur englobants. Des arguments explicites comme `Outer<int>::Inner` restent explicites. Un alias du même type concret partage son stockage statique. Une valeur locale homonyme reste une valeur dans les expressions ; `::` en tête sélectionne toujours la racine. Voir [Portée](scope.md).

Les champs statiques de type sont initialisés au démarrage du module dans chaque état d’exécution, contrairement à un static local initialisé au premier passage. Les dépendances statiques génériques identifiables sont préparées avant leur initialiseur consommateur. Stocker un lambda n’appelle pas son corps ; un champ nullable ou conteneur vide ne construit pas ses éléments. Ce mécanisme n’est ni une initialisation paresseuse arbitraire ni un ordre total entre types indépendants : simplifiez les dépendances et placez les effets dont l’ordre importe explicitement dans `main`.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/static-members.md) · [fr-FR](static-members.md) · [ja-JP](../../ja-JP/language-basics/static-members.md) · [ko-KR](../../ko-KR/language-basics/static-members.md) · [zh-CN](../../zh-CN/language-basics/static-members.md) · [zh-TW](../../zh-TW/language-basics/static-members.md)
