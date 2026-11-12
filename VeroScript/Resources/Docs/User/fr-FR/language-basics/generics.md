# Génériques

Les génériques permettent à un type ou à une fonction d'utiliser des paramètres de type dans sa déclaration et de remplacer des types concrets sur les sites d'utilisation. VeroScript prend en charge les structures, classes, interfaces et fonctions génériques, ainsi que les contraintes de type `where`.

## Types génériques

Déclarez les paramètres de type entre crochets après le nom du type :

```VeroScript
class Duo<T, U = int>
{
public:
	T first;
	U second;

	Duo(T first_value, U second_value)
	{
		first = first_value;
		second = second_value;
	}
};

Duo<string> text_pair = Duo<string>("score", 10);
Duo<int, string> number_pair = Duo<int, string>(10, "points");
```

Les paramètres de type de fin peuvent avoir des types par défaut, et une valeur par défaut peut faire référence à un paramètre de type antérieur. Les arguments de type explicites forment un préfixe ; les arguments ultérieurs peuvent être déduits du contexte ou des arguments d'appel.

## Fonctions génériques

Déclarez les paramètres de type de fonction après le nom de la fonction :

```VeroScript
T identity<T>(T value)
{
	return value;
}

string text = identity<string>("VeroScript");
int number = identity(42);
```

Lorsque les arguments fournissent suffisamment d’informations, les arguments de type peuvent être omis. Fournissez-les explicitement lorsqu'un type particulier doit être sélectionné.

`template<T>` est une autre orthographe lisible pour une déclaration générique. Il suit les mêmes règles génériques qu'un nom de type ou de fonction suivi de `<T>` :

```VeroScript
template<T>
T identity_again(T value)
{
	return value;
}
```

## Contraintes de type

Utilisez `where` pour exiger un contrat d'interface à partir d'un paramètre de type :

```VeroScript
interface IReadable
{
	virtual int read() const = 0;
};

T read_once<T>(T value) where T : IReadable
{
	return value;
}
```

Un paramètre de type peut avoir plusieurs contraintes d'interface :

```VeroScript
T describe<T>(T value) where T : IReadable, IName
{
	return value;
}
```

Les contraintes sont vérifiées au moment de la compilation. Un type concret utilisé pour appeler une fonction générique ou instancier un type générique doit satisfaire chaque contrainte déclarée.

Un alias de type d'interface ou une chaîne d'alias peut servir de contrainte : après `using Readable = IReadable;`, écrivez `where T : Readable`. Les alias d'interfaces génériques concrètes telles que `IBox<int>` sont également pris en charge. L'alias est résolu dans le module de la déclaration ; le type concret doit toujours implémenter l'interface obtenue.

## Membres et interfaces génériques

Un type générique peut hériter d'une interface ou d'un type de base générique, et une interface peut elle-même avoir des paramètres de type :

```VeroScript
interface IBox<T>
{
	virtual T get() const = 0;
};

class Box<T> : public IBox<T>
{
public:
	T value;

	T get() const override
	{
		return value;
	}
};
```

Les membres d'une déclaration générique suivent toujours les règles normales de contrôle d'accès, de méthodes virtuelles, de surcharges et de membres statiques. Les éléments statiques génériques disposent d'un stockage indépendant pour chaque type de béton ; voir [Membres statiques](static-members.md).

## Limites des paramètres de type

Un paramètre de type ne peut être utilisé qu'avec des opérations que le compilateur peut établir à partir de sa déclaration. Ajoutez une contrainte `where` avant d'appeler une méthode d'interface ou d'accéder à un membre contraint ; ne comptez pas sur des membres implicites non déclarés.

Un accès statique qualifié comme `T::member` est vérifié après substitution du type concret, qui doit fournir le champ ou la méthode statique accessible correspondant. Accès, surcharge, constance et références restent vérifiés. Voir [Membres statiques](static-members.md) pour les types propres, alias et valeurs par défaut de champs.

Les types imbriqués, les déclarations locales, les règles d’accès et les arguments génériques par segment sont décrits dans [Portées des types](scope.md).

## Modèles de méthodes et paramètres de valeur

`T`, `typename T` et `class T` déclarent le même paramètre de type. Les méthodes d’instance, statiques et auxiliaires non virtuelles d’interface avec corps peuvent avoir leurs propres paramètres. Un paramètre interne ne peut pas masquer un paramètre englobant. Un modèle de méthode ne peut être virtual, override ou final.

Les paramètres de valeur acceptent bool, les entiers de largeur fixe et les énumérations du script ou de l’hôte enregistré. Leur type peut dépendre d’un paramètre précédent : `template<typename T, T Value>`. Exemple : `template<typename T, int Count = 4> struct Buffer { T values[Count]; };`. Les valeurs sont des constantes de compilation immuables. Seuls les calculs scalaires purs, conversions contrôlées, membres d’énumération et initialiseurs const évaluables sont utilisés ; les fonctions [constexpr](../language-reference/constexpr.md) admissibles peuvent être évaluées, mais aucune autre fonction ni aucun rappel hôte n’est exécuté. Débordement, division par zéro, valeur hors plage et mauvaise identité d’énumération sont des erreurs. Les dimensions des tableaux fixes sont vérifiées après substitution.

Après la première valeur par défaut, tous les paramètres propres suivants doivent en avoir une. Les noms sont liés dans le contexte de déclaration ; les paramètres englobants ou précédents sont accessibles. Les appels appliquent le préfixe explicite, déduisent depuis la structure des types, puis complètent les valeurs par défaut. Les valeurs d’exécution ne servent pas à déduire les paramètres de valeur.

`box.echo<string>("x")` sélectionne explicitement les modèles ; `box.echo("x")` autorise la déduction. `f<>()` ne considère que les modèles. La qualité des conversions prime ; à égalité, le candidat ordinaire gagne. Ensuite seule une spécialisation structurelle démontrable départage les candidats. La recherche de type privilégie le nombre total exact de paramètres propres avant les valeurs par défaut ; plusieurs déclarations complétables sont ambiguës.

`identity<int>`, `box.echo<string>` et les références statiques créent des callables ; une signature fn explicite aide aussi la déduction. Les noms sous forme de chaînes dynamiques n’instancient pas les modèles ouverts. Une définition externe utilise un en-tête template par propriétaire générique et pour la méthode, avec correspondance positionnelle et sans répéter les valeurs par défaut.

Voir `method_templates.vs`, `value_template_parameters.vs` et `template_methods_main.vs`. Ces fonctions utilisent Core VM/Mixed/JIT ou Core AOT. Les exporteurs source indépendants signalent les modèles non pris en charge avant toute sortie. Une bibliothèque compilée conserve les déclarations ouvertes pour de nouveaux arguments sans sources originales. La réflexion seule ne génère pas de corps exécutable.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/generics.md) · [fr-FR](generics.md) · [ja-JP](../../ja-JP/language-basics/generics.md) · [ko-KR](../../ko-KR/language-basics/generics.md) · [zh-CN](../../zh-CN/language-basics/generics.md) · [zh-TW](../../zh-TW/language-basics/generics.md)
