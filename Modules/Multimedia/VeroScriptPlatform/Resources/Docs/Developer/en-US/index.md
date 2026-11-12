# VeroScriptPlatform

See [VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/en-US/index.md#provider-boundary).

## Native contract build

Run these commands from the repository root to build the public contracts without script adapters. Executing scripts also requires a provider and a host that registers the corresponding RegisterScriptBindings adapters with the matching Core.

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform
```

## Maintenance and verification

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/en-US/index.md)

[README](../../../../README.md) · [User](../../User/en-US/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/en-US/index.md)

[en-US](index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
