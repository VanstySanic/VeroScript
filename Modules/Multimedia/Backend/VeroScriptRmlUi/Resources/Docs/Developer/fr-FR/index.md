# Maintenance de VeroScriptRmlUi

## Responsabilités et vérification

Ce module maintient l’adaptateur GUI de rendu, de disposition et de saisie et ses dépendances intégrées. Sa surface publique est déclarée dans [`Public/VeroScriptRmlUi.h`](../../../../Public/VeroScriptRmlUi.h) ; ses entrées spécifiques sont dans [`Resources/Tests`](../../../Tests).

Les frontières communes des fournisseurs, les reconstructions requises, les vérifications et les ressources légales sont décrites dans [Maintenance multimédia](../../../../../../../../Resources/Docs/Developer/fr-FR/multimedia.md).


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/fr-FR/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/fr-FR/GENERAL.md)

Les mesures de texte utilisent des caches bornés par contenu. La clé de largeur comprend les plages du paragraphe et de la ligne, la direction, la langue, les espacements et la révision des polices de repli. L’analyse du paragraphe distingue aussi le service réel de composition. Recréer le propriétaire du même texte immuable ne relance pas l’analyse. Limites : 1 024 largeurs / 512 Kio par face et taille, et 2 048 paragraphes / 8 Mio de texte. Glyphes, curseurs et sélection conservent la composition complète.

Les conteneurs verticaux imbriqués à hauteur automatique utilisent un flux bloc et des éléments privés d’espacement pour éviter les mesures intrinsèques répétées de Flex imbriqués. Hidden conserve sa place ; Collapsed et les éléments flottants sont exclus du flux. Les dispositions horizontales, Grid, Wrap, Splitter et les éditeurs gardent leurs chemins spécialisés. Les dégradés linéaires affines à deux couleurs peuvent utiliser les couleurs des sommets ; les autres restent rastérisés. Les masques isolés utilisent des cibles temporaires bornées, avec repli sur le compositeur général si nécessaire. Les masques imbriqués, doux et inversés préservent la composition source-over prémultipliée.

La dernière analyse reste disponible pendant les mesures, y compris pour un seul paragraphe dépassant le budget de texte du LRU. Elle est remplacée par l’analyse suivante ou libérée avec les ressources de polices.

<a id="native-provider-maintenance"></a>

Le module optionnel `VeroScriptRmlUiModule` fournit le service GUI natif et doit correspondre au build Core/Gui. Synchronisez en-têtes RmlUi corrigés, bibliothèque et inventaire correctifs/notices ; ne mélangez pas un RmlUi système avec ces en-têtes d’adaptation. La voie native du SDK n’exige pas le fournisseur optionnel de bindings Core. Font, layout, Image et Graphics restent des dépendances choisies ; leur durée de vie et les échecs d’atlas suivent le guide public RmlUi. Préservez ces limites lors des changements de caches ou de synchronisation.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
