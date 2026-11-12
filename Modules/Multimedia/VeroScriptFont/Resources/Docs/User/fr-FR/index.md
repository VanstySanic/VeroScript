# Font

## Utiliser le service

`Font::IService` conserve les octets des polices dans les faces ; `IFace` expose métadonnées, métriques, mise en forme et rastérisation. Les positions sont des pixels signés en 26.6 et les clusters des offsets d’octets UTF-8 originaux. `FShapeOptions` utilise par défaut 16 pixels, direction automatique, crénage actif et toute la plage. Les bitmaps sont A8 ou RGBA8 sRGB prémultiplié, contrairement à Image. Contrat : `font` / `veroscript.font` / `0.1.2`.

Passez explicitement ITextLayoutService en premier argument de FTextLayout::Create(service,text,direction,error). Font possède les interfaces génériques ; FontBackend fournit CreateTextLayoutService() avec SheenBidi 3.0.0 / Unicode 17.0.0. Contrat : text-layout / veroscript.text-layout / 0.1.0. La clé de cache doit garder l’identité d’instance du service, pas seulement son ID moteur.

`FTextLayout::Create(service, text, direction, error)` analyse le texte logique immuable par le service de disposition explicitement choisi. `GetVisualRuns` réordonne chaque ligne ; ses bornes UTF-8 doivent rester dans un seul paragraphe. `ShapeLine` garde le contexte et fournit clusters visuels et positions de curseur. L’appelant choisit toujours les coupures et gère atlas/GPU. `ShapeWithFallback` conserve les faces retenues. Lato Latin est enregistré comme `Vero Sans` ; les autres écritures demandent des polices adaptées. Les scripts utilisent `VS::Font::Service::Create("font-backend").LoadDefault()` ; leurs positions sont en pixels.

## SDK et intégration

```cmake
find_package(VeroScriptFont 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Font)
```

Le SDK natif fonctionne sans Core. Les liaisons de script et fournisseurs natifs facultatifs doivent correspondre à la compilation réelle de Core, à son ABI publique et à la chaîne d’outils ; la version du produit ne suffit pas. Installez aussi les dépendances natives et les composants Bindings demandés.

- [Image](../../../../../VeroScriptImage/Resources/Docs/User/fr-FR/index.md) · [Font](index.md)
- [GUI](../../../../../VeroScriptGui/Resources/Docs/User/fr-FR/index.md)

ShapeLine applique l’espacement une fois sur le dernier glyphe de chaque cluster visible ; les métriques retournées conservent leurs avances initiales. Le X d’un curseur est la position logique finale du stylo, et non le bord d’une boîte visuelle. Un espacement négatif peut placer cette position à l’intérieur de l’enveloppe du cluster.

<a id="font-layout-boundaries"></a>

La direction automatique suit le premier caractère fort du paragraphe ; un sens explicite fixe sa base. Les segments visuels suivent UAX #9 L1/L2 après retour à la ligne. `FTextLine` expose glyphes visuels, plages d’octets logiques et positions de curseur ; une frontière de direction peut avoir deux positions choisies par affinité. Les groupes invisibles de contrôle gardent leurs extrémités logiques sans glyphe ni espacement. Conservez texte du presse-papiers et offsets dans l’ordre logique. Les curseurs internes aux ligatures, la navigation complète par graphèmes, les retours et l’alignement restent à la charge de l’hôte.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
