# VeroScriptPlatform

[VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/ja-JP/index.md#provider-boundary) を参照してください。

## ネイティブ契約のビルド

リポジトリのルートで次のコマンドを実行すると、スクリプトアダプターを含まない公開契約をビルドできます。スクリプトの実行にはプロバイダーと、対応する RegisterScriptBindings アダプターを互換性のある Core に登録するホストも必要です。

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform
```

## 保守と検証

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/ja-JP/index.md)

[README](../../../../README.md) · [User](../../User/ja-JP/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
