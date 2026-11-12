# Flux aléatoires déterministes

Fichier du module : `random.vs`

Créez un état indépendant avec `std::random::make_engine(seed)`, puis
transmettez-le aux fonctions du module. Aucun générateur global au processus
n'est utilisé, ce qui rend les tests reproductibles et évite la contention
entre sessions. Le tableau `array<unsigned int>` à un élément renvoyé doit être
traité comme un état opaque et réutilisé tel quel.

## API

| Fonction | Comportement |
| --- | --- |
| `std::random::make_engine(seed)` | Crée un état indépendant ; une graine nulle utilise l'état initial non nul documenté. |
| `std::random::seed(engine, seed)` | Réinitialise l'état fourni, en créant son élément s'il manque. |
| `std::random::next_uint(engine)` | Avance l'état et renvoie une valeur entière non signée. |
| `std::random::next_bounded_uint(engine, upper_exclusive)` | Renvoie une valeur dans `[0, upper_exclusive)` ; la borne doit être positive. |
| `std::random::next_int(engine, minimum, maximum_exclusive)` | Renvoie une valeur dans `[minimum, maximum_exclusive)` ; l'intervalle doit être non vide. |
| `std::random::next_double(engine)` | Renvoie une valeur dans `[0, 1)`. |
| `std::random::next_double_range(engine, minimum, maximum_exclusive)` | Renvoie une valeur dans l'intervalle flottant semi-ouvert. |
| `std::random::next_bool(engine)` | Renvoie un booléen pseudo-aléatoire. |

La génération bornée utilise un échantillonnage par rejet afin d'éviter le
biais de modulo.
L’échantillonnage utilise les `2^32-1` sorties non nulles réellement accessibles à xorshift32. Ne modifiez pas directement le tableau d’état : `std::random::next_uint` rejette l’état nul, que `std::random::seed` peut rétablir. Ce générateur ne convient pas à la cryptographie. `std::random::next_double_range` exige des bornes finies et strictement croissantes ; il rééchantillonne si l’arrondi atteint la borne supérieure exclue.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/random.md) · [fr-FR](random.md) · [ja-JP](../../ja-JP/standard-library/random.md) · [ko-KR](../../ko-KR/standard-library/random.md) · [zh-CN](../../zh-CN/standard-library/random.md) · [zh-TW](../../zh-TW/standard-library/random.md)
