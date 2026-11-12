# 07 : Modules, importations et préprocesseur

Une fois qu'un programme s'étend sur plusieurs fichiers, les modules organisent les dépendances et les API publiques. Le préprocesseur sélectionne la source avant la compilation. Ce didacticiel commence par une implémentation privée et exporte uniquement la fonction dont les autres fichiers ont besoin.

## Étape 1 : créer un module

Créez `math_utils.vs` :

```VeroScript
export int square(int value)
{
	return value * value;
}

int hidden_value()
{
	return 99;
}
```

`export` intègre `square` aux déclarations publiques du module. `hidden_value` est privé pour le module et n'est pas une API de bibliothèque publique.

Créez `main.vs` :

```VeroScript
import "math_utils.vs";

int main()
{
	print(square(7));
	return 0;
}
```

Exécutez les deux fichiers à partir du même répertoire :

```powershell
veroscript --check main.vs
veroscript main.vs
```

## Étape 2 : réexporter un module

Créez `facade.vs` :

```VeroScript
export import "math_utils.vs";
```

Le point d'entrée peut désormais importer uniquement la façade :

```VeroScript
import "facade.vs";

int main()
{
	print(square(8));
	return 0;
}
```

La réexportation continue d'exposer les déclarations exportées par le module d'origine ; il ne rend pas `hidden_value` public.

## Étape 3 : ajouter une condition à la compilation

Le préprocesseur sélectionne le code à la compilation ; utilisez des instructions `if` ordinaires pour les décisions à l'exécution :

```VeroScript
#define MESSAGE "development"

#ifdef RELEASE
#define MESSAGE "release"
#endif

int main()
{
	print(MESSAGE);
	return 0;
}
```

Définissez `RELEASE` sur la ligne de commande :

```powershell
veroscript --check main.vs
veroscript -D RELEASE main.vs
```

Les macros se développent avant la compilation. Évitez de donner aux macros les mêmes noms que les fonctions et utilisez une vraie fonction lorsque la logique devient substantielle.

## Étape 4 : regrouper des déclarations avec un espace de noms anonyme

Un espace de noms anonyme s'écrit `namespace { ... }`. Il regroupe des déclarations sans ajouter de qualification. Dans VeroScript, il est transparent : il ne crée pas d'espace de noms synthétique et ne fournit pas de liaison interne de type C++.

```VeroScript
namespace
{
	int local_helper()
	{
		return 6;
	}
}

int main()
{
	print(local_helper());
	return 0;
}
```

Dans un espace de noms nommé, les membres conservent la qualification de l'espace de noms englobant :

```VeroScript
namespace Diagnostics
{
	namespace
	{
		int code()
		{
			return 200;
		}
	}
}

int main()
{
	print(Diagnostics::code());
	return 0;
}
```

Utilisez `export` sur les déclarations individuelles qu'un autre module doit voir. Un espace de noms anonyme n'est pas exporté en lui-même et ne rend pas les déclarations privées à lui seul.

## Exercices

1. Exportez une fonction `cube` et appelez-la depuis le module principal.
2. Créez une façade qui réexporte à la fois le module string et votre module mathématique.
3. Ajoutez une branche `#if` qui imprime des informations supplémentaires dans une version de débogage.
4. Placez une fonction utilitaire dans un espace de noms anonyme à l'intérieur d'un espace de noms nommé, puis appelez-la avec la qualification englobante.

Voir [Modules](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/modules.md), [Préprocesseur](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/preprocessor.md) et [Espaces de noms](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/namespaces.md). Les exemples distribués sont [`examples/reexport_main.vs`](../../../../../VeroScript/Resources/Scripts/examples/reexport_main.vs) et [`examples/preprocessor.vs`](../../../../../VeroScript/Resources/Scripts/examples/preprocessor.vs).

Suivant : [Chaînes, fichiers et chemins](../standard-library/08-strings-files-and-paths.md).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/language/07-modules-and-preprocessor.md) · [fr-FR](07-modules-and-preprocessor.md) · [ja-JP](../../ja-JP/language/07-modules-and-preprocessor.md) · [ko-KR](../../ko-KR/language/07-modules-and-preprocessor.md) · [zh-CN](../../zh-CN/language/07-modules-and-preprocessor.md) · [zh-TW](../../zh-TW/language/07-modules-and-preprocessor.md)
