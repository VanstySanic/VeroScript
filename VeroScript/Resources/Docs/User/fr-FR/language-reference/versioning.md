# Versionnement et compatibilité


Les informations de version affectent les fichiers sources, les caches de bytecode, les paquets d'application, les paquets de bibliothèque et les liaisons d'hôte. Un déploiement doit utiliser des versions de langage et d’exécution mutuellement compatibles pour tous ces composants.

Inspectez la version installée avec :

```powershell
veroscript --version
```

## Compatibilité des formats de fichiers

Le produit, le bytecode, l'ABI Interop et l'ABI WebAssembly utilisent la version initiale `0.1.1`. Le chargement du bytecode exige une version de format et un identifiant magique exactement identiques. Reconstruisez depuis les sources les fichiers `.vsbc`, `.vsapp` et `.vslib` des anciennes versions de développement ; ces anciens binaires ne sont pas compatibles.

- `.vs` est source et doit normalement être revérifié par le compilateur correspondant ;
- `.vsbc` est un cache de bytecode et ne doit pas être traité comme un format de distribution multi-versions ;
- `.vsapp` est un paquet d'application exécutable et doit correspondre à son environnement d'exécution et à sa version de paquet ;
- `.vslib` est un paquet de bibliothèque dont les déclarations publiques, la version du paquet et le runtime doivent rester compatibles.

Après une mise à niveau de version, exécutez à nouveau `--check`, reconstruisez les paquets et régénérez ou validez les artefacts natifs lors de l'utilisation d'AOT ou de liaisons d'hôte.

Les API actuelles de longueur et d’indice linéaire utilisent `int64`. Reconstruisez les paquets binaires et caches de bytecode produits avant ce changement, même si `--version` indique la même version du produit. Utilisez la bibliothèque standard correspondante et régénérez les sorties AOT lors d’une mise à niveau.

## Limites linguistiques

L'implémentation actuelle ne fournit pas de sémantique de pointeur de script ni de syntaxe de concepts C++ autonomes ; `__VA_OPT__` pour les macros variadiques, les captures de références lambda, les retours de référence et la réflexion d'exécution sont des fonctionnalités publiques. Les interfaces utilisateur graphiques sont en dehors du runtime principal. Traitez la liste des fonctionnalités implémentées et les notes de version comme faisant autorité au lieu de supposer que la syntaxe C++ est automatiquement disponible.

## Compatibilité de liaison d'hôte

Les liaisons d'hôte dépendent non seulement des noms de fonctions, mais également des types de paramètres, des types de retour, des types d'éléments de conteneur, des types de clé/valeur et de la durée de vie de l'objet hôte. Lorsque ces limites changent, mettez à jour ensemble le catalogue de liaisons, le code hôte et les appelants de script, puis répétez la vérification et la validation d'exécution.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-reference/versioning.md) · [fr-FR](versioning.md) · [ja-JP](../../ja-JP/language-reference/versioning.md) · [ko-KR](../../ko-KR/language-reference/versioning.md) · [zh-CN](../../zh-CN/language-reference/versioning.md) · [zh-TW](../../zh-TW/language-reference/versioning.md)
