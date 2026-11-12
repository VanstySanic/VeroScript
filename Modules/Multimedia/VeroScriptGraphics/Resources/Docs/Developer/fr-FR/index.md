# VeroScriptGraphics

Voir [VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/fr-FR/index.md#provider-boundary).

## Compiler les contrats natifs

Depuis la racine du dépôt, ces commandes compilent les contrats publics sans adaptateurs de script. Pour exécuter un script, il faut aussi un fournisseur et un hôte qui enregistre les adaptateurs RegisterScriptBindings correspondants avec un Core compatible.

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_BUILD_GRAPHICS=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform VeroScriptGraphics
```

## Maintenance et vérification

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/fr-FR/index.md)

[README](../../../../README.md) · [User](../../User/fr-FR/index.md)

## Portées natives de soumission facultatives

IRenderSubmission2D est une interface native facultative accessible par dynamic_cast depuis IRenderer2D, sans modifier sa table virtuelle ni l’API de script. SDL_GPU la prend en charge et le module SDL3 la transmet. BeginSubmission refuse l’imbrication ; EndSubmission soumet les passes consécutives de ce moteur. CancelSubmission est idempotent et assure aussi le nettoyage après un échec ou une exception. Les appels restent sur le thread propriétaire et les ressources restent ouvertes jusqu’à la fin. Terminez la portée avant qu’un autre moteur consomme ses sorties. Dans une portée, bPresented de RenderSurface indique l’acquisition d’une image ; EndSubmission confirme la soumission, sans garantir l’achèvement GPU ou l’affichage. L’annulation après acquisition d’une image respecte les règles de soumission obligatoire du fournisseur.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
