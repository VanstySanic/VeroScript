# VeroScriptPlatform

参阅[VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-CN/index.md#provider-boundary)。

## 构建原生契约

从仓库根目录运行以下命令，构建不含脚本适配器的公共契约。执行脚本还需要提供者，以及通过对应 RegisterScriptBindings 适配器和匹配的 Core 完成注册的宿主。

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform
```

## 维护与验证

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-CN/index.md)

[README](../../../../README.md) · [User](../../User/zh-CN/index.md)

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
