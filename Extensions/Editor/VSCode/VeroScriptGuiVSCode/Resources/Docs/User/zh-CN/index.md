# 在 VS Code 中使用 VeroScript GUI

先安装 **VeroScript**，再安装 **VeroScript GUI**。本地主插件需要支持工具 API 1，应配套更新。GUI 插件负责 `.vswml`、`.vswcss`；`.vs`、LSP、DAP、Runtime 选择和编译设置由主插件负责。颜色跟随 VS Code 主题。

编辑支持控件、属性、`bind:`／`on:`、枚举值和资源路径提示，悬停、大纲、折叠、匹配标签高亮和六／八位十六进制颜色编辑。原名及 Mark 别名区分大小写，`bind:`／`on:` 不需要声明 `xmlns`。VSWCSS 使用原生 GUI 支持的子集。结构提示为信息级别，最终正确性由原生编译器判定。内嵌脚本提供语法着色，不提供独立 GUI LSP 或表达式语义分析。

内建提示已覆盖 ListView/ListItem、TreeView/TreeItem、TableView/TableColumn/TableRow、ComboBox、TabView/TabItem、Menu/MenuItem 和 Dialog，包括继承的属性与事件。SelectionColor / selection-color 支持标记颜色编辑，Popup Placement 提供 below、right、center 候选。执行时需使用配套重建的 GUI 提供者和脚本库；更新后的原生结构不可混用之前的 0.1.1 二进制。 [GUI 框架](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/zh-CN/Reference/Framework.md).

使用 **VeroScript GUI：检查 GUI、编译 GUI 应用、预览 GUI、选择 GUI 入口、显示 GUI 输出** 命令。预览启动原生应用。通过主插件的 `veroscript.runtimePath` 选择启用了 GUI 和 GUI 脚本的匹配 Runtime，并准备应用需要的原生提供者。单纯编辑不会启动原生进程。

显式 GUI 命令在冷启动时可能编译完整框架，耗时可超过一分钟。任务异步运行，可按需使用 VS Code 的任务控制停止。

## 工作区设置

`veroscriptGui.entry` 指定 `.vs`／`.vswml`，检查和预览也支持 `.vsapp`。留空使用选中文件；样式文件默认使用同名 `.vswml`，共享样式应显式选择入口。`assets` 指定编译时嵌入的文件，`packageName` 指定包名，`schemaFiles` 增加编辑提示元数据。数组替换默认值。

`backends` 仅接受 `platform`、`graphics`、`gui`、`image`、`font`、`text-layout`。插件值省略或留空时继承原生环境；指定后端标识会选择该后端，`auto` 请求原生候选选择，`none` 禁用该服务。禁用预览必需的服务会返回原生错误。`environmentPath` 显式通过 `--path` 加载 CLI 包环境，后端选项先于环境加载。它不会配置主插件的 LSP 或调试器。参数、环境、工作目录和编译选项继承主插件。检查／编译固定使用 VM 且不添加 JIT 模块；预览继承运行后端。`previewFrames` 为 0 时不传帧数限制；正安全整数仅适用于 `.vswml`，`.vs` 入口自行管理循环。

标准原生 Context 工厂仅在构造参数、本次调用与应用／环境默认值均未提供选择时后备为 `rmlui`，保留显式 `auto` 和 `none`。插件设置留空表示不传覆盖选项；原生环境配置中明确保存的空后端值则表示禁用服务。VM/JIT 执行选项由主插件负责，不属于 GUI 服务角色。

路径和进程属于工作区扩展宿主，远程场景亦如此。相对设置和任务路径以所属工作区为基准；无工作区时以选中文件目录为基准。支持 `${workspaceFolder}`、`${file}`、`${fileDirname}`、`${env:NAME}`。文件变量需要文件上下文，工作区变量需要文件夹。文档内部资源以声明文件所在目录为基准；不把资源 ID 强行解释为文件。

## 任务与元数据

任务使用 `type: "veroscript-gui"`、`action: "check" | "compile" | "preview"` 和 `input`。编译还需以 `.vsapp` 结尾的不同 `output`。可选 `assets`、`name`、`frames`、`args`、`backends`、`environmentPath` 覆盖对应设置，数组替换继承值，`args` 仅用于预览。命令会询问保存涉及的未保存源文件；配置任务遵循 VS Code 的任务保存策略。请另行保存配置源范围之外的依赖。原生错误支持文件和行导航，不将 UTF-8 字节列误作 UTF-16 列。

将自定义 `*.veroscript-gui.json` 加入 `schemaFiles` 可扩展提示，并获得 JSON 格式校验。使用 `schemaVersion: 1`、`types`；类型包含 `name` 及可选 `mark`、`bases`、`properties`、`events`，成员包含 `name`、`type` 及可选 `mark`、`bindable`、`style`、`values`、`description`。这些元数据不注册可执行绑定或编译处理器，冲突标签别名不会作为候选提供。

```json
{
	"schemaVersion": 1,
	"types": [
		{
			"name": "Demo::Card",
			"mark": "card",
			"bases": ["VS::Gui::Widget"],
			"properties": [
				{ "name": "Title", "mark": "title", "type": "string", "bindable": true }
			]
		}
	]
}
```

GUI `.vs` 的语义提示和调试需要主插件配置兼容宿主及绑定目录；没有自动 GUI 处理器启动、独立 GUI 调试器或可视化设计器。超过 2 Mi UTF-16 代码单元的文件停止本地结构分析。

未完成的标签头和文件末尾 CSS 声明仍可补全；保留 `=`／`:` 前的空格，数值尺寸按完整值替换。标签闭合后不提供属性，类名／ID 选择器不混入控件类型。CSS URL 链接遵循原生大小写与转义规则；脚本／样式体内 XML 注释中的标签不会破坏结构范围。

GUI 编译需要原生 Application 支持，产出标准 `.vsapp` 应用容器。同一产物可用于 GUI 检查／预览和通用 CLI inspect／检查／运行；宿主需要提供 GUI 支持及所需提供者。此前的原始 GUI `.vsapp` 产物需要重新编译。后端保持手动选择，插件不会自动选择 RmlUi。

通用 CLI 的 `--check` 可独立检查生成的 `.vsapp`，也可通过 `--path` 选择匹配的包环境。GUI 检查通过 `environmentPath` 选择该环境。请使用修复后的 Runtime 及匹配的已安装环境；Core／ABI 更新后，应重新生成面向旧构建的环境。

请使用完整且匹配的 GUI Runtime，包括随包提供的 `Vm/` 模块。GUI 检查和编译可能执行脚本编写的编译处理器，因此同时需要 Runtime VM 和所选 GUI 提供者。提供者保持手动选择；编辑器元数据描述可编辑的声明，运行时通知实现的变化不直接增加标记属性。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
