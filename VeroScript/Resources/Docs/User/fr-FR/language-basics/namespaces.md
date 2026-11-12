# espaces de noms

Les espaces de noms organisent les noms et empêchent les collisions entre des zones distinctes d'un script. Un espace de noms peut contenir des variables, des fonctions, des structures, des classes, des interfaces, des énumérations et d'autres déclarations.

## Déclarer et qualifier les noms

Déclarez un espace de noms avec `namespace` et accédez à ses membres avec `::` :

```VeroScript
namespace Util
{
	int Seed = 10;

	int next_seed()
	{
		Seed = Seed + 1;
		return Seed;
	}
}

int current = Util::Seed;
int next = Util::next_seed();
```

Les noms qualifiés rendent explicite l’espace de noms propriétaire. Préférez `::` pour les membres de type, les membres enum et les membres statiques :

```VeroScript
enum class State
{
	Ready,
	Running
};

State state = State::Running;
```

## Qualification globale

Un `::` initial commence la recherche dans la portée globale :

```VeroScript
int GlobalValue = 3;

namespace Feature
{
	int GlobalValue = 9;

	int read_root_value()
	{
		return ::GlobalValue;
	}
}
```

`::GlobalValue` ignore la portée de l’espace de noms courant, les alias locaux et le repli de `using namespace`. Cette forme peut aussi qualifier un type ou un espace de noms global, et `using namespace ::Name` est valide.

## Espaces de noms anonymes

`namespace { ... }` déclare un espace de noms anonyme. Dans VeroScript, il s'agit d'un groupe de déclarations transparent : il ne crée pas d'espace de noms synthétique et ne fournit pas de liaison interne de type unité de traduction comme en C++. Ses membres restent dans l'espace de noms environnant.

À la portée globale, les membres d'un espace de noms anonyme peuvent être utilisés sans qualification supplémentaire :

```VeroScript
namespace
{
	int transparent_helper()
	{
		return 40;
	}
}

int main()
{
	print(transparent_helper() + 2);
	return 0;
}
```

La même règle s'applique à l'intérieur d'un espace de noms nommé. Les membres conservent la qualification de l'espace de noms englobant :

```VeroScript
namespace Outer
{
	namespace
	{
		int nested_helper()
		{
			return 9;
		}
	}
}

int main()
{
	print(Outer::nested_helper());
	return 0;
}
```

Un espace de noms anonyme ne modifie ni l'exportation ni la visibilité du module. Exportez les déclarations individuelles qu'un autre module doit utiliser ; utilisez un espace de noms anonyme lorsqu'un regroupement transparent améliore l'organisation du code.

## L'espace de noms standard

Les conteneurs intégrés, les types de valeurs utilitaires et les fonctions d’exécution publiques disposent d’une écriture recommandée dans l’espace de noms réservé `std` :

```VeroScript
import "string.vs";

std::array<int> values = {1, 2, 3};
std::optional<int> selected = std::optional<int>(values[0]);
std::print(std::format("selected: {}", selected.get()));
```

Les écritures globales existantes telles que `array`, `optional`, `print` et `format` restent des alias de compatibilité. `std::` est résolu au moment de la compilation et n’ajoute aucune répartition VM ou JIT. Les fonctions de bibliothèque source telles que `std::sqrt` nécessitent toujours leur importation normale de module ; la qualification ne contourne pas `import`/`export`.
L'espace de noms global `std` est réservé et ne peut pas être étendu par des scripts.

Un espace de noms organise les noms mais ne modifie pas automatiquement leur statut d'exportation. Le fait qu'une déclaration soit visible en dehors de son module est toujours contrôlé par les règles `export` du module.

Voir [L'espace de noms `std`](standard-namespace.md) pour les tables complètes d'alias de types et de fonctions.

## Utiliser un espace de noms

`using namespace` rend les membres directs d'un espace de noms disponibles dans la portée conteneur :

```VeroScript
namespace MathTools
{
	int base = 2;
}

using namespace MathTools;

int value = base + 3;
```

La directive affecte sa portée et le code qui la suit. Il ne réexporte pas les noms et n’affecte pas les espaces de noms frères. Conservez la qualification complète lorsque les noms peuvent entrer en collision :

```VeroScript
int value = MathTools::base + 3;
```

Utilisez des espaces de noms pour des limites de noms stables. Utilisez un module `import` lorsque l'objectif est d'importer des déclarations d'un autre fichier source.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/namespaces.md) · [fr-FR](namespaces.md) · [ja-JP](../../ja-JP/language-basics/namespaces.md) · [ko-KR](../../ko-KR/language-basics/namespaces.md) · [zh-CN](../../zh-CN/language-basics/namespaces.md) · [zh-TW](../../zh-TW/language-basics/namespaces.md)
