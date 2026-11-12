# Agrégation numérique

Fichier du module : `numeric.vs`

`std::numeric::accumulate(values, initial)` accepte une valeur initiale explicite et fonctionne avec un tableau vide. `sum` et `average` nécessitent un tableau non vide et déclenchent une assertion sinon. `average` convertit chaque élément en `double`.
`average` utilise une moyenne progressive pour éviter le débordement d’une somme intermédiaire de valeurs finies ; le résultat reste soumis à l’arrondi flottant.


- [Fondations de la bibliothèque standard](foundation.md)


## Entiers exacts et nombres rationnels

Importez `bigint.vs`, `rational.vs` ou `core.vs`. Le catalogue natif du noyau fournit les types. Les valeurs sont immuables et copiées par valeur, y compris dans les conteneurs, champs, paramètres et `std::attribute<T>`.

La division entière tronque vers zéro ; le reste conserve le signe du dividende. Le décalage droit est arithmétique et les opérations binaires suivent le complément à deux infini. Les fractions sont réduites avec un dénominateur positif ; le texte canonique est `numérateur/dénominateur`, ou le seul numérateur pour les entiers (zéro : `0`). Aucune conversion implicite depuis un flottant, bool, enum ou entier ordinaire n’est ajoutée.

Utilisez les constructeurs explicites entier/chaîne, `parse`, `try_parse` et les conversions explicites. Les entiers conservent leurs 64 bits. `from_double_exact` utilise la valeur binaire stockée : 0.1 diffère de 1/10. `to_double`/`to_float` arrondissent directement au format cible, au pair en cas d’égalité ; `_exact` refuse les pertes. Les fractions doivent être arrondies avant une conversion entière. Les échecs ordinaires de `try_` préservent la sortie ; limites, annulation et allocations restent des erreurs.

`std::numeric::gcd`, `lcm`, `divmod` et `pow` sont exacts. Les exposants entiers doivent être positifs ou nuls ; les puissances rationnelles acceptent les exposants négatifs. Les comptes et bases exigent des entiers vérifiés avant réduction. `sum`/`accumulate` restent exacts ; `average` utilise double et est approximatif. Le formatage canonique respecte les limites numériques.

`round_with_mode` et `to_decimal_with_mode` acceptent `std::numeric::rounding_mode::{toward_zero, floor, ceil, away_from_zero, half_even, half_away_from_zero}`. Le mode par défaut est half_even. Le nombre de décimales demandé est conservé. Ces opérations nécessitent le runtime natif ; l’export AOT autonome les refuse actuellement.

```VeroScript
import "rational.vs";

std::bigint n = std::bigint("18446744073709551616");
auto qr = std::numeric::divmod(n, std::bigint(3));
assert(qr.first * std::bigint(3) + qr.second == n);
std::rational r = std::rational(1, 3) + std::rational(1, 6);
assert(r.to_string() == "1/2");
assert(r.to_decimal(4) == "0.5000");
```

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/numeric.md) · [fr-FR](numeric.md) · [ja-JP](../../ja-JP/standard-library/numeric.md) · [ko-KR](../../ko-KR/standard-library/numeric.md) · [zh-CN](../../zh-CN/standard-library/numeric.md) · [zh-TW](../../zh-TW/standard-library/numeric.md)
