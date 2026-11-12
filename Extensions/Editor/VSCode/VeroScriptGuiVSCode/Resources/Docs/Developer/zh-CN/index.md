# VeroScript GUI VS Code 开发文档

## 架构

原生 GUI 验收须使用匹配的 Core、Tooling、VM 与提供者构建；产品版本相同不代表 ABI 兼容。编辑元数据校验与实际检查、应用容器编译和有限帧预览分别验证。

产物回归通过带所选提供者的 `gui-check`、独立的普通 `--check`，以及继承所选源码项目环境和编译参数的普通 `--check` 校验 `.vsapp`。显式标准库路径与 `--path` 同时传入，覆盖原生标准库根目录去重。总体期限覆盖原生任务及归档校验，每条命令仍有独立期限。原生 Core／ABI 构建变化后，应重新生成已安装的测试环境。

Runtime、LSP、DAP 保持在主插件。GUI 插件激活依赖后通过 `Public/api.d.ts` 的 API 1 获取所属工具链，仅显式执行时解析 Runtime。源文件 URI 用于展开主插件运行变量，任务所属工作区决定配置。编辑和自动任务发现不解析、不启动原生工具；配置任务的解析可查询 CLI 版本。原生 GUI 生产代码独立维护。

结构模型按文档版本缓存，解析有大小与深度限制；文档变化或取消后丢弃异步元数据／路径结果。自定义元数据缓存和监视器按解析后的配置隔离。原名和 Mark 标签区分大小写，冲突别名不提供候选，继承遍历避免重复。编辑提示不得代替编译器语义校验。

## 构建与验收

需要 Node.js 22.18+ 和桌面 VS Code 1.106+。分别在对应模块目录执行下列命令，两边均安装依赖，因为共享声明引用主插件的 VS Code SDK 类型。开发测试同时加载两个扩展；安装包测试在隔离配置中按顺序安装本地两个 VSIX，测试 CLI 不应从 Marketplace 获取未发布的依赖。

```sh
# VeroScriptVSCode
npm ci
npm run typecheck
npm run package
# VeroScriptGuiVSCode
npm ci
npm run typecheck
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
node Resources/Tools/test.mjs --packaged --skip-build
```

桌面 Extension Host 测试应逐套串行运行。多个 VS Code 测试窗口同时打开时，可能争用补全确认、撤销和回车用例所需的编辑器焦点。

原生 GUI 验收为每个检查、编译或预览任务保留 600 秒，并仅按该测试的任务序列调整总体期限；编辑器提供者仍使用较短的等待期限。显式 GUI 命令可能冷编译完整框架，正常完成也可能超过一分钟。产品任务保持异步，可从 VS Code 停止；非零退出、缺少输出或超过测试预算仍判定验收失败。

单元用例覆盖结构恢复、UTF-16 位置、样式边界、原生元数据、冲突／循环、CLI 参数顺序和语法作用域。Extension Host 用例覆盖依赖、语言、补全、悬停、链接、标签高亮、诊断及编辑不启动原生服务。设置 `VEROSCRIPT_TEST_RUNTIME` 为匹配且启用了 GUI 的 Runtime，再设置 `VEROSCRIPT_GUI_TEST_NATIVE=1` 才要求真实检查／编译；有兼容原生提供者的桌面环境可另设 `VEROSCRIPT_GUI_TEST_PREVIEW=1`，要求两帧标记预览。Windows、Linux、macOS 需分别验收，并检查空格／非 ASCII 路径、无工作区、多工作区、远程宿主及真实预览窗口。跳过可选用例不代表原生验收通过。

可选 `VEROSCRIPT_TEST_DIRECTORY` 与主插件一致，用绝对路径指定隔离测试工作区和配置的父目录。验收原生包环境时，将其放在 `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH` 所选项目内，使生成的 GUI 源码归属于该环境。无关临时工作区可能被原生源码归属检查拒绝；不要通过放宽编译器访问范围绕过测试布局不匹配。

