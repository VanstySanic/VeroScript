# VeroScriptGraphics

参阅[VeroScriptSdl3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-CN/index.md#provider-boundary)。

## 构建原生契约

从仓库根目录运行以下命令，构建不含脚本适配器的公共契约。执行脚本还需要提供者，以及通过对应 RegisterScriptBindings 适配器和匹配的 Core 完成注册的宿主。

```sh
cmake -S . -B build/visual-contracts -DVEROSCRIPT_BUILD_PLATFORM=ON -DVEROSCRIPT_BUILD_GRAPHICS=ON -DVEROSCRIPT_VISUAL_SCRIPT_BINDINGS=OFF
cmake --build build/visual-contracts --config Release --target VeroScriptPlatform VeroScriptGraphics
```

## 维护与验证

[SDL3](../../../../../Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-CN/index.md)

[README](../../../../README.md) · [User](../../User/zh-CN/index.md)

## 可选原生提交作用域

IRenderSubmission2D 是可选原生接口，通过 IRenderer2D 的 dynamic_cast 查询，不改变既有渲染器虚表或脚本 API。SDL_GPU 支持该接口，SDL3 模块会转发此能力。BeginSubmission 拒绝嵌套，EndSubmission 集中提交同一渲染器的连续通道；CancelSubmission 可重复调用，失败或异常时也必须清理。所有调用保持在所属线程，资源在作用域结束前保持打开。其他渲染器消费其输出前须先结束作用域。作用域内 RenderSurface 的 bPresented 表示已取得图像，EndSubmission 成功仅确认提交，不表示 GPU 已完成或显示器已呈现。取得交换链图像后取消仍须遵守提供者的强制提交规则。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
