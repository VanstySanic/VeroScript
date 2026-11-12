# VeroScriptPlatform

Voir [VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/fr-FR/index.md#provider-boundary).

## Compiler les contrats natifs

Depuis la racine du dépôt, ces commandes compilent les contrats publics sans adaptateurs de script. Pour exécuter un script, il faut aussi un fournisseur et un hôte qui enregistre les adaptateurs RegisterScriptBindings correspondants avec un Core compatible.

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform
```

## Maintenance et vérification

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/fr-FR/index.md)

[README](../../../../README.md) · [User](../../User/fr-FR/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
