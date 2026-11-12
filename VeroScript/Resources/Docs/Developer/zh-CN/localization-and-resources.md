# 本地化资源与部署

本页面向本地化目录维护者和构建、打包工具开发者。语言选择、自定义目录与 SDK 使用见[诊断本地化](../../User/zh-CN/localization.md)。

## 源码归属与部署

源码中的本地化资源由所属模块或区域维护，统一位于 `Resources/Localization/<culture>/`。只有被多个 Python 工具共用的消息放在 Python 区域的共享目录。构建与打包按组件依赖汇总到部署目录 `localization/`：Runtime/Core SDK 携带 CLI、Tooling、LSP、DAP 和 Interop 所需资源；WebAssembly 携带核心与 C++ 宿主资源；Python 包携带核心、C++、Interop 和 Python 工具资源。IDE、安装器界面资源保持独立。

```text
VeroScript/Resources/Localization/<culture>/
  language.json, runtime.json, shared.json, host-binding.json, memory.json, i18n.json
Extensions/Language/Cpp/VeroScriptCpp/Resources/Localization/<culture>/embedding.json
VeroScriptInterop/Resources/Localization/<culture>/interop.json
VeroScriptTooling/Resources/Localization/<culture>/
  tooling.json, lsp.json, dap.json
Extensions/Language/Python/Resources/Localization/<culture>/python-tools.json
Extensions/Language/Python/VeroScriptPythonFrontend/Resources/Localization/<culture>/python-frontend.json
Extensions/Language/Python/VeroScriptPythonBindGen/Resources/Localization/<culture>/python-bindgen.json
Extensions/Language/Python/VeroScriptPythonTranspiler/Resources/Localization/<culture>/python-transpiler.json
Extensions/Language/Python/VeroScriptPythonAot/Resources/Localization/<culture>/python-aot.json
VeroScriptInstaller/App/Resources/Localization/<culture>/
```

直接从源码仓库运行时，核心发现自己的目录，各 SDK 或工具模块补充自己的 Resources/Localization。显式加载的目录和部署目录保持权威；成功替换目录后，不会自动混入源码资源。

## 源码资源发现

`VERO_LOCALIZATION_DIR` 指定完整的本地化根目录。未指定或无法加载时，核心依次尝试以下位置，在首个有效目录树处停止：

1. 可执行文件旁的 `localization`，然后是相对于可执行文件目录的 `../assets/localization`；
2. 可执行文件目录的最多八级祖先：每一级先尝试 `localization`，再从该目录本身或仓库的直接子目录识别源码模块，读取其核心本地化目录；
3. 当前工作目录的 `localization`；
4. 在当前目录、父目录、祖父目录依次识别源码模块；识别成功后读取模块内的 `VeroScript/Resources/Localization`。

源码发现接受包含 `VeroScript/Public/VeroScriptVersion.h` 的模块目录，或包含 `CMakeLists.txt` 且恰有一个此类直接子目录的仓库。外层目录可以改名；缺失或有歧义的源码模块会被忽略。`InitializeModuleFromEnvironment` 仅在初始化已选择源码目录后，补充相对于源码模块的 `Resources/Localization`。成功加载的显式目录或部署目录保留自己的资源集合。

## 发布检查

- 每个发行包都必须包含完整且有效的 `en-US` 目录；
- 需要支持的其他语言必须随目标宿主一起复制、嵌入或打包；
- 翻译文化的文件路径和消息 ID 必须是英文基准的子集；
- 不要使用旧的 `en.json`/`zh-CN.json` 域优先布局，当前格式是 `<culture>/<component>.json`；
- 诊断 ID 比显示文本稳定，宿主、LSP 和 DAP 应使用 `MessageId` 做逻辑判断；
- 打包后应验证可用语言集合、缺失键的英文回退和脚本参数占位符替换。


## 移动资源与本地化

公共移动目录清单、部署路径和显式加载流程统一维护在 [Mobile](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/zh-CN/index.md#mobile-localization)。

翻译缓冲区所有权、错误与导出维护契约见 [Interop](../../../../../VeroScriptInterop/Resources/Docs/Developer/zh-CN/abi-and-lifetime.md#localization-abi)。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/localization-and-resources.md) · [fr-FR](../fr-FR/localization-and-resources.md) · [ja-JP](../ja-JP/localization-and-resources.md) · [ko-KR](../ko-KR/localization-and-resources.md) · [zh-CN](localization-and-resources.md) · [zh-TW](../zh-TW/localization-and-resources.md)
