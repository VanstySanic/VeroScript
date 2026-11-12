# 14 : Utilisez l'espace de noms standard `std`

Ce didacticiel refactorise un petit script qui utilise des déclarations de module non qualifiées dans l'orthographe publique `std` recommandée. `std::` est un alias au niveau source pour les noms standard ; il ne crée pas de deuxième type de conteneur et ne modifie pas le comportement d'exécution.

## Étape 1 : préparer un script de partition

Créez `scores.vs` :

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	array<int> scores = {81, 92, 88};
	optional<int> selected = optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	print(format("selected={}, root={}", selected.get(), root));
	return 0;
}
```

Vérifiez et exécutez-le d'abord :

```powershell
veroscript --check scores.vs
veroscript scores.vs
```

La première version qualifie déjà la fonction mathématique appartenant au module. Les fonctions de la bibliothèque standard appartenant à un module ne sont accessibles que par leur espace de noms public `std` ; seule la petite surface des fonctions intégrées fondamentales reste globale.

## Étape 2 : migrer vers `std::`

Modifiez uniquement la qualification du nom, pas l'algorithme :

```VeroScript
import "math.vs";
import "string.vs";

int main()
{
	std::array<int> scores = {81, 92, 88};
	std::optional<int> selected = std::optional<int>(scores[1]);
	double root = std::sqrt(81.0);

	std::print(std::format("selected={}, root={}", selected.get(), root));
	std::print(std::strings::contains("scores", "core"));
	return 0;
}
```

Exécutez à nouveau les mêmes commandes de vérification et d’exécution. `std::array<int>` et `array<int>` font référence au même type de langage, et `std::print` et `print` font référence à la même fonction d'exécution. `std::` est résolu lors de la compilation et n’ajoute aucune répartition à l’exécution.

Les points d’entrée mathématiques, d’E/S, de système de fichiers, de chaînes, de chemins, de concurrence, de temps, de threads, d’environnement et de saisie typée sont réservés aux espaces de noms. Leurs formes non qualifiées, ainsi que les noms plats tels que `std::now`, ne sont pas fournis. Le code utilisateur peut toujours définir une fonction indépendante portant le même nom non qualifié.

## Étape 3 : comprendre les limites de l'importation

Un alias d'espace de noms standard n'est pas une importation de module. Les fonctions de la bibliothèque standard source nécessitent toujours le module qui les fournit :

- `math.vs` fournit `sqrt`, `sin`, `cos`, `tan`, `floor`, `ceil`, `round`, `trunc`, `pow`, `exp`, `log`, `log2`, `asin`, `acos`, `atan2`, `isfinite`, `isnan`, `pi`, `e`, `clamp` et `clamp_int`.
- `string.vs` fournit des requêtes de chaîne via `std::strings`, notamment `last_find`, `repeat`, `remove_prefix` et `remove_suffix`.
- `io.vs` fournit des opérations de flux via `std::io` et des opérations de système de fichiers via `std::filesystem`.
- `path.vs` ajoute des opérations de chemin à `std::filesystem` ; `concurrent.vs` utilise `std::concurrent`.

Les fonctions de console du runtime ne nécessitent pas d'importation. Utilisez
`std::console::write` et `std::console::error` pour une sortie sans nouvelle
ligne, `std::console::flush()` pour publier la sortie en attente et
`std::console::is_interactive()` pour détecter un terminal interactif.

Par exemple, la suppression de `import "math.vs";` n’oblige pas `std::sqrt` à charger automatiquement le module mathématique. Importez d'abord le module, puis utilisez le nom qualifié pour que la dépendance reste explicite.

## Étape 4 : migrer les conteneurs et les types de valeur

L'espace de noms standard couvre également les cartes, les ensembles, les files d'attente, les canaux, les tâches et les types d'informations de réflexion :

```VeroScript
int main()
{
	std::map<string, int> totals = {"math": 2, "io": 1};

	std::print("math={}, io={}", totals["math"], totals["io"]);
	return 0;
}
```

Utilisez l’orthographe groupée `std` de manière cohérente dans le nouveau code. Les déclarations d'implémentation restent non qualifiées à l'intérieur de leurs modules afin que la couche d'espace de noms puisse mapper chaque backend à la même identité de symbole.

## Étape 5 : expliciter la protection des valeurs et les conversions

L’exemple actuel combine également l’alias `std::readonly<T>` avec les intégrés de conversion globaux. `std::` ne préfixe pas chaque intégré : gardez `cast<T>`, `try_cast<T>` et `const_cast<T>` non qualifiés.

```VeroScript
std::readonly<int> published_score = std::readonly<int>(selected.get());
double exact_score = cast<double>(scores[1]);
std::optional<int> narrowed = try_cast<int>(exact_score);

const std::array<int> explicitly_unlocked = {13};
// N'utilisez cette suppression de const que si le stockage sous-jacent est connu comme modifiable.
const_cast<std::array<int>>(explicitly_unlocked).push(published_score.get());
```

`cast<T>` impose la conversion, tandis que `try_cast<T>` renvoie un `optional<T>` que l’appelant peut inspecter. `const_cast<T>` ne convertit pas une valeur et n’effectue pas de vérification de type à l’exécution ; il supprime uniquement l’accès `const` à la compilation et doit donc rester contrôlé. Les règles complètes sont dans [Conversions de type](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-reference/type-conversions.md).

## Noms qualifiés depuis la racine

Un `::` initial commence la recherche dans la portée globale. Il ignore l’espace de noms courant, les alias locaux et le repli de `using namespace` :

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

Utilisez `::Name` lorsque la déclaration racine est intentionnelle. `std::readonly<T>` est également la forme de l’espace de noms standard pour le wrapper `readonly<T>` ; il s’agit d’un alias source, pas d’un second type d’exécution.


## Exercices

1. Ne remplacez pas l'exemple par `std::unordered_map` ; consultez le tableau des alias complet et expliquez quel conteneur pris en charge est approprié à la place.
2. Ajoutez un `std::set<int>` et utilisez-le pour supprimer les scores en double.
3. Importez `string.vs` et appelez `std::strings::contains` ou `std::strings::starts_with`.

Consultez le tableau complet [Espace de noms standard `std`](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/standard-namespace.md). L'exemple exécutable est [`examples/standard_namespace.vs`](../../../../../VeroScript/Resources/Scripts/examples/standard_namespace.vs).

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/advanced/14-standard-namespace.md) · [fr-FR](14-standard-namespace.md) · [ja-JP](../../ja-JP/advanced/14-standard-namespace.md) · [ko-KR](../../ko-KR/advanced/14-standard-namespace.md) · [zh-CN](../../zh-CN/advanced/14-standard-namespace.md) · [zh-TW](../../zh-TW/advanced/14-standard-namespace.md)
