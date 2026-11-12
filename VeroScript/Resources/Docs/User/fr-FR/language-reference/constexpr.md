# constexpr

`constexpr` exige une valeur scalaire calculée à la compilation et implique `const`. Il s’applique aux variables locales, de module et d’espace de noms, ainsi qu’aux champs explicitement `static constexpr`.

Les fonctions libres, les fonctions d’espace de noms et les méthodes statiques peuvent être constexpr, y compris les modèles pris en charge. Les paramètres par valeur et le résultat non void doivent être bool, entiers de largeur fixe, char ou énumérations. Les variables scalaires initialisées, affectations, branches, boucles, switch et récursion bornée sont autorisés. Un appel avec des arguments d’exécution reste un appel ordinaire.

Les deux branches d’un if ordinaire sont vérifiées ; if constexpr ne vérifie que la branche choisie. L’évaluation ne peut appeler du code natif, lire un état global modifiable, construire des objets ou utiliser références, flottants, chaînes, conteneurs, exceptions ou tâches asynchrones. Un paramètre ordinaire ne peut initialiser une variable locale explicitement constexpr.

Initialisations constantes, arguments et valeurs par défaut de modèles, dimensions de tableaux, étiquettes case entières et valeurs d’énumération partagent l’évaluateur exact. Débordements, conversions invalides, divisions par zéro et dépassements de budget sont des erreurs. Le court-circuit ignore l’arithmétique des branches non sélectionnées.

Les déclarations exportées utilisent import. Les services de compilation et de création de `.vslib` conservent le contexte de définition et les dépendances privées pour une utilisation sans le projet source. Le chargement ne relance pas les processeurs de compilation.

L’hôte configure `FCompileOptions::ConstantEvaluationLimits` ou `SetConstantEvaluationLimits` du compilateur direct. Valeurs par défaut : 1 000 000 unités, 64 appels, profondeur d’expression/native 256, stockage temporaire 16 MiB, cache 8 MiB. Les limites sont positives ; zéro désactive le cache. Annulation et échéance restent actives ; les limites VM sont distinctes.

Les descripteurs de réflexion exposent `is_constexpr()` et les vues de `std::compiler` exposent `is_constexpr`. Une requête ne lance aucune évaluation. Core AOT partage le bytecode ; les exporteurs autonomes utilisent les littéraux typés vérifiés et conservent leurs restrictions sur les modèles.

```VeroScript
constexpr int square(int value)
{
	return value * value;
}
constexpr int capacity = square(8);

int main()
{
	constexpr int count = capacity + 1;
	int input = 3;
	print(count);         // 65
	print(square(input)); // 9
	return 0;
}
```

[constexpr.vs](../../../../Scripts/examples/constexpr.vs)

Les exportateurs autonomes conservent leurs limites existantes. C++ reçoit les dimensions de tableaux résolues ; les tableaux fixes refusés par C# ou Python et les constantes génériques propres à chaque instance utilisent Core AOT. Les arguments par défaut restent soumis aux règles existantes sur les littéraux et les valeurs de modèles.

## Branches if constexpr

La condition doit être une constante bool, entière, char ou une énumération non scoped ; une énumération scoped exige une conversion explicite. Les appels constexpr et les valeurs de modèle sont admis, contrairement aux paramètres ordinaires. Chaque instance choisit sa branche. Les deux branches doivent respecter la syntaxe, mais seule la branche choisie participe au contrôle des types, aux instanciations suivantes, à l’analyse des retours et des captures lambda. Elle conserve sa portée de bloc. Les fonctions ordinaires suivent la même règle ; un if ordinaire vérifie les deux branches.

```VeroScript
template<int N>
int sum()
{
	if constexpr (N == 0)
		return 0;
	else
		return N + sum<N - 1>();
}
```

Les fonctions constexpr acceptent aussi cette instruction ; leurs paramètres ordinaires ne deviennent pas des conditions constantes, même lors d’un appel constant. VM, JIT et Core AOT utilisent le même résultat choisi. L’export de modèles dépendant de l’instance conserve la limite existante : utiliser Core AOT.

Les membres non qualifiés des énumérations locales non scoped peuvent aussi servir de conditions constantes, avec les règles habituelles de masquage lexical. Une initialisation const évaluée tardivement conserve son environnement de déclaration. Les lambdas utilisent les constantes des énumérations englobantes sans capturer de stockage d'exécution ; les énumérations locales des modèles sont évaluées pour chaque instance concrète.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-reference/constexpr.md) · [fr-FR](constexpr.md) · [ja-JP](../../ja-JP/language-reference/constexpr.md) · [ko-KR](../../ko-KR/language-reference/constexpr.md) · [zh-CN](../../zh-CN/language-reference/constexpr.md) · [zh-TW](../../zh-TW/language-reference/constexpr.md)
