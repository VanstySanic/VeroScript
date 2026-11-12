# Rider GUI 插件开发

## 归属与依赖

[插件描述文件](../../../META-INF/plugin.xml) 是插件标识及基础插件依赖的正本。基础 VeroScript 项目提供 Runtime 路径、编译设置和 VS 词法着色，不重复打包其类。[Gradle 的 localPlugin 项目依赖](https://plugins.jetbrains.com/docs/intellij/tools-intellij-platform-gradle-plugin-dependencies-extension.html#local-plugin) 用于开发、测试和 IDE 沙箱；两个安装 ZIP 仍然独立。

使用 JDK/JVM 25、Gradle 9.5.0、Kotlin 2.4.0、IntelliJ Platform Gradle 插件 2.19.0。
SDK 固定为 Rider 2026.2.2，最低构建 `262.10315.191`，严格上界为 `262.*`；其他分支需要
另行验证。不内置原生工具或提供者。根 MIT 许可打入 `META-INF/LICENSE`，Gradle Wrapper
原始声明保存在 `Resources/Licenses`。

## 设计与契约

`gui-compile` 输出标准 `.vsapp` 应用容器。检查和预览将其交给原生 CLI，由后者读取容器与
Core 字节码。Runtime、VM 原生库及 GUI 提供者需要配套使用。

- `language/`：独立、区分大小写的标记和样式词法器及扁平 PSI；不引入 XML 命名空间诊断或
  浏览器 CSS 语义。内联脚本复用基础 VS 词法器，内联样式使用 GUI 样式词法器。
- `GuiMarkupModel`：按文件缓存结构数据，服务补全、引用及文档，不做类型检查。编辑时不扫描
  整个项目、不自动调用原生检查或预览。超过 1 MiB 或存在多光标时，跳过输入便利处理。
- `Resources/Catalog/gui.json`：从 GUI 自有源码提取的内置补全元数据。执行
  `python Resources/Tools/update-catalog.py` 后审查差异。源哈希用于发现漂移；目录不能替代
  `FGuiSchema`。自定义 Schema 由原生工具校验，不可因未收录而报错。保留 Original/Mark
  拼写以及继承属性、绑定和事件。
  提取会跟随 `VeroScriptGui.vs` 的本地导入及间接支持文件，省略 Mark 时使用声明名。
  类边界和注释/字面量屏蔽保证各声明的元数据归属正确。
  目录格式 2 保留声明的 `qualifiedName`，包括 `VS::Gui::Internal` 作用域。查找和控件
  文档使用真实限定名，同时保留短名和 Mark 别名；目录须与读取代码一起生成、打包。
- `run/`：继承基础 Runtime、语言与编译设置，按各路径的来源解析。参数依次为全局编译/
  环境选项（含 `--path`）、GUI 后端选项、`gui-*` 任务及任务选项，最后 `--` 与预览脚本
  参数。保留重复 `--asset`，帧数遵循 `int64` 边界。
- 检查和烘焙不初始化执行脚本；预览创建一个原生进程，由 Rider 进程处理器停止和终止。
  后端 ID 可留空。没有 GUI 专用 DAP 启动适配；宿主已准备调试域时，使用基础插件附加。
  控制台只按行导航，不将原生 ML 的 Unicode 码点列当作 UTF-16 偏移。

元数据提取接受 `@` 与注解名称之间合法的语言空白，例如 `@GuiWidget` 和 `@ GuiWidget`，两者产生相同的属性、事件与继承信息。即使公开类型不变，源码摘要变化后仍须刷新目录并审查差异。

## 验证与发行

编辑器不变量：两种词法器的 token 归属各自语言，扫描不得越过指定范围，包括截断的结束标记。
内联正文在下一处原始正文/CDATA 边界停止，不反复搜索文件余部；以字符读取次数的回归用例约束
线性扫描，避免依赖机器计时。
补全消费 `=`/`:` 接受字符，保留已有值，Tab 替换完整名称。普通本地 `src` 引用通过
`GuiStringLiteralManipulator` 修改路径/重命名，转义写入内容，保留未闭合引号；含实体转义
的路径仍不提供本地导航。当前系统的绝对路径从文件系统根解析，重新绑定时写入完整原生前缀，
不采用 SDK 的项目根语义；相对路径保留所属文件基准，禁用 SDK 的项目根式路径转换。
仅规范化原生分隔符，保留 POSIX 文件名中的反斜杠；引用解码保留 `?`，并沿用 SDK 的
macOS NFC 规范化。
相对路径重绑定保留 SDK 行为：无法形成相对路径时不改写引用文本。跨盘符/文件系统移动需单独
复核和验收，不宣称具备自动迁移能力。
文档目标拒绝已编辑的快照，样式悬停只识别属性 token。上下文操作
清除不兼容的任务/入口模板选项，保留适用的自定义输出。静态检查只遍历自有输入，不遍历
Gradle SDK 缓存或沙箱。

在本模块目录执行：

```sh
python -B Resources/Tests/test_catalog.py
python -B Resources/Tools/check-static.py
./gradlew :test :verifyPluginProjectConfiguration :verifyPluginStructure :buildPlugin
./gradlew :veroscript:buildPlugin
./gradlew :verifyPlugin
```

Windows 使用 `gradlew.bat`；POSIX 可用 `bash ./gradlew`。通过
`-PlocalIdePath=/absolute/path/to/Rider` 使用已安装 SDK。显式根任务路径避免误选所有子项目
测试。GUI 输出为独立的 `VeroScriptGuiJetBrains-0.1.1.zip`。仓库综合 IDE 打包已登记 GuiJetBrains；正式流程会构建基础插件并将两个 ZIP 联合交给 Plugin Verifier，按各自版本校验依赖。单独开发构建仍可使用上述命令。

基础插件尚未发布时，单独执行 `:verifyPlugin` 无法从 Marketplace 解析这项依赖。
应使用官方 Plugin Verifier CLI 同时校验两个 ZIP：在 `plugins.txt` 中逐行写入两者的绝对路径，
再执行下列命令。这样用本地基础插件解析依赖，出版前需要确认两个插件的校验结果。

```sh
java -jar /absolute/path/to/verifier-cli.jar check-plugin @/absolute/path/to/plugins.txt /absolute/path/to/Rider
```

测试覆盖词法器重启/不完整输入、目录继承与样式限制、可迁移路径、帧数与入口校验、CLI
选项顺序与参数分隔、资源/覆盖检查、持久化、注册和六语言资源。SDK 夹具不能替代 Rider
实机验收。需用 GUI Runtime 实测 ML、VS、产物的 Check/Compile/Preview、迁移、提供者、
空格及非 ASCII 路径、停止/取消和诊断导航；每个目标系统单独验证。检查内联着色、补全首项、
引号/标签、`src` 导航，以及两插件同时安装时普通 `.vs` 的行为。

参阅[编辑器集成维护](../../../../../../../../Resources/Docs/Developer/zh-CN/editors.md#acceptance-evidence)。

编辑器夹具通过仅供测试使用的 SDK 模块初始化正常的 Rider 文件夹方案，使用真实本地文件。
仅在测试 JVM 内禁用 ReSharper；沿用 SDK 的设置损坏检查，以 Rider 初始化后的设置作为基线。

Rider 交互界面、真实窗口的 Preview/Stop、迁移、其他操作系统和真实宿主集成仍需单独验收。
上述结果仅覆盖已测试环境，不代表这些剩余场景已经通过。

六语言界面、User 和 Developer 文档应同步维护；模块清单已注册到共享文档目录。
修改文档后应执行文档出版校验。

[用户指南](../../User/zh-CN/index.md) ·
[规范](../../../../../../../../Resources/Docs/Standards/zh-CN/GENERAL.md)

## 检查／烘焙调用与保存路径

`GuiCommand.arguments()` 对 `check`、`compile` 在最前面添加 `--backend=vm`，先于 `--path` 准备包环境，抑制这些任务的执行 JIT 选择；预览不传执行后端或 JIT 覆盖，保留原生默认值。GUI 后端字段仍是独立服务选择。`GuiCommand.persist()` 仅在项目相对保存路径中将 `java.io.File.separatorChar` 转为 `/`，保留 POSIX 文件名中的字面反斜杠。维护选项顺序、设置持久化后的省略／auto／none／自定义提供者和本机文件名往返场景。路径行为应按目标 OS 分别验收，Windows 结果不能证明 POSIX 行为。

[Rider SDK](../../../../../VeroScriptJetBrains/Resources/Docs/Developer/zh-CN/index.md#rider-platform-sdk)

<!-- docs-navigation -->

---

[开发者文档](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
