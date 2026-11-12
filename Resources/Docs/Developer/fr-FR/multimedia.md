# Maintenance multimédia

Ce guide commun définit les frontières des fournisseurs et la procédure de vérification des modules multimédias. Le comportement public et les API restent dans les guides User ; les détails d’implémentation restent dans les guides Developer de chaque module.

Les en-têtes publics ne doivent pas exposer de types tiers. Le contrat générique appartient au module Media ; les bibliothèques concrètes et notices originales appartiennent au moteur choisi. Préservez erreurs, sorties et durée de vie. Après un changement de contrat ou d’identité Core, reconstruisez fournisseurs et consommateurs ensemble.

Lorsque l’exécution est autorisée, utilisez l’entrée CMake du module et ses tests déclarés. Vérifiez séparément les consommateurs CMake installés, les liaisons/fournisseurs facultatifs et chaque plateforme annoncée. Les contrôles statiques ne certifient ni rendu, polices, périphériques, cibles mobiles ni intégration IDE. Gardez les mesures temporaires hors des guides User.

Conservez les licences originales et inventaires de sources épinglées des dépendances redistribuées. Les textes juridiques tiers restent dans leur langue originale.

## Guides des modules

| Module | Utilisation | Maintenance |
| --- | --- | --- |
| VeroScriptPlatform | [Utilisateur](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/fr-FR/index.md) |
| VeroScriptGraphics | [Utilisateur](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/fr-FR/index.md) |
| VeroScriptAudio | [Utilisateur](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/fr-FR/index.md) |
| VeroScriptImage | [Utilisateur](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/fr-FR/index.md) |
| VeroScriptFont | [Utilisateur](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/fr-FR/index.md) |
| VeroScriptGui | [Utilisateur](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/fr-FR/index.md) |
| VeroScriptSdl3 | [Utilisateur](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/fr-FR/index.md) |
| VeroScriptStb | [Utilisateur](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/fr-FR/index.md) |
| VeroScriptFontBackend | [Utilisateur](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/fr-FR/index.md) |
| VeroScriptRmlUi | [Utilisateur](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/User/fr-FR/index.md) | [Développeur](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/fr-FR/index.md) |

L’installation des composants agrégés GUI/foundation utilise les blocs CMake 3.25+ `block(SCOPE_FOR VARIABLES ...)`. Restaurez chaque sélection imbriquée en propageant `CMAKE_INSTALL_MANIFEST_FILES` et `CMAKE_ABSOLUTE_DESTINATION_FILES`, pour conserver dépendances et inventaire entre Gui/RmlUi/ressources. Consommateurs SDK installés et filtrage de release restent des contrôles séparés.

<!-- docs-navigation -->

---

[Maintenance documentaire](index.md)

[en-US](../en-US/multimedia.md) · [fr-FR](multimedia.md) · [ja-JP](../ja-JP/multimedia.md) · [ko-KR](../ko-KR/multimedia.md) · [zh-CN](../zh-CN/multimedia.md) · [zh-TW](../zh-TW/multimedia.md)
