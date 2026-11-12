# Modules

Les modules organisent les déclarations autour des limites du fichier source et contrôlent la visibilité. Un module est normalement représenté par un fichier `.vs`, importé avec `import`, et mis à disposition des autres modules avec `export`.

## Importer un module

Utilisez `import` pour importer un module source ou un paquet de bibliothèque :

```VeroScript
import "string.vs";

int main()
{
	print(std::strings::join(std::strings::split("a,b,c", ","), "-"));
}
```

Lorsqu'un module est importé plusieurs fois, son identité de module supprime le chargement en double. L'importation d'un module n'expose pas toutes ses déclarations privées ; seules les déclarations exportées sont disponibles pour les modules externes.

## Exporter les déclarations

Placez `export` avant une déclaration pour le rendre disponible aux modules importateurs :

```VeroScript
export struct Box<T>
{
	T value;

	Box(T initial)
	{
		value = initial;
	}

	T get() const
	{
		return value;
	}
};

int hidden_value()
{
	return 99;
}
```

`Box` est disponible dans le cadre de l'API publique du module, tandis que `hidden_value` est privé car il n'est pas exporté. Les fonctions, variables, structures, classes, interfaces, énumérations et déclarations génériques peuvent être exportées.

## Réexporter un module

`export import` importe un module et rend les déclarations exportées de ce module disponibles via le module actuel :

```VeroScript
export import "string.vs";
```

Un autre module peut importer la façade et utiliser les déclarations réexportées par celle-ci :

```VeroScript
import "string_facade.vs";

string text = std::strings::join(std::strings::split("a,b", ","), "-");
```

La réexportation ne transforme pas les déclarations privées en déclarations publiques et ne copie pas le texte source.

## Modules et espaces de noms

Les modules gèrent les dépendances et la visibilité du fichier source. Les espaces de noms organisent les noms dans un module. Un module peut déclarer plusieurs espaces de noms et importer des déclarations publiques à partir d'espaces de noms dans un autre module.

## Importer un paquet de bibliothèque

Les modules sources et les paquets de bibliothèques binaires utilisent la même syntaxe `import` :

```VeroScript
import "DemoLibrary.vslib";
```

Un paquet de bibliothèque ne peut exposer que les déclarations exportées lors de sa construction. Voir [Paquets et distribution](../../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/packages.md) pour la création, la vérification et la distribution de paquets.

## Macros publiques

Voir [Préprocesseur — Macros publiques](preprocessor.md#exported-macro-contract).

Les types imbriqués, les déclarations locales, les règles d’accès et les arguments génériques par segment sont décrits dans [Portées des types](scope.md).

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/modules.md) · [fr-FR](modules.md) · [ja-JP](../../ja-JP/language-basics/modules.md) · [ko-KR](../../ko-KR/language-basics/modules.md) · [zh-CN](../../zh-CN/language-basics/modules.md) · [zh-TW](../../zh-TW/language-basics/modules.md)
