# FontBackend

## Utiliser le service

`FontBackend::CreateFontService()` fournit `font/font-backend`. FreeType charge polices, métriques et glyphes A8/couleur ; HarfBuzz traite formes contextuelles, ligatures, écriture, langue et direction. Chaque face possède ses octets et synchronise les mutations, sans exposer de types tiers.

Utilisez les fabriques natives ou l’API Font enregistrée avec le fournisseur `font-backend` dans un hôte compatible. Les faces conservent le module fournisseur. Installez Font/FontBackend et, si nécessaire, leurs fournisseurs/liaisons. La police par défaut et sa licence appartiennent à Font. L’analyse bidi du paragraphe relève de `FTextLayout` ; les écritures réellement disponibles dépendent aussi des polices.

## SDK et intégration

```cmake
find_package(VeroScriptFontBackend 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::FontBackend)
```

Le SDK natif fonctionne sans Core. Les liaisons de script et fournisseurs natifs facultatifs doivent correspondre à la compilation réelle de Core, à son ABI publique et à la chaîne d’outils ; la version du produit ne suffit pas. Installez aussi les dépendances natives et les composants Bindings demandés.

- [Image](../../../../../../VeroScriptImage/Resources/Docs/User/fr-FR/index.md) · [Font](../../../../../../VeroScriptFont/Resources/Docs/User/fr-FR/index.md)
- [GUI](../../../../../../VeroScriptGui/Resources/Docs/User/fr-FR/index.md)

FontBackend possède FreeType, HarfBuzz, SheenBidi, l’analyse des paragraphes et leurs notices. CreateFontService() et CreateTextLayoutService() fournissent les deux services ; le fournisseur natif facultatif les enregistre sous font-backend.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
