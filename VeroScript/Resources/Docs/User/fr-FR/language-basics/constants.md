# Constantes

Une constante est une variable ou un membre qui ne peut pas être modifié via son nom déclaré. VeroScript utilise `const` pour les constantes.

`const` garantit un accès en lecture seule, sans imposer une initialisation à la compilation. Utilisez [constexpr](../language-reference/constexpr.md) pour exiger une valeur scalaire connue à la compilation.

## Variables constantes

`const` peut être utilisé pour les variables locales et au niveau du module. Une constante doit être initialisée lors de sa déclaration :

```VeroScript
const int max_count = 100;

int main()
{
	const string product_name = "VeroScript";
	print(product_name);
}
```

Après initialisation, une constante ne peut pas être affectée via ce nom :

```VeroScript
const int limit = 10;
// limit = 20; // Compile error.
```

La constante a toujours le type écrit dans sa déclaration. `const` ne convertit pas la valeur et ne modifie pas sa portée.

## Membres constants

Une structure ou une classe peut déclarer un champ constant :

```VeroScript
class Settings
{
public:
	const int version = 1;
};
```

Un champ non statique sans initialiseur de déclaration peut être initialisé dans la liste d'initialisation du constructeur :

```VeroScript
class Settings
{
public:
	const int version;

	Settings(int initial_version)
		: version(initial_version)
	{
	}
};
```

La liste ne peut nommer que les champs d'instance déclarés directement par le type actuel. Les entrées de base directe de la liste d'initialisation s'exécutent d'abord ; les entrées de champ de cette liste s'exécutent ensuite dans l'ordre de déclaration des champs, avant le corps du constructeur. Un champ qui possède un initialiseur de déclaration ne peut pas aussi apparaître dans cette liste, et un même champ ne peut pas y apparaître deux fois. Une écriture dans le corps du constructeur reste une affectation ordinaire et ne peut donc pas modifier un champ `const`. Les initialiseurs de déclaration et les champs omis de la liste continuent de suivre les règles existantes de construction par défaut.

## `const` fonctions membres

Une fonction membre peut utiliser `const` après sa liste de paramètres pour indiquer qu'elle ne peut pas modifier l'état mutable de l'objet actuel :

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
```

Les fonctions membres Const sont appropriées pour les requêtes. La possibilité d'appeler une fonction membre dépend de la constance de l'objet et du qualificatif `const` de la fonction.

### L’exception `mutable`

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

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/constants.md) · [fr-FR](constants.md) · [ja-JP](../../ja-JP/language-basics/constants.md) · [ko-KR](../../ko-KR/language-basics/constants.md) · [zh-CN](../../zh-CN/language-basics/constants.md) · [zh-TW](../../zh-TW/language-basics/constants.md)
