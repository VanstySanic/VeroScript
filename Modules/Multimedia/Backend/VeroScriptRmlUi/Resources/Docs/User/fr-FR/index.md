# RmlUi

## Utiliser le service

`RmlUi::CreateBackend` adapte les arbres GUI au moteur Graphics. Fournissez les services de polices et de disposition du texte et, pour les images sans décodeur personnalisé, Image. Le rendu emprunte un périphérique et exactement une surface/texture ; le mode explicite de mise en page seule n’a pas besoin de cible de rendu. Synchronisez puis mettez à jour avec succès avant de rendre. Service GUI : `gui` / `veroscript.gui` ; sa version suit la version de diffusion Gui, actuellement `0.1.1`.

Le moteur expose mise en page typée, texte, contrôles, entrée et composition Graphics annoncée. ML/CSS suivent le frontend GUI, pas CSS navigateur arbitraire ni RML natif. Fermez sur le thread propriétaire et récupérez les retraits différés avant les ressources empruntées. Un nettoyage échoué garde ses ressources pour réessai. Capacités et limites dépendent des fournisseurs natifs ; un plug-in d’éditeur ne les installe pas.

## SDK et intégration

```cmake
find_package(VeroScriptRmlUi 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::RmlUi)
```

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/fr-FR/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/fr-FR/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/fr-FR/index.md)

<a id="composition-resources"></a>

## Composition et limites des ressources

RmlUi prend en charge masques imbriqués/inversés, captures de couches, opacité, luminosité, contraste, inversion, niveaux de gris, saturation, sépia, rotation de teinte, flou et ombre portée. Masques et filtres exigent la composition Graphics ; les opérations non prises en charge produisent une erreur. Le sigma du flou est limité à 64 pixels, celui des ombres à 64/3 pixels. Les dégradés sont rastérisés une fois par géométrie dans des textures mises en cache, limitées à 8192 par dimension et 16 777 216 pixels.

Les identités de ressources chargées sont immuables. Les images absentes ou invalides produisent une erreur de mise à jour/rendu ; après remplacement des données en échec, changez d’identité ou recréez le contexte. Les sources d’image sont limitées à 64 Mio, les images décodées à 16 384 par dimension et 16 777 216 pixels, les données de polices personnalisées conservées à 128 Mio au total. Le lecteur standard `ReadGuiResourceFile` limite aussi chaque fichier à 64 Mio ; un lecteur personnalisé ne supprime pas les limites des images ou le total des polices. Configurez le lecteur avant utilisation des textures.

<a id="font-atlas-and-services"></a>

Les atlas de police mesurent 1024 × 1024 pixels ; glyphes/effets doivent tenir avec leur marge de rangement. Un échec produit `rmlui.font_atlas_failed` et reste à régénérer : les nouvelles demandes signalent aussi l’échec au lieu d’omettre du texte. Les contextes RmlUi vivants partagent le registre de polices et doivent utiliser les mêmes instances de services police et layout ; des ID de backend identiques ne suffisent pas. Choisissez de nouvelles instances après fermeture de tous les contextes. Fournissez-les via `RmlUi::FOptions` ou les options GUI et ne les remplacez pas pendant la vie des contextes.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
