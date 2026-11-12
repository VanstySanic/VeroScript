# 诊断本地化

本页说明诊断与宿主工具界面的显示语言。已加载目录选择显示文本，不改变脚本语义。应用文本使用独立的 [std::i18n API](standard-library/i18n.md)；修改应用上下文不会改变进程诊断语言。

语言选择作用于已加载目录中收录的诊断。CLI 帮助及部分参数校验消息目前仍为英文；目录未收录的消息保留原文。

## 支持策略

标准包提供 `en-US`、`fr-FR`、`ja-JP`、`ko-KR`、`zh-CN` 和 `zh-TW`。应用可用的语言取决于实际加载的目录，可通过 `GetAvailableLanguageTags` 或对应 SDK API 查询。有效的新增文化目录可以提供其他语言；请将其随应用携带，或显式加载其本地化根目录。

语言选择 API 推荐使用规范的 BCP 47 标签，例如 `en-US`、`zh-CN` 或 `zh-TW`。运行时也会规范化操作系统常见写法，例如 `ja_JP.UTF-8`、带 `@modifier` 的标签以及 `C`/`POSIX`；规范化后的结果使用连字符、语言小写、脚本首字母大写和地区大写。

规范化会先移除首尾空白、编码和修饰符后缀，再将 `C`/`POSIX` 解析为 `en-US`，也支持 `C.UTF-8` 和 `POSIX.UTF-8@modifier`。

## 语言匹配与回退

标签解析按以下顺序进行：

1. 精确匹配已经加载的规范标签；
2. 逐级去掉最后一个子标签，尝试父标签；
3. 在相同主语言的文化目录中选择确定性的候选；
4. 如果没有任何可用候选，显式选择 API 返回失败，初始化阶段回退到 `en-US`。

中文会保留简繁体语义：`zh-Hant-HK` 可以匹配 `zh-TW`，`zh-Hans` 可以匹配 `zh-CN`。显式的 `Hans` 或 `Hant` 子标签优先于地区推断；扩展及私有用途子标签不参与书写系统选择。在默认资源目录中，`zh-Hans-TW` 匹配 `zh-CN`，`zh-Hant-CN` 匹配 `zh-TW`。请求了简体或繁体时，不会静默选择另一种书写系统的目录。翻译缺少某个文件或消息键时，只要英文基准中存在对应条目，就回退到 `en-US` 的文本。

`TrySetLanguageFromTag` 成功后，语言会切换到实际匹配到的规范标签；失败时不会改变当前语言。`GetAvailableLanguageTags` 返回当前目录中已经加载的语言集合。

英文基准目录中没有的诊断保留原文。加载译文目录不会翻译脚本输出或应用中的任意字符串。

## 选择语言

CLI 使用 `--lang`，环境变量使用 `VERO_LANG`：

```powershell
veroscript --lang zh-CN --check script.vs
$env:VERO_LANG = "en-US"
veroscript --check script.vs
```

CLI 先加载环境和本地化目录，再处理命令行参数，因此 `--lang` 覆盖 `VERO_LANG`。`VERO_LANG` 又覆盖操作系统语言；若环境或操作系统语言无法匹配，初始化会使用 `en-US`。CLI 显式传入一个无法匹配的 `--lang` 时会报告参数错误。

对于由编辑器管理的语言服务器会话，应在打开文档前把 BCP 47 偏好写入
`initialize.locale`。`workspace/didChangeConfiguration` 可以修改分析设置，但语言
最好在下一次初始化时应用，确保所有打开文档使用同一个目录。`veroscript-lsp` 与
CLI 使用相同的 `tooling.json` 和 compiler 目录；客户端的逻辑应使用诊断 ID，而不应
依赖翻译后的消息文本。
解析器的声明诊断（包括 `static class` 和 `static struct` 的约束）也使用相同的稳定
ID 和目录加载路径。因此，对于相同的源码错误，CLI 与 LSP 会选择相同的本地化消息。

## 目录布局

本地化根目录必须包含完整的 `en-US` 基准目录；其他一级目录都是文化目录，目录名必须是带地区或其他限定部分的规范 BCP 47 标签。VeroScript 当前要求文化目录名包含连字符，因此单独的 `zh`、`ja` 或非规范的 `zh_cn` 会被拒绝。

默认目录示例为：

```text
localization/
  en-US/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
  <culture>/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
```

也可以继续按相同规则增加文化目录或在文化目录下按相对路径拆分 JSON 文件：

```text
localization/
  en-US/
    runtime.json
    tooling/compiler.json
  <culture>/
    runtime.json
    tooling/compiler.json
```

加载器会递归发现 `.json` 文件，并按相对路径合并。对于每个翻译文化：

- 翻译文件必须在 `en-US` 下存在同一路径；
- 翻译文件不能增加英文文件中没有的消息 ID；
- 缺少翻译文件或消息键时使用英文文本；
- 同一文化内的文件不能重复消息 ID；
- 文化目录不能重复、不能为空，也不能使用非规范或不带限定部分的标签。

