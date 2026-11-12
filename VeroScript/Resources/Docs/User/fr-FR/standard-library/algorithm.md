# Algorithmes

Fichier du module : `algorithm.vs`

Importez le module et utilisez ses API qualifiées par espace de noms. Les
opérations sont génériques et sur place, sauf `copy` qui renvoie un nouveau
tableau. Les comparaisons d'ordre exigent `<` et celles d'égalité `==`.

| Fonction | Comportement |
| --- | --- |
| `sort`, `reverse` | Trie ou inverse le tableau sur place. |
| `find_index`, `count`, `contains` | Recherche, compte ou teste une valeur ; `find_index` renvoie `-1` si elle manque. |
| `lower_bound`, `binary_search` | Recherche dans un tableau déjà trié. |
| `fill`, `copy`, `equal` | Remplit, copie vers un nouveau tableau ou compare deux tableaux. |
| `is_sorted` | Vérifie l'ordre croissant. |
| `min_index`, `max_index` | Renvoie l'indice du premier minimum/maximum, ou `-1` pour un tableau vide. |
| `rotate` | Place l'élément `middle` en première position ; les bornes sont valides. |
| `shuffle` | Mélange sur place avec l'état explicite de `random.vs`. |

`lower_bound` et `binary_search` exigent un tableau trié. Les tableaux sont des
valeurs de conteneur partagées ; les opérations sur place modifient l'appelant.

## Signatures complètes

| Fonction | Signature |
| --- | --- |
| `sort` | `std::algorithm::sort<T>(array<T> values)` |
| `reverse` | `std::algorithm::reverse<T>(array<T> values)` |
| `find_index` | `std::algorithm::find_index<T>(array<T> values, T value)` |
| `count` | `std::algorithm::count<T>(array<T> values, T value)` |
| `lower_bound` | `std::algorithm::lower_bound<T>(array<T> values, T value)` |
| `binary_search` | `std::algorithm::binary_search<T>(array<T> values, T value)` |
| `fill` | `std::algorithm::fill<T>(array<T> values, T value)` |
| `copy` | `std::algorithm::copy<T>(array<T> values)` |
| `contains` | `std::algorithm::contains<T>(array<T> values, T value)` |
| `equal` | `std::algorithm::equal<T>(array<T> left, array<T> right)` |
| `is_sorted` | `std::algorithm::is_sorted<T>(array<T> values)` |
| `min_index` | `std::algorithm::min_index<T>(array<T> values)` |
| `max_index` | `std::algorithm::max_index<T>(array<T> values)` |
| `rotate` | `std::algorithm::rotate<T>(array<T> values, int64 middle)` |
| `shuffle` | `std::algorithm::shuffle<T>(array<T> values, array<unsigned int> engine)` |

Exemple :

```VeroScript
import "algorithm.vs";

array<int> values = {9, 2, 7, 2, 5};
std::algorithm::sort(values);
bool found = std::algorithm::binary_search(values, 7);
```

Les algorithmes ordonnés exigent que `<` définisse un ordre strict faible ; un NaN flottant n’est pas une entrée valide. `binary_search` utilise l’équivalence selon `<` ; les algorithmes ordonnés n’exigent donc pas `==`.

`find_index`, `count`, `lower_bound`, `min_index` et `max_index` renvoient `int64`. `lower_bound` peut renvoyer `len(values)` ; l’absence de résultat pour `find_index`, `min_index` et `max_index` est indiquée par `-1`. L’argument `middle` de `rotate` est aussi de type `int64`.


- [Fondations de la bibliothèque standard](foundation.md)

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/algorithm.md) · [fr-FR](algorithm.md) · [ja-JP](../../ja-JP/standard-library/algorithm.md) · [ko-KR](../../ko-KR/standard-library/algorithm.md) · [zh-CN](../../zh-CN/standard-library/algorithm.md) · [zh-TW](../../zh-TW/standard-library/algorithm.md)
