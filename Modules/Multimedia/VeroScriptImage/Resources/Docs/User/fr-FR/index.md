# Image

## Utiliser le service

`Image::IService::Decode` produit du RGBA8 sRGB compact à alpha droit ; un échec préserve le résultat précédent. `Image::Validate` vérifie dimensions et tampon. Par défaut, `FDecodeOptions` limite chaque dimension à 16 384, le nombre de pixels à 16 777 216 et la source à 64 Mio. Recherche des ressources, cache et transfert GPU restent à la charge de l’appelant. Contrat : `image` / `veroscript.image` / `0.1.0`.

Un fournisseur tel que `stb` réalise le décodage. Avec les liaisons Image enregistrées : `VS::Image::Service::Create("stb").Load("image.png")` ou `Decode(bytes)`. Tailles et indices de script utilisent `int64` ; les limites par défaut sont vérifiées avant la copie d’une chaîne trop grande. Le lecteur injecté résout les ressources ; sans lui, les chemins partent du répertoire courant du processus.

## SDK et intégration

```cmake
find_package(VeroScriptImage 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Image)
```

Le SDK natif fonctionne sans Core. Les liaisons de script et fournisseurs natifs facultatifs doivent correspondre à la compilation réelle de Core, à son ABI publique et à la chaîne d’outils ; la version du produit ne suffit pas. Installez aussi les dépendances natives et les composants Bindings demandés.

- [Image](index.md) · [Font](../../../../../VeroScriptFont/Resources/Docs/User/fr-FR/index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/fr-FR/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
