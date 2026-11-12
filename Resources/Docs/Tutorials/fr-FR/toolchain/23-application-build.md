# Construire bibliothèque et application

Prérequis : CLI compatible avec services Build/Application, stockage accessible en écriture et exemple hello distribué. Contenu de script seulement, sans fournisseur natif ni service réseau.

1. Copiez tout le dossier hello lié vers un chemin absolu accessible comme /absolute/hello, en gardant src/package. Inspectez le plan puis construisez la cible application seule ; sa bibliothèque se construit automatiquement. Spécifiez le profil release.

   [hello](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)

```sh
veroscript build --path /absolute/hello --profile release --target application --plan --json
veroscript build --path /absolute/hello --profile release --target application
veroscript /absolute/hello/dist/release/portable/application/sample.vsapp
```

2. Le plan décrit les dépendances sans exécuter d’étapes personnalisées. La construction réussie produit dist/release/portable/application/sample.vsapp et la bibliothèque. L’application affiche result: 42 et rend 0. Corrigez tout échec avant exécution ; produire un fichier ne prouve pas son exécution.

3. Remplacez la réponse de bibliothèque par 43, recréez le plan et reconstruisez : result: 43 attendu. Ne modifiez pas le conteneur généré, gardez sources/configuration comme entrées.

[Build](../../../../../VeroScriptBuild/Resources/Docs/User/fr-FR/index.md) · [Application](../../../../../VeroScriptApplication/Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)

Remplacez /absolute/hello par un chemin absolu de votre système, par exemple C:/work/hello sous Windows.

## Entrées sources

- [veroscript-build.json](../../../../../VeroScriptBuild/Resources/Examples/hello/veroscript-build.json)
- [src/main.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/main.vs)
- [src/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/src/library.vs)
- [package/veroscript-package.json](../../../../../VeroScriptBuild/Resources/Examples/hello/package/veroscript-package.json)
- [package/scripts/library.vs](../../../../../VeroScriptBuild/Resources/Examples/hello/package/scripts/library.vs)

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/toolchain/23-application-build.md) · [fr-FR](23-application-build.md) · [ja-JP](../../ja-JP/toolchain/23-application-build.md) · [ko-KR](../../ko-KR/toolchain/23-application-build.md) · [zh-CN](../../zh-CN/toolchain/23-application-build.md) · [zh-TW](../../zh-TW/toolchain/23-application-build.md)
