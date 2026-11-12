# Interfaces

`interface` déclare un ensemble de contrats de membres que les structures ou les classes peuvent implémenter. Une interface ne peut pas être instanciée directement, mais elle peut être utilisée comme paramètre, type de retour ou type de variable pour accéder uniformément à différentes implémentations.

## Déclarations d'interface

Les méthodes d'interface sont généralement déclarées comme des méthodes virtuelles pures. Une méthode virtuelle pure doit utiliser `virtual` et se terminer par `= 0;` :

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};
```

Une méthode virtuelle pure doit être publique. Sa liste de paramètres, son type de retour et sa qualification `const` forment ensemble le contrat de mise en œuvre.

## Implémentation d'une interface

Une structure ou une classe nomme l'interface dans sa liste d'héritage et fournit une implémentation de méthode correspondante :

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

class Counter : public IReadable
{
public:
	int value = 0;

	int read() const override
	{
		return value;
	}
};
```

Utilisez `override` pour rendre explicite l’implémentation d’une méthode d’interface. Une classe ou une structure qui possède encore une méthode virtuelle pure non implémentée ne peut pas être instanciée en tant qu'objet concret.

Un type peut implémenter plusieurs interfaces :

```VeroScript
interface IId
{
	virtual int id() const = 0;
};

interface IName
{
	virtual string name() const = 0;
};

class Person : public IId, public IName
{
public:
	int id() const override
	{
		return 42;
	}

	string name() const override
	{
		return "Ada";
	}
};
```

## Implémentations par défaut

Une méthode d'interface peut également fournir un corps, formant une implémentation par défaut. Un type d'implémentation peut hériter de cette implémentation ou fournir sa propre version avec `override` :

```VeroScript
interface IDescribable
{
	virtual string name() const = 0;

	string describe() const
	{
		return "name=" + name();
	}
};
```

Une méthode par défaut reste partie intégrante du contrat d'interface ; seule une méthode virtuelle pure non implémentée empêche l'instanciation concrète.

## Héritage de l'interface

Une interface peut hériter d'une ou plusieurs interfaces parents :

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

interface IResource : IReadable
{
	virtual void close() = 0;
};
```

Un type implémentant `IResource` doit satisfaire toutes les méthodes virtuelles pures déclarées par lui et ses interfaces parentes. Chaque type dans une liste d’héritage d’interface doit lui-même être une interface.

## Valeurs et conversions de l'interface

La conversion d'une structure en type d'interface crée la représentation de la valeur de structure requise par l'interface ; accéder à cette valeur d'interface ne modifie pas la structure d'origine. La conversion d'une classe en interface préserve l'identité partagée de l'objet classe :

```VeroScript
interface ICounter
{
	virtual void add(int amount) = 0;
};

struct ValueCounter : ICounter
{
	int value = 0;
	void add(int amount)
	{
		value += amount;
	}
};

class SharedCounter : ICounter
{
public:
	int value = 0;
	void add(int amount) override
	{
		value += amount;
	}
};
```

La copie et l'appel d'une valeur d'interface dépendent donc de l'implémentation sous-jacente : une valeur d'interface de structure suit la sémantique de valeur, tandis qu'une valeur d'interface de classe suit la sémantique de handle de classe. Une variable d'interface ne peut pas être utilisée pour construire un nouvel objet d'implémentation.

Les types imbriqués, les déclarations locales, les règles d’accès et les arguments génériques par segment sont décrits dans [Portées des types](scope.md).

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/interfaces.md) · [fr-FR](interfaces.md) · [ja-JP](../../ja-JP/language-basics/interfaces.md) · [ko-KR](../../ko-KR/language-basics/interfaces.md) · [zh-CN](../../zh-CN/language-basics/interfaces.md) · [zh-TW](../../zh-TW/language-basics/interfaces.md)
