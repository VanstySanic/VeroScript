# Portée

La portée détermine où un nom est valide dans le code source. Les portées VeroScript courantes incluent la portée des modules, des espaces de noms, des fonctions, des blocs et des membres.

## Portée du bloc

Les accolades à l’intérieur d’une fonction ou d’une instruction de flux de contrôle créent un bloc. Une variable locale est valide depuis sa déclaration jusqu'à la fin de son bloc englobant :

```VeroScript
int main()
{
	int outer = 1;

	if (outer > 0)
	{
		int inner = 2;
		print(outer);
		print(inner);
	}

	// inner is out of scope here.
	print(outer);
}
```

Un bloc interne peut accéder aux noms déjà déclarés dans un bloc externe. Un bloc externe ne peut pas accéder aux variables locales déclarées dans un bloc interne.

## Portée de la fonction

Les paramètres de fonction et les variables locales appartiennent à la portée de la fonction. Différentes fonctions peuvent utiliser le même nom local ; chaque déclaration est distincte :

```VeroScript
void first()
{
	int value = 1;
	print(value);
}

void second()
{
	int value = 2;
	print(value);
}
```

## Portée du module

Les variables, fonctions et types déclarés au niveau supérieur appartiennent au module actuel. Les déclarations qui ne sont pas exportées ne sont normalement disponibles qu'à l'intérieur du module ; les déclarations exportées peuvent être utilisées par d'autres modules selon les règles d'importation.

## Portée de l'espace de noms

Un espace de noms place les déclarations dans une plage de noms distincte :

```VeroScript
namespace math
{
	int add(int left, int right)
	{
		return left + right;
	}
}

int main()
{
	print(math::add(1, 2));
}
```

Les membres de l'espace de noms sont accessibles avec `.` ou `::`. Un espace de noms ne remplace pas un module : les modules organisent les importations de fichiers et de paquets, tandis que les espaces de noms organisent les noms de déclarations.

### Qualification de la portée globale

Le préfixe `::` force la recherche depuis la portée globale et évite les résultats provenant de l’espace de noms courant, d’un alias local ou de `using namespace`. Utilisez `::Name` lorsque la déclaration racine est intentionnelle.

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

Il peut également qualifier un type ou un espace de noms racine et est accepté dans `using namespace ::Name`.

## Portée des membres

Les champs et les méthodes d'une structure, d'une classe ou d'une interface appartiennent à la portée membre du type. L'accès est restreint par `public`, `protected` et `private` :

```VeroScript
class Counter
{
private:
	int value = 0;

public:
	int get()
	{
		return value;
	}
};
```

Les fonctions membres peuvent utiliser `this` pour accéder à l'objet actuel. Les membres statiques ne dépendent pas d'une instance d'objet particulière et ont des règles d'accès différentes.

## Nom masqué

Une portée interne peut déclarer le même nom qu’une portée externe. La déclaration interne masque alors la déclaration externe. Évitez les ombres inutiles, car elles rendent la recherche de nom plus difficile à lire.


## Types imbriqués et locaux

Un type peut déclarer des class, struct, interface, enum et alias imbriqués. Utilisez `Outer::Inner`, avec les arguments propres à chaque segment générique : `Outer<int>::Inner<string>`. Un type public imbriqué est accessible via son type externe exporté ; les chemins privés/protégés et les alias publics restent soumis aux contrôles habituels.

Les blocs exécutables peuvent déclarer des types locaux, visibles de leur déclaration à la fin du bloc. Un bloc interne peut masquer leur nom. Ils utilisent les types, alias, constantes enum et paramètres génériques visibles, sans capturer les variables externes ni un `this` externe. Passez explicitement un objet pour accéder à ses membres. Le stockage statique est propre au type concret et à l’instance d’exécution.

```VeroScript
class Outer<T>
{
public:
	struct Inner<U>
	{
		T key;
		U value;
	};
};

void main()
{
	Outer<int>::Inner<string> item = Outer<int>::Inner<string>();
	item.key = 7;
	item.value = "saved";
	struct Result
	{
		int count = 1;
	};
	Result result = Result();
	print("{} {} {}", item.key, item.value, result.count);
}
```

Une méthode imbriquée non locale peut être définie sous la forme `T Outer<T>::Inner<U>::read() { ... }`, y compris dans un autre fichier important la déclaration. Les méthodes locales restent dans leur type ou dans un corps généré par un processeur. Les déclarations anticipées de types incomplets, la réouverture d’un type et les définitions inter-fichiers de méthodes locales ne sont pas prises en charge.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/scope.md) · [fr-FR](scope.md) · [ja-JP](../../ja-JP/language-basics/scope.md) · [ko-KR](../../ko-KR/language-basics/scope.md) · [zh-CN](../../zh-CN/language-basics/scope.md) · [zh-TW](../../zh-TW/language-basics/scope.md)
