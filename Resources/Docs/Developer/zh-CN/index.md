# 文档维护

[GitHub 源码仓库](https://github.com/VanstySanic/VeroScript)

<a id="documentation-ownership"></a>

## 正本与维护归属

| 内容 | 维护正本 |
| --- | --- |
| 公共编辑器集成 | [编辑器集成维护](editors.md) |
| 公共阅读路线、跨模块关系、教程和本项目开发与贡献规范 | 根目录 `Resources/Docs` |
| 模块行为、公开 API、默认值和限制，以及模块架构、构建与维护 | 所属模块的 `Resources/Docs/User/<locale>` 和 `Developer/<locale>` |
| 多媒体模块共用的提供者维护说明 | [多媒体维护](multimedia.md) |
| 导出的 All/User 目录与 ZIP | 生成产物；修改链接指向的正本后重新出版 |

每个主题、每种语言只维护一份正本。入口页可摘要并链接，完整契约、操作流程和会变化的默认值由归属处维护。六语言并行正本同步更新；跨模块公共流程归此处，协议和 SDK 细节归所属模块。文档旧版本由版本控制保留。

`documentation.json` 登记集合的语言、模块清单和配套资源；各模块的 `manifest.json` 将本模块 User 正本映射到稳定出版路径，Developer 页面仍按所属模块归集。同一源文件在 All 集合中只出版一次；User 包从中投影相同的 User/Tutorials 内容。出版副本不是另一份维护正本。

## 本地化与路径

包括 Overview 在内，六语言保持相同页面文件名和语义；API 名、诊断 ID、JSON 键、命令和可运行代码保持原样。既有验收结果只适用于记录的源码、产物及环境。

模块清单将 User 正本映射到稳定出版路径。所包含页面和资源间改写为相对链接，保留语言、锚点、编码路径、查询参数、图片和引用式链接，代码及 HTML 注释保持原样。仅属于源码的引用跳转在线仓库。旧文档通过版本控制查看，不复制到出版包。

维护文档和本地化文件时，遵循[公共文件换行规则](../../Standards/zh-CN/GENERAL.md)。该仓库格式约定不会改变语言和工具对 CRLF 输入的支持。出版流程生成规范化副本，不改写维护正本或二进制资源。

## 校验与出版

从仓库根目录运行下列命令。每次出版都从当前正本重新集成，修改源页面后无需同步生成副本；仅提交维护的正本。集成期间若正本发生变化则拒绝出版，无论成功或失败都会清理临时目录。CI 校验语言对齐、文件集合、出版字节和链接。Markdown/JSON 出版文本使用 LF；原始许可证声明、二进制资源和正本保持原始字节。

```powershell
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
python Resources/Tools/Packaging/veroscript_docs.py publish --audience All --output .tmp/docs-preview
python Resources/Tools/Packaging/veroscript_docs.py verify-publication --audience All --output .tmp/docs-preview
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -OutRoot .tmp/docs-collection-package
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -Audience User -OutRoot .tmp/docs-user-package
```

`build` 和 `publish` 的输出必须是仓库 `.tmp` 或 `dist` 内的专用空子目录。源码 `Release` 和已有受管集合下禁止直接输出；Resources 通过事务式业务发布器发布。每次预览使用新目录；重复导出独立 ZIP 时，使用 `package-veroscript-docs.ps1`，它会先暂存并校验新包，再替换旧产物。

<a id="integrated-publication-views"></a>

`package-veroscript-docs.ps1` 默认导出 All：现行 Overview、User、Developer、Standards、完整 Tutorials 与链接资源。`-Audience User` 将参考发布到 `docs/<locale>/...`，完整教程发布到 `docs/Tutorials/<locale>/...`，并包含其依赖。两种受众均不包含 History。

该脚本导出独立文档，不发布到受管产品集合。默认输出为 `dist/docs/<Audience>/<version>/`，指定 `-SourceRoot` 时为 `dist/docs/Custom/<version>/`；可用 `-OutRoot` 指定其他专用输出目录。完整 Resources 发行仍通过产品发布器集成 User 视图。

完整发行通过 Resources 在 `Release/Resources/any/<version>/Resources/Docs/` 发布 User 视图，与 `Resources/Scripts`、`Resources/Assets` 并列。当前 Runtime/SDK/扩展归档和安装器不附带编写的文档与示例；载荷政策见 Installer Developer 的 [release-layout.md](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-CN/release-layout.md)。

[本项目开发与贡献规范](../../Standards/zh-CN/README.md) 仅适用于 VeroScript 仓库开发与贡献，不是 SDK 使用要求或 MIT 许可的附加条件。`documentation.json` 的 `standardsAudience` 固定为 `Developer`：Standards 仅作为开发者资料进入 `All`，不进入 `User`、Resources 中的用户文档、Runtime/SDK/扩展发行包或安装器。源码版权与许可声明继续保留。

本项目开发与贡献规范维护在 `Resources/Docs/Standards/<locale>`：`zh-CN` 定义规则，其他五种语言保持等价。出版直接读取正本。本独立仓库没有 IDE 规范镜像；`sync-standards` 仅在通过文档配置发现 ValenceStudio 使用方时更新受管参考副本，否则不会创建镜像。

需要 Python 3.11+。`check` 校验源码文档和已发现的规范使用方；`check-collection` 校验临时完整集合；`build/verify` 预览 User 抽取；`publish/verify-publication` 从当前正本出版并核验 All 或 User。自定义 `-SourceRoot` 导出显式提供的目录，不能与 `-Audience` 混用。缺失或越界链接、缺失锚点、空目录树、文件系统链接和特殊条目会被拒绝；源与输出不能重叠。ZIP 包含隐藏资源与空目录，替换失败时恢复旧产物，回滚失败则保留恢复备份。在线源码引用需要联网；HTML 渲染器须转换 `.md` 链接并保留标题 ID。产品构建、载荷校验与运行验收状态记录见[发行验收流程](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-CN/release-validation.md)。

## 模块清单

顶层 `Resources/Docs/documentation.json` 中的 `$module/` 前缀表示所属源码模块的根目录。`VeroScript` 等产品目录名保持原样；各模块 manifest 内的路径仍相对于自己的 Docs 目录。移动或改名外层目录时，还须更新人工文档中的命令及仓库工作流使用的实际路径。供 Windows PowerShell 5.1 使用的脚本包含非 ASCII 文本时须保留 UTF-8 BOM。

```json
{
  "schemaVersion": 1,
  "module": "VeroScriptCli",
  "user": [
    {
      "source": "User/{locale}/index.md",
      "output": "{locale}/toolchain/cli.md"
    },
    {
      "source": "User/{locale}/packages.md",
      "output": "{locale}/toolchain/packages.md"
    }
  ],
  "developer": true
}
```

## 文档类型与内容

User 面向使用者的任务、公开 API 行为、配置与限制；Developer 面向架构、实现职责、构建、测试和出版；Tutorials 按前提、步骤及预期结果教学；Standards 仅收录维护者已决定的本项目开发与贡献规范。旧计划和验收证据保存于版本控制或临时输出，不能证明变更后工作区已验收。

文档正文、标题和文件名避免具体日历日期及非必要个人标识。通用说明直接描述行为，不反复标注当前发行版本；兼容性、工具要求、ABI 或格式契约、可执行示例、产物选择及原始许可声明所需的准确版本仍须保留。变化中的标识优先链接其维护正本，不重复抄录。带日期的日志与验收记录放入被忽略的临时输出。User 不重复测试方法或内部实现细节。

## 链接与读者边界

User 中的相对链接应落在 User 视图、Tutorials 或已登记的配套资源内。User 到 Developer 或 Standards 的相对链接会阻止 All 和 User 两种出版。服务端实现、维护和验收流程放在 Developer，User 说明使用者可观察的行为；Developer 可以链接对应的 User 契约。修改跨模块链接后，应检查正本集合和两种出版包。

## 模块参考

| 类别 | 参考文档 |
| --- | --- |
| 语言与工具 | [VeroScript](../../../../VeroScript/Resources/Docs/Developer/zh-CN/index.md) · [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/zh-CN/index.md) · [Tooling](../../../../VeroScriptTooling/Resources/Docs/Developer/zh-CN/index.md) |
| 宿主 SDK | [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/zh-CN/index.md) · [C++](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-CN/index.md) · [Python](../../../../Extensions/Language/Python/Resources/Docs/Developer/zh-CN/index.md) · [C#](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/zh-CN/index.md) · [WebAssembly](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/zh-CN/index.md) |
| 执行引擎 | [Bytecode VM](../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/Developer/zh-CN/index.md) · [AsmJit](../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/zh-CN/index.md) |
| 包与应用 | [PackageManager](../../../../VeroScriptPackageManager/Resources/Docs/Developer/zh-CN/index.md) · [Application](../../../../VeroScriptApplication/Resources/Docs/Developer/zh-CN/index.md) · [Build](../../../../VeroScriptBuild/Resources/Docs/Developer/zh-CN/index.md) · [Installer](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-CN/index.md) |
| 编辑器 | [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/zh-CN/index.md) · [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/zh-CN/index.md) · [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/zh-CN/index.md) · [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/zh-CN/index.md) |
| 嵌入平台 | [Mobile](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/zh-CN/index.md) · [Android](../../../../Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Docs/Developer/zh-CN/index.md) · [iOS](../../../../Extensions/Platform/Mobile/VeroScriptIOS/Resources/Docs/Developer/zh-CN/index.md) |
| 数据与网络 | [JSON](../../../../Modules/Network/VeroScriptJson/Resources/Docs/Developer/zh-CN/index.md) · [Network](../../../../Modules/Network/VeroScriptNetwork/Resources/Docs/Developer/zh-CN/index.md) · [HTTP](../../../../Modules/Network/VeroScriptHttp/Resources/Docs/Developer/zh-CN/index.md) · [WebSocket](../../../../Modules/Network/VeroScriptWebSocket/Resources/Docs/Developer/zh-CN/index.md) · [Boost](../../../../Modules/Network/Backend/VeroScriptBoost/Resources/Docs/Developer/zh-CN/index.md) · [Curl](../../../../Modules/Network/Backend/VeroScriptCurl/Resources/Docs/Developer/zh-CN/index.md) |
| 多媒体与 GUI | [Platform](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/zh-CN/index.md) · [Graphics](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/zh-CN/index.md) · [Audio](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/zh-CN/index.md) · [Image](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/zh-CN/index.md) · [Font](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/zh-CN/index.md) · [Gui](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/zh-CN/index.md) · [Sdl3](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/zh-CN/index.md) · [Stb](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/zh-CN/index.md) · [FontBackend](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/zh-CN/index.md) · [RmlUi](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/zh-CN/index.md) |

同仓库现行指南之间使用相对链接，不能用在线 URL 绕过读者边界或让已包含文档依赖网络；源码引用保持既有行为。

<a id="current-publication-policy"></a>

All 和 User 均只出版现行文档。History 目录、旧计划和验收快照不属于出版输入；历史修订由版本控制保留，精确验收证据放在被忽略的临时输出。源码或自定义 SourceRoot 含 History 路径时会拒绝出版，大小写变体也不例外。按所选受众集成 Overview、User、Developer、Standards、完整 Tutorials 及其依赖。

<a id="module-entrypoints"></a>

模块根 README 介绍用途、关联模块、文档和许可入口；Resources/Docs/README 负责六语言及读者导航，也是集成出版使用的入口。完整使用说明归 User，架构、构建和验证归 Developer，分步骤教学归 Tutorials。语言工具子模块链接到所属工具组的文档正本。入口保持简短，文件移动时同步相对链接，不在 README 维护另一份完整手册或验收记录。

<!-- docs-navigation -->

---

[开发文档](index.md)

语言: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
