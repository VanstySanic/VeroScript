# Module de mathématiques

Fichier du module : `math.vs`

Importez-le avec :

```VeroScript
import "math.vs";
```

## Fonctions

| Fonction | Descriptif |
| --- | --- |
| `std::sqrt(double x)` | Renvoie la racine carrée. |
| `std::sin(double x)` | Renvoie le sinus. L'argument est en radians. |
| `std::cos(double x)` | Renvoie le cosinus. L'argument est en radians. |
| `std::tan(double x)` | Renvoie la tangente. L'argument est en radians. |
| `std::floor(double x)` | Arrondit vers l’infini négatif. |
| `std::ceil(double x)` | Arrondit vers l’infini positif. |
| `std::round(double x)` | Arrondit vers la valeur entière la plus proche ; les demi-valeurs s'éloignent de zéro. |
| `std::trunc(double x)` | Supprime la partie fractionnaire en arrondissant vers zéro. |
| `std::pow(double base, double exponent)` | Élève `base` à la puissance `exponent`. |
| `std::exp(double x)` | Renvoie l'exponentielle naturelle de `x`. |
| `std::log(double x)` | Renvoie le logarithme naturel de `x`. |
| `std::log2(double x)` | Renvoie le logarithme en base 2 de `x`. |
| `std::asin(double x)` / `std::acos(double x)` | Renvoie l'arc sinus ou l'arc cosinus en radians. |
| `std::atan2(double y, double x)` | Renvoie l'angle du point `(x, y)` en radians. |
| `std::isfinite(double x)` / `std::isnan(double x)` | Indique si la valeur est finie ou NaN. |
| `std::pi()` / `std::e()` | Renvoie les constantes mathématiques π ou e. |
| `std::clamp<T>(T x, T lo, T hi)` | Borne une valeur numérique de type `T` à `[lo, hi]`. |
| `std::clamp_int(int x, int lo, int hi)` | Fixe un entier à `[lo, hi]`. |

`std::clamp<T>` accepte les types numériques primitifs sauf bool ; des arguments de même type peuvent déduire `T`. Les bornes inversées ou NaN sont refusées ; une entrée NaN est conservée. `std::clamp_int` appelle la même opération pour int. Voir [la référence de base](foundation.md) pour les conversions.

Les valeurs absolues, minimales et maximales sont des éléments intégrés globaux fournis au moment de l'exécution plutôt que des exportations à partir de `math.vs`.


- [Fondations de la bibliothèque standard](foundation.md)

<a id="floating-results"></a>

Les erreurs de domaine flottant ou les dépassements peuvent produire NaN ou des infinis ; utilisez `std::isnan` / `std::isfinite` si le résultat doit être fini. L’orthographe et la présentation du signe de NaN dépendent de la plateforme ; ce texte ne constitue pas une identité portable.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/math.md) · [fr-FR](math.md) · [ja-JP](../../ja-JP/standard-library/math.md) · [ko-KR](../../ko-KR/standard-library/math.md) · [zh-CN](../../zh-CN/standard-library/math.md) · [zh-TW](../../zh-TW/standard-library/math.md)
