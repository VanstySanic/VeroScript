# Alias de type et les déclarations spéciales

VeroScript prend en charge les alias de type et utilise `= default` ou `= delete` pour rendre explicite l'état des constructeurs, des destructeurs, des méthodes, des opérateurs et des fonctions libres.

## Alias de type

Utilisez `using` pour donner un autre nom à un type existant :

```VeroScript
using Ints = array<int>;
using Scores = map<string, int>;

Ints values = Ints{1, 2, 3};
Scores scores = Scores();
```

`typedef` est également pris en charge pour les alias de type :

```VeroScript
typedef Names = array<string>;
Names names = Names();
```

Un alias de type ne crée pas de nouveau type d'exécution. Un alias peut être déclaré au niveau du module ou dans un bloc local ; un alias local n'est visible que dans sa portée.

## Exporter des alias

Utilisez `export using` ou `export typedef` au niveau du module, y compris dans un espace de noms. Les deux formes utilisent `Alias = Type`. Un alias ordinaire reste privé au module ; un alias local reste local. Le module importateur peut qualifier un alias exporté par son espace de noms. `export import` transmet les alias exportés sans exposer les alias privés. `using namespace` ne modifie que la recherche dans son module ou sa portée ; `export using namespace` est rejeté.

Enregistrez ce fichier sous aliases.vs :

```VeroScript
namespace Measures
{
	export using Count = int;
	export typedef Label = string;
	using InternalCount = int;
}
```

```VeroScript
import "aliases.vs";

int main()
{
	Measures::Count count = 42;
	Measures::Label label = "total";
	print(count);
	return 0;
}
```

Enregistrez le consommateur sous main.vs et exécutez `veroscript main.vs` ; il affiche `42`. Vous pouvez importer une façade contenant `export import "aliases.vs";`. Pour distribuer une bibliothèque binaire, exécutez `veroscript --emit-package aliases.vslib aliases.vs`, puis remplacez la cible importée par `"aliases.vslib"`.

## Déclarations par défaut

Un constructeur ou un destructeur peut utiliser `= default` pour demander une implémentation générée par le compilateur :

```VeroScript
struct Token
{
	int value = 7;

	Token() = default;
	~Token() = default;
};
```

La construction par défaut combine les valeurs par défaut des champs avec les règles par défaut normales du type. L'écriture de `= default` rend explicite l'intention d'implémentation générée et maintient la déclaration visible dans l'interface de type.

## Déclarations supprimées

Utilisez `= delete` pour conserver une signature tout en interdisant explicitement ce formulaire d'appel :

```VeroScript
struct Token
{
	Token() = default;
	Token(string text) = delete;

	int read() const
	{
		return 7;
	}
	int read(string mode) const = delete;
};
```

L’appel d’un constructeur, d’une méthode, d’un opérateur ou d’une fonction supprimé produit un diagnostic de compilation. Les déclarations supprimées sont utiles pour rejeter les appels implicites indésirables ou les surcharges incorrectes ; ils ne créent pas de fonction d'exécution vide.

`= default` et `= delete` font partie de la déclaration et ne peuvent pas être combinés avec un corps de fonction. Les fonctions virtuelles et les emplacements d'interface doivent toujours satisfaire aux règles d'héritage et d'implémentation.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/declarations.md) · [fr-FR](declarations.md) · [ja-JP](../../ja-JP/language-basics/declarations.md) · [ko-KR](../../ko-KR/language-basics/declarations.md) · [zh-CN](../../zh-CN/language-basics/declarations.md) · [zh-TW](../../zh-TW/language-basics/declarations.md)
