# Référence de la bibliothèque standard

La bibliothèque standard VeroScript fournit des modules pour les opérations courantes sur les chaînes, les mathématiques, les fichiers, les chemins, la concurrence et les événements. Un script peut importer `core.vs` pour les modules d’exécution regroupés ou importer uniquement les modules dont il a besoin.

Appelez les fonctions propres aux modules avec leur espace de noms public complet, par exemple `std::strings::join` ou `std::concurrent::spawn`. Les noms globaux non qualifiés sont refusés. Les types de base et les fonctions directes comme `array<T>` et `print` acceptent leurs noms canoniques. Un alias ne remplace pas l’import du module requis. [std](../language-basics/standard-namespace.md).

- [Mémoire gérée](memory.md)：`std::memory`

- [Attributs et fonctions liées aux méthodes](attribute.md)：`std::attribute<T>` · `std::bind_method<F>`

## Index des modules

- [Fonctions intégrées](builtins.md) : sorties, conversions, fonctions numériques et environnement du runtime ; les entrées exigent io.vs.
- [Point d'entrée du noyau](core.md) : réexporte les modules d’exécution indiqués dans cette référence ; compiler.vs nécessite un import explicite.
- [Chaînes](string.md) : recherche, découpage, fractionnement, jointure, suppression des espaces, conversion de casse, remplacement et formatage.
- [Math](math.md) : trigonométrie, racines carrées, arrondi et serrage.
- [Algorithmes](algorithm.md) : tri, inversion, recherche et comptage génériques des tableaux.
- [Numérique](numeric.md) : accumulation, somme et moyenne.
- [Aléatoire](random.md) : flux pseudo-aléatoires déterministes avec état explicite.
- [I/O](io.md) : fichiers, répertoires, entrée standard et répertoire de travail.
- [Chemins](path.md) : noms de fichiers, noms de répertoires, extensions et jonction de chemins.
- [Concurrency](concurrent.md) : canaux, tâches isolées, tâches en attente et annulation coopérative.
- [Événements](events.md) : délégués unicast et multicast.

## Conventions d'importation

Importez la bibliothèque complète :

```VeroScript
import "core.vs";
```

Ou importez des modules individuels :

```VeroScript
import "string.vs";
import "path.vs";
```

Les fonctions de bibliothèque standard sont normalement des déclarations exportées. Leurs paramètres et types de retour sont définis par l’implémentation du module correspondant ; les opérations impliquant des fichiers, des threads ou l'environnement hôte peuvent également dépendre des autorisations d'exécution et de la configuration de l'hôte.

- [Internationalisation des applications — std::i18n](i18n.md): `std::i18n`

Importez `compiler.vs` explicitement dans une extension de compilation. `core.vs` ne le réexporte pas ; son import dans un programme ordinaire ne donne aucune autorité de compilation.

[Extensions de compilation](compiler.md)


- [Fondations de la bibliothèque standard](foundation.md)

- [variant — Boîte de valeur dynamique](variant.md)
<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/index.md) · [fr-FR](index.md) · [ja-JP](../../ja-JP/standard-library/index.md) · [ko-KR](../../ko-KR/standard-library/index.md) · [zh-CN](../../zh-CN/standard-library/index.md) · [zh-TW](../../zh-TW/standard-library/index.md)
