# Maintenance de VeroScriptFontBackend

## Responsabilités et vérification

Ce module maintient les fournisseurs de mise en forme, de rastérisation et d’analyse bidirectionnelle du texte. Sa surface publique est déclarée dans [`Public/VeroScriptFontBackend.h`](../../../../Public/VeroScriptFontBackend.h) ; ses entrées spécifiques sont dans [`Resources/Tests`](../../../Tests).

Les frontières communes des fournisseurs, les reconstructions requises, les vérifications et les ressources légales sont décrites dans [Maintenance multimédia](../../../../../../../../Resources/Docs/Developer/fr-FR/multimedia.md).


- [CMakeLists.txt](../../../../CMakeLists.txt)
- [README](../../../../README.md)
- [User](../../User/fr-FR/index.md)
- [Standards](../../../../../../../../Resources/Docs/Standards/fr-FR/GENERAL.md)

<a id="native-provider-maintenance"></a>

Le module optionnel `VeroScriptFontBackendModule` enregistre séparément les services police et layout. Les hôtes statiques utilisent `veroscript_font_backend_query`, les extensions dynamiques `veroscript_extension_query`. Vérifiez chaque service contre le contrat Font correspondant avant conversion. Faces et analyses conservées gardent le lease du fournisseur indépendamment du handle de registre initial. Conservez ici sources, notices et build fournisseur FreeType/HarfBuzz/SheenBidi ; Font possède les contrats publics, la police par défaut et les tests par mocks indépendants.

## Construction et durée de vie du fournisseur

`VEROSCRIPT_BUILD_FONT_BACKEND` sélectionne ce module. `VEROSCRIPT_FONT_BACKEND_NATIVE_EXTENSION` construit le fournisseur adapté au Core ; `VEROSCRIPT_FONT_BACKEND_STATIC_PROVIDER` sélectionne son entrée statique nommée. Font possède les interfaces génériques et ne découvre aucun backend concret. Ce module conserve les faces FreeType/HarfBuzz, l’analyse SheenBidi, les sources épinglées et les notices. Font et text-layout restent des descripteurs de service indépendants, même avec le même identifiant de backend.

Les régressions de script lient explicitement le `VeroScriptBytecodeVm` correspondant et utilisent sa fabrique publique. Pour contrôler les leases, gardez séparément faces, analyses et instantanés après libération des handles du registre. Vérifiez les constructions natives seules et avec Core, l’isolation des dépendances et les clients SDK déplacés lorsque l’exécution est permise ; consignez échecs et cibles non vérifiées.

[Contrats publics de police et de mise en page](../../../../../../VeroScriptFont/Resources/Docs/User/fr-FR/index.md) · [Maintenance des consommateurs GUI](../../../../../../VeroScriptGui/Resources/Docs/Developer/fr-FR/index.md#gui-text-layout-maintenance)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
