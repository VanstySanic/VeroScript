# Module d'entrée de base

Fichier du module : `core.vs`

`core.vs` est le point d’entrée global de la bibliothèque standard. Il réexporte ces modules avec `export import` :

- `i18n.vs`
- `memory.vs`
- `math.vs`
- `algorithm.vs`
- `numeric.vs`
- `random.vs`
- `io.vs`
- `string.vs`
- `path.vs`
- `concurrent.vs`
- `attribute.vs`
- `callable.vs`
- `delegate.vs`

Importez les modules d’exécution ci-dessus avec :

```VeroScript
import "core.vs";
```

Des modules individuels peuvent également être importés directement pour rendre explicites les dépendances et la portée du nom. `core.vs` n'introduit pas d'API distincte ; il expose les déclarations publiques des autres modules de la bibliothèque standard.

Consultez les pages du module correspondantes pour les signatures des opérations de formatage, de fichier, de concurrence et d'événement.

- [Internationalisation des applications — std::i18n](i18n.md): `std::i18n`

Importez `compiler.vs` explicitement dans une extension de compilation. `core.vs` ne le réexporte pas ; son import dans un programme ordinaire ne donne aucune autorité de compilation.

[Extensions de compilation](compiler.md)

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/core.md) · [fr-FR](core.md) · [ja-JP](../../ja-JP/standard-library/core.md) · [ko-KR](../../ko-KR/standard-library/core.md) · [zh-CN](../../zh-CN/standard-library/core.md) · [zh-TW](../../zh-TW/standard-library/core.md)
