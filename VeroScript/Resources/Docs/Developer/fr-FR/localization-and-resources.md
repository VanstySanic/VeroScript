# Ressources de localisation et déploiement

Cette page s’adresse aux mainteneurs des catalogues et des outils de compilation et de publication. Pour choisir une langue, charger des catalogues ou utiliser les SDK, voir [Localisation des diagnostics](../../User/fr-FR/localization.md).

## Propriété des sources et déploiement

Les catalogues sources appartiennent à leur module ou zone, dans `Resources/Localization/<culture>/`. Seuls les messages utilisés par plusieurs outils Python restent dans la zone Python commune. La compilation et le packaging réunissent les catalogues nécessaires aux dépendances dans le répertoire déployé `localization/`. Runtime/Core SDK inclut les dépendances CLI, Tooling, LSP, DAP et Interop ; WebAssembly inclut le cœur et C++ ; les paquets Python incluent le cœur, C++, Interop et les outils Python. Les catalogues UI de l’IDE et de l’installateur restent indépendants.

```text
VeroScript/Resources/Localization/<culture>/
  language.json, runtime.json, shared.json, host-binding.json, memory.json, i18n.json
Extensions/Language/Cpp/VeroScriptCpp/Resources/Localization/<culture>/embedding.json
VeroScriptInterop/Resources/Localization/<culture>/interop.json
VeroScriptTooling/Resources/Localization/<culture>/
  tooling.json, lsp.json, dap.json
Extensions/Language/Python/Resources/Localization/<culture>/python-tools.json
Extensions/Language/Python/VeroScriptPythonFrontend/Resources/Localization/<culture>/python-frontend.json
Extensions/Language/Python/VeroScriptPythonBindGen/Resources/Localization/<culture>/python-bindgen.json
Extensions/Language/Python/VeroScriptPythonTranspiler/Resources/Localization/<culture>/python-transpiler.json
Extensions/Language/Python/VeroScriptPythonAot/Resources/Localization/<culture>/python-aot.json
VeroScriptInstaller/App/Resources/Localization/<culture>/
```

Lors d’une exécution directe depuis les sources, le cœur découvre ses propres catalogues et chaque module SDK ou outil ajoute son répertoire Resources/Localization. Les catalogues chargés explicitement et ceux déployés restent la référence ; un remplacement réussi ne reçoit pas automatiquement les ressources des sources.

## Recherche des ressources sources

`VERO_LOCALIZATION_DIR` sélectionne une racine de localisation complète. Si elle est absente ou invalide, le cœur essaie les emplacements suivants dans l’ordre et s’arrête à la première arborescence valide :

1. `localization` à côté de l’exécutable, puis `../assets/localization` depuis le répertoire de l’exécutable ;
2. jusqu’à huit ancêtres du répertoire de l’exécutable : le dossier `localization` de chacun, puis les catalogues du cœur d’un module source découvert dans cet ancêtre ou parmi ses enfants directs de dépôt ;
3. `localization` dans le répertoire de travail ;
4. découverte du module source dans le répertoire courant, son parent, puis son grand-parent ; un module découvert fournit `VeroScript/Resources/Localization`.

La découverte accepte un module contenant `VeroScript/Public/VeroScriptVersion.h`, ou un dépôt contenant `CMakeLists.txt` avec exactement un tel enfant direct. Le répertoire extérieur peut être renommé ; un module absent ou ambigu est ignoré. `InitializeModuleFromEnvironment` ajoute un arbre `Resources/Localization` relatif au module source uniquement après une initialisation depuis les catalogues sources. Les arbres explicitement chargés ou déployés conservent leur propre ensemble de ressources.

## Vérifications de publication

- Chaque distribution doit inclure un répertoire `en-US` complet et valide ;
- chaque langue supplémentaire requise par une version doit être copiée, intégrée ou empaquetée avec l'hôte cible ;
- les chemins culturels et les identifiants de message traduits doivent être des sous-ensembles de la référence anglaise ;
- n'utilisez pas l'ancienne disposition `en.json`/`zh-CN.json` axée sur le domaine ; le format actuel est `<culture>/<component>.json` ;
- les ID de diagnostic sont plus stables que le texte affiché, donc les hôtes, LSP et DAP doivent utiliser `MessageId` pour la logique ;
- après l'emballage, vérifiez le jeu de langues disponible, l'anglais de secours pour les clés manquantes et la substitution d'espace réservé de diagnostic.


## Ressources et localisation mobiles

L’inventaire commun des catalogues mobiles, leurs chemins de déploiement et leur chargement explicite sont décrits dans [Mobile](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/fr-FR/index.md#mobile-localization).

La propriété des tampons traduits, les erreurs et la maintenance des exports sont définies dans [Interop](../../../../../VeroScriptInterop/Resources/Docs/Developer/fr-FR/abi-and-lifetime.md#localization-abi).

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/localization-and-resources.md) · [fr-FR](localization-and-resources.md) · [ja-JP](../ja-JP/localization-and-resources.md) · [ko-KR](../ko-KR/localization-and-resources.md) · [zh-CN](../zh-CN/localization-and-resources.md) · [zh-TW](../zh-TW/localization-and-resources.md)
