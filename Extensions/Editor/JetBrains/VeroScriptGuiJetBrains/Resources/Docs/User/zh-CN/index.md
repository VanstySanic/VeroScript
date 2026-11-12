# 在 Rider 中使用 VeroScript GUI

## 安装与配置

先安装 **VeroScript**，再通过 **Settings → Plugins → Install Plugin from Disk** 安装兼容的 **VeroScript GUI**，并按提示重启。Rider 要求和 Runtime 选择以以下基础插件指南为准。

按[基础插件指南](../../../../../VeroScriptJetBrains/Resources/Docs/User/zh-CN/index.md)
在“语言与框架 → VeroScript”配置 Runtime。GUI 任务需要匹配的 GUI 版 CLI、GUI 脚本库及
原生提供者，插件不会安装这些组件。没有 Runtime 时，仍可使用本地标记/样式着色和内置补全。

## 编辑标记和样式

内置补全包括 `ListView`、`TreeView`、`TableView`、`ComboBox`、`TabView`、`Menu` 和 `Dialog`，
以及继承的选择/弹出属性、自定义事件和样式属性。数据项和虚拟化通过 GUI 脚本 API 配置。
使用包含这些控件的配套 GUI Runtime。

`.vswml` 使用原生 GUI 标记语法，`.vswcss` 使用 GUI 样式语言，不等同于 HTML 或完整 CSS。
`bind:`、`on:` 不需要声明 `xmlns`。补全包括内置控件、继承属性、取值绑定、事件、样式属性
及支持的伪状态，保留大小写规则，并遵循 Rider 的首项选择设置。指令 `src` 支持本地路径导航和
补全；相对路径以所属文件为基准，当前系统识别的绝对路径以文件系统根为基准。
分隔符遵循当前系统规则，保留文件名中的 `?` 等字面字符。
宿主不透明资源标识，以及含 SDK 会裁剪的首尾空白或实体转义（`&...;`）的路径不提供本地导航。
普通路径引用参与 Rider 的文件/目录重命名，写入名称时保留标记转义。

用 `=` 接受属性或用 `:` 接受样式属性时，不会重复已有标点，并将光标移至值处；已有值会保留。
Tab 替换完整名称，包括连字符名称及 `bind:`/`on:` 前缀。

内联 `<script>`、`<style>`（包括 CDATA）支持词法着色。需要完整 VS 语义编辑时，将代码
放进外部 `.vs`，使用 `<script src="logic.vs" type="App::Logic" />` 引用；外部脚本继续使用
基础插件的 LSP。本插件没有实现内联脚本/绑定的语义补全、自定义控件 Schema 分析、格式化
或自动编译诊断。请通过“检查 GUI”调用原生工具校验。
GUI API 的语义解析和调试仍以所配置的原生 LSP/DAP 与宿主支持为准；本插件不会为它们生成可执行绑定。

`page.vswml` 示例：

```xml
<Stack Width="100%" Height="100%">
	<style src="page.vswcss" />
	<Text Text="Hello, VeroScript GUI!" />
</Stack>
```

`page.vswcss` 示例：

```css
:root {
    padding: 16px;
    gap: 12px;
    color: #edf4fc;
    background-color: #101927;
    font-size: 18px;
}
```

完整语义见[框架指南](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/zh-CN/Reference/Framework.md)
及[综合样例](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/Showcase/page.vswml)。
自定义控件不会仅因未列入内置补全目录而被标记成错误。

## 检查、预览与烘焙

右键本地 `.vswml`、`.vs` 或 `.vsapp` 入口，选择 **VeroScript GUI**。
`.vswcss` 通过引用它的标记入口进行检查，不能直接运行。

| 任务 | 行为 |
| --- | --- |
| 检查 GUI | 调用 `gui-check`，检查／编译，但不调用应用入口、不创建预览窗口。 |
| 预览 GUI | 调用 `gui-preview`，执行入口并打开独立原生窗口；可通过 Rider 停止或关闭窗口。 |
| 配置 GUI 应用烘焙 | 打开编译配置，核对 `.vsapp` 输出和资源，再点“运行”执行 `gui-compile`。 |

检查或烘焙源码时仍可能执行脚本编译处理器。请使用包含 VM 和 GUI 脚本库的完整匹配 Runtime；这些任务不需要执行用的 JIT 提供者。

可在运行配置中新增 **VeroScript GUI**。`.vswml` 支持从上下文创建预览配置；普通 `.vs` 的
运行和调试继续由基础插件提供。GUI 任务当前仅支持“运行”，没有 GUI 专用 DAP 启动适配。
宿主已准备调试域时，可按基础插件说明附加。预览需要显式执行，没有内嵌设计器或自动热重载。

## 配置细节

- 上下文操作继承 GUI 配置模板，清除不适用于所选任务/入口的选项；适用的自定义烘焙输出会保留。
- 继承基础插件的 Runtime、语言和项目编译设置。单次运行的编译 JSON 覆盖不改变编辑设置。
  继承路径保留项目来源；运行覆盖中的相对路径按工作目录解析。
- 上下文入口使用 `$PROJECT_DIR$`。工作目录留空表示项目根目录。ML/CSS 内部的相对资源引用
  始终以所属源文件为基准。
- “包环境”对应 `--path`。后端字段留空时不传覆盖项，继承 Runtime/包环境默认值。
  `auto` 交由原生端选择可用提供者，`none` 禁用该服务；禁用预览必需服务时会报错。
  原生环境中显式存储的空后端值按 `none` 处理，与此处留空不同。相应提供者仍需安装或由 GUI Runtime 提供。
- 标准 `Context` 工厂仅在构造参数、调用选择及工厂默认值都未指定 GUI 后端时回退 `rmlui`，
  不会替换显式的 `auto`/`none`。六个后端字段只管理 GUI 服务；VM/JIT 执行选项不属于这些字段。
- 检查和烘焙使用 VM；预览保留原生执行默认值。
- 持久化入口路径时只归一化本机路径分隔符，保留 POSIX 文件名中的反斜杠。
- 预览参数放在 `--` 后。帧数限制为正 `int64`，仅用于 `.vswml`；留空或 `0` 表示不限。
  脚本和 `.vsapp` 自己管理执行循环。
- 烘焙要求 `.vswml` 或 `.vs` 源码、以 `.vsapp` 结尾的输出，可填写名称与资源 JSON 数组，
  如 `["assets/logo.png"]`。输出不能覆盖入口或列出的资源。默认输出位于 `.tmp/gui/`，
  如需长期保留产物，请修改路径。
- 环境变量为字符串值的 JSON 对象，可控制是否继承父进程环境。原生错误引用本地文件时，
  控制台提供行导航。Runtime 不支持命令、缺少提供者等错误会显示在控制台。

<!-- docs-navigation -->

---

[用户文档](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
