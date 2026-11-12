# Maintenance de VeroScriptFont

## Responsabilités et vérification

Ce module maintient les contrats des polices, de la mise en forme et de la disposition du texte. Sa surface publique est déclarée dans [`Public/VeroScriptFontLayout.h`](../../../../Public/VeroScriptFontLayout.h) ; ses entrées spécifiques sont dans [`Resources/Tests`](../../../Tests).

Les frontières communes des fournisseurs, les reconstructions requises, les vérifications et les ressources légales sont décrites dans [Maintenance multimédia](../../../../../../../Resources/Docs/Developer/fr-FR/multimedia.md).


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/fr-FR/index.md)
- [Standards](../../../../../../../Resources/Docs/Standards/fr-FR/GENERAL.md)

<a id="font-layout-maintenance"></a>

Les layouts immuables conservent texte et service choisi. Leur cache synchronisé garde au plus quatre analyses immuables ; les fragments stylés sélectionnent les segments logiques concernés. N’omettez la validation UTF-8 que si le layout validé possède exactement le tampon et sa longueur. Préservez validation des groupes et ordre des glyphes du fallback. `VeroScriptFontContractTests` et `VeroScriptFontLayoutContractTests` utilisent des mocks sans fournisseur concret ; corpus et tests script appartiennent à FontBackend. Les bindings choisissent explicitement une VM sans en-têtes de test privés du Core.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
