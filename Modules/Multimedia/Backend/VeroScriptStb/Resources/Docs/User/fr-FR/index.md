# Stb

Les échantillons PGM/PPM binaires sont normalisés en RGBA8 selon `Maxval` (1–65535). Une valeur maximale invalide ou un échantillon hors plage provoque un échec sans modifier la sortie. La quantification et l’ordre des octets des entrées à pleine échelle 255/65535 restent inchangés.

## Utiliser le service

`Stb::CreateImageService()` fournit `image/stb`. PNG, JPEG, BMP, TGA et les autres formats stb_image activés produisent du RGBA8 sRGB à alpha droit ; GIF donne la première image. Les sorties HDR/flottantes sont désactivées. BMP descendant, ordre des octets PGM/PPM 16 bits et PNG CgBI sont normalisés. Un échec préserve la sortie et les limites explicites s’appliquent.

Les appels concurrents utilisent sorties/erreurs séparées et une entrée inchangée durant l’appel. Sans stockage local au thread du décodeur, le décodage est sérialisé entre instances. L’API de script `VS::Image` vient des liaisons Image ; `VeroScriptStbModule` fournit seulement le moteur. Installez Image, Stb et les composants Bindings nécessaires.

## SDK et intégration

```cmake
find_package(VeroScriptStb 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Stb)
```

Le SDK natif fonctionne sans Core. Les liaisons de script et fournisseurs natifs facultatifs doivent correspondre à la compilation réelle de Core, à son ABI publique et à la chaîne d’outils ; la version du produit ne suffit pas. Installez aussi les dépendances natives et les composants Bindings demandés.

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/fr-FR/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/fr-FR/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/fr-FR/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