每个 JSON 文件必须是 UTF-8 编码的扁平对象，键和值都必须是字符串：

```json
{
	"diag.example": "Example message",
	"diag.with_argument": "Value: {0}"
}
```

文本可以使用 `{0}` 到 `{9}` 占位符接收诊断参数。JSON 必须是完整对象，不能有重复键、非法转义、无效 UTF-8、尾随内容或非字符串值。

## 加载、替换与合并

`VERO_LOCALIZATION_DIR` 指定完整的本地化根目录，请在启动工具或初始化 SDK 前设置。没有有效的显式目录时，运行时优先尝试可执行文件附近的发行资源（`localization`、`../assets/localization`），然后才尝试工作目录中的资源。需要确定使用自己的目录时，设置该变量或显式调用 `LoadCatalogDirectory`。仅在尚未加载目录树时执行自动发现。

`LoadCatalogDirectory` 会校验并替换当前进程中的完整目录树；加载失败时保留原来的已加载目录。`MergeCatalogDirectory` 要求传入的目录自身也是完整且有效的目录树，然后按文化合并：默认不覆盖已经存在的消息 ID，显式启用覆盖时才替换已有文本。两种操作都在核心中使用线程安全的进程级本地化状态。

标准库搜索根、库包根或模块文件旁边可以携带同级的 `localization/` 目录。模块加载器会以非覆盖方式合并这些目录；这使库能够提供自己的消息 ID，同时避免意外改写宿主已经加载的文本。源覆盖仍保留模块的逻辑路径，因此旁侧目录仍然可以被发现。

## SDK 边界

- C++ 宿主直接使用 `VeroScript::Locale` API；语言和目录状态是核心进程级状态。
- Interop 通过 `vs_interop_locale_set_language`、`vs_interop_locale_get_language`、`vs_interop_locale_get_available_languages`、`vs_interop_locale_load_catalog_directory` 和 `vs_interop_locale_merge_catalog_directory` 暴露同一策略。
- WebAssembly/WASM 构建会把构建时的整个 `localization` 树嵌入虚拟文件系统；`setLanguage` 只能选择该构建实际携带的目录，`language` 和 `availableLanguages` 用于读取规范化后的当前语言与可用目录。

CLI 环境变量和命令行选项见[命令行工具](../../../../../VeroScriptCli/Resources/Docs/User/zh-CN/index.md)，C++ 宿主配置见[宿主集成](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/embedding.md)，Interop 接口见[Interop SDK](../../../../../VeroScriptInterop/Resources/Docs/User/zh-CN/index.md)，WebAssembly/WASM 接口见[WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/zh-CN/index.md)。

独立的 Python Frontend、BindGen、Transpiler 和 AOT 工具分别选择 `python_tools.*` 消息，其 `--lang` 优先于 `VERO_LANG` 和系统语言。它们规范化系统标签、保留中文简繁体，按精确、父级、确定性的同语言目录顺序匹配，无匹配或缺失文本时回退到英文。这不会修改原生 SDK 的进程级语言。见 [Python frontend](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/frontend.md)。


## 移动本地化的显式配置

原生 Android AAR 与 iOS C ABI SDK 携带六语言目录，需要显式加载。运行脚本前，将完整目录树复制到应用管理的真实目录；APK 资源路径不是文件系统目录。Android AAR 资源位于 `veroscript/localization`，iOS SDK 资源位于 `assets/localization`。Android 提供 `VeroScriptEngine.loadCatalogDirectory(path)` 和 `setLanguage(tag)`，均返回布尔成功值。iOS C ABI 提供 `vs_interop_locale_load_catalog_directory` 和 `vs_interop_locale_set_language`；用 `vs_interop_buffer_destroy` 释放错误缓冲区。检查每个返回值。

C# 移动应用先用 `VeroScriptMobileResources.InstallAsync` 安装资源，再调用 `VeroScriptRuntime.LoadCatalogDirectory` 并设置 `VeroScriptRuntime.CurrentLanguage`。Python 在导入 `veroscript` 时加载相邻的 `veroscript/localization` 目录树；部署时保留完整目录，然后调用 `veroscript.set_language`。见 [C# 移动集成](../../../../../Extensions/Language/CSharp/Resources/Docs/User/zh-CN/mobile.md)与 [Python 移动集成](../../../../../Extensions/Language/Python/Resources/Docs/User/zh-CN/mobile.md)。

语言选择在进程内共享，不是每个引擎独立设置；选择失败保留当前语言。脚本输出不变。目录中没有的消息保留原文；内存分配失败也可能保留英文。

<!-- docs-navigation -->

---

[用户文档](index.md) · [VeroScript](../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../en-US/localization.md) · [fr-FR](../fr-FR/localization.md) · [ja-JP](../ja-JP/localization.md) · [ko-KR](../ko-KR/localization.md) · [zh-CN](localization.md) · [zh-TW](../zh-TW/localization.md)
