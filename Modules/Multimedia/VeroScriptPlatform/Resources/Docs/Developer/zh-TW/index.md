# VeroScriptPlatform

請參閱[VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-TW/index.md#provider-boundary)。

## 建置原生契約

從儲存庫根目錄執行以下命令，建置不含指令碼配接器的公共契約。執行指令碼還需要提供者，以及透過對應 RegisterScriptBindings 配接器和相符 Core 完成註冊的宿主。

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform
```

## 維護與驗證

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-TW/index.md)

[README](../../../../README.md) · [User](../../User/zh-TW/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/zh-TW/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](index.md)