每次原生验收都应记录 Runtime 对应源码／发行快照，旧报告不能证明新原生代码。CLI 超时属于验收失败。不要因编辑自动启动预览。

## 编辑器元数据

将下例保存为 `demo.veroscript-gui.json` 并加入 `veroscriptGui.schemaFiles`。它只提供编辑提示，真实类型和编译处理器仍由宿主注册。靠后的元数据替换同名完整类型，需提供该类型全部自有成员；基类使用规范全名。可选 `routedEvents` 增加公共事件提示。

```json
{
	"schemaVersion": 1,
	"types": [{
		"name": "Demo::Card",
		"mark": "card",
		"bases": ["VS::Gui::Widget"],
		"properties": [{ "name": "Title", "mark": "title", "type": "string", "bindable": true }],
		"events": [{ "name": "Selected", "mark": "selected", "type": "std::multicast_delegate<void()>" }]
	}]
}
```

内建元数据从原生 `VeroScriptGui.vs` 入口沿本地 `export import` 收集，输入列于 `builtin.json`。保留未指定 `Mark` 别名的已注解控件，确保复合控件继承完整。用 `npm run schema:update` 刷新后审查原生字面值及样式／绑定标记。正常构建检查快照同步，并拒绝重复类型或缺失基类。单元和 Extension Host 回归覆盖复合标签、继承属性／事件、位置值、CSS 提示及选中色。元数据校验不代表原生运行验收。 原生命名空间重构后保留完整限定的 `VS::Gui::Internal` 基类，维持继承提示。

元数据提取接受 `@` 与注解名称之间合法的语言空白，例如 `@GuiWidget` 和 `@ GuiWidget`，两者产生相同的属性、事件与继承信息。源码排版变化后重新运行 `npm run schema:check`；提取结果变化时使用 `npm run schema:update`。`builtin.json` 记录来源路径及元数据，不保存源码内容摘要。

## 分发

安装包仅包括 bundle、清单、本地化、语法、Schema、图标及 MIT 许可，不含原生二进制、开发工具、测试和开发文档。当前 GUI 运行时代码没有第三方 JavaScript 依赖；增加依赖必须更新声明。LICENSE 应与仓库一致，六种 UI／文档同步。保留本模块独立 VSIX 打包；仓库综合 IDE 打包已独立登记 GuiVSCode，会构建基础插件并用两个本地 VSIX 联合验收。

本轮静态回归审查覆盖 EOF／标签边界、数值替换范围、带空格赋值、关闭引号后的重复属性、内嵌 XML 注释及空大纲项。CSS URL 在缓存的样式解析中单次读取，支持原生大小写与转义；链接请求复用范围，不再为每个链接重复遍历屏蔽区间。配置任务的准备阶段可能查询 CLI 版本；编辑与自动任务发现不会启动 GUI 命令。原生验收通过 VS Code 已注册的任务提供者获取配置任务，使安装包测试真正使用已安装插件。提供者需要包环境时，可将 `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH` 设为已准备环境的绝对路径。回归用例也覆盖与 JavaScript 原型成员同名的自定义标签属性补全。隔离测试配置关闭单词建议以避免干扰插件结果，并使用受支持的编辑器关闭命令。原生验收要求匹配的 Runtime，以及成功完成检查、编译和预览。

参阅[编辑器集成维护](../../../../../../../../Resources/Docs/Developer/zh-CN/editors.md#acceptance-evidence)。
发布者以 `package.json` 的声明为准，GUI 依赖必须与主插件标识一致。根 README 随包提供，作为扩展详情页，不链接到未打包的仓库文件；功能包文档过滤仅豁免 `extension/readme.md`。发布 Marketplace 需要对应已注册发布者的权限并完成验收，修改清单不等于注册发布者。

构建时将[共享 256px 产品 PNG](../../../../../../../../Resources/Assets/icons/veroscript-icon-256.png)复制为扩展展示图标。


[zh-CN](../../User/zh-CN/index.md) · [Documentation](../../README.md)
