# Build central et ressources d’exécution

[Backends VM remplaçables](vm-backends.md)

## Cible de compilation

Voir [Backends VM remplaçables](vm-backends.md#vm-provider-assembly).

## Ressources d’exécution

La cible dépend de `VeroScriptRuntimeAssets`. Elle utilise les profils partagés de `Resources/Tools/CMake/VeroScriptLocalization.cmake`, supprime les catalogues JSON obsolètes, copie les catalogues modifiés et copie les fichiers de bibliothèque standard vers la sortie d’exécution. Cette dépendance s’applique aussi aux cibles de test individuelles afin que leurs catalogues correspondent aux sources actuelles.

Le chargeur essaie `VERO_LOCALIZATION_DIR`, puis les racines déployées et du dépôt ; l’ordre exact figure dans [Localisation des diagnostics](localization-and-resources.md). Une arborescence valide exige la référence `en-US`, des cultures BCP 47 canoniques qualifiées et des identifiants uniques. Les fichiers et clés traduits peuvent être des sous-ensembles de l’anglais ; les éléments absents utilisent cette référence. Modules et bibliothèque standard peuvent fusionner des catalogues valides sans écraser les identifiants existants. Les langues disponibles sont découvertes dans les ressources chargées.

## Compatibilité binaire

La compatibilité du bytecode et des paquets est indépendante de l’activation d’asmjit. Un backend optionnel ne doit pas modifier les contrats publiés des ressources, diagnostics ou formats de module.

## Infrastructure de construction et extension des catalogues sources

Le dépôt autonome possède les utilitaires sous `Resources/Tools/CMake` ; utilisez ces chemins depuis sa racine, sans préfixe `Script/`. L’API locale publique déclare `InitializeModuleFromEnvironment`, avec un chemin relatif au module source découvert, par exemple `VeroScriptTooling` ou `Extensions/Language/Cpp/VeroScriptCpp`. Elle fusionne sans remplacer les ID existants uniquement si le cœur a choisi les catalogues sources. Un remplacement explicite efface cet état de découverte. `VeroScriptLocaleSourceTests` vérifie la découverte après renommage et l’ajout des catalogues de modules.

- [Ressources de localisation et déploiement](localization-and-resources.md)

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/build-and-assets.md) · [fr-FR](build-and-assets.md) · [ja-JP](../ja-JP/build-and-assets.md) · [ko-KR](../ko-KR/build-and-assets.md) · [zh-CN](../zh-CN/build-and-assets.md) · [zh-TW](../zh-TW/build-and-assets.md)
