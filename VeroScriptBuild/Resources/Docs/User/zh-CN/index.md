# 构建与打包应用

`--build-file <文件>` 可指定任意配置文件名，支持绝对路径及相对命令启动目录的路径。未指定 `--path` 时使用配置文件父目录作为项目根；指定后，配置内输入路径以该项目根为基准。默认文件名为 `veroscript-build.json`。

可选的 `VeroScriptBuild` 模块读取严格的 `veroscript-build.json`。配置内路径相对显式项目根；命令行路径相对命令启动目录。服务不会修改进程工作目录。

`--profile <名称>` 选择 `profiles` 中声明的配置，默认值为 `release`。`--target <名称>` 选择构建目标，可重复指定；未显式选择目标时使用配置中的 `defaultTargets`。

```sh
veroscript build --path /absolute/project --plan --json
veroscript build --path /absolute/project --build-file /absolute/config/release.json --plan --json
veroscript build --path /absolute/project
veroscript build --path /absolute/project --target application --profile release
veroscript /absolute/project/dist/release/portable/application/sample.vsapp
```

`--plan` 只检查与规划，不执行处理器、自定义步骤、安装钩子或子进程；显式密码库在规划时也只读取身份，不加载原生代码。需要依赖时先安装并锁定。构建不联网恢复依赖或发布到注册源。

| 目标 | 产物与职责 |
| --- | --- |
| library | Core `.vslib` 库 |
| application | 编译或源码 `.vsapp` 应用 |
| distribution | 根含 `veroscript-package.json` 的 `.vspkg/.zip` 安装分发包 |
| deployment | 从已安装快照导出 runtime 或 runtime＋compile 用途环境 |
| bundle | 应用加可选 Runtime、依赖环境和启动器；可输出目录、ZIP、容器或 PE/ELF 单文件 |
| custom | 已注册原生提供者、显式授权的进程或脚本步骤 |
| group | 选择依赖目标，不生成文件 |

构建时的应用验证使用所选构建目标的平台与 ABI，而非构建机器的身份。声明的组件齐备时可以准备其他目标的产物，但其中的原生组件仍需在对应目标环境中验证和运行。

产物引用写为 `{ "target": "library", "output": "main" }`；源码树成员增加 `"path": "main.vs"`。随附 `Resources/Examples/hello` 展示分库、编译／源码应用、目录、ZIP 和安装包；复制到可写位置后运行。

输出位于 `dist/<profile>/<目标身份>/<target>/`，portable 身份目录为 `portable`。构建收据管理所有权和摘要：外部修改或非本构建拥有的文件不会被覆盖。每个目标同卷原子发布；后续目标失败不会回滚已完成的独立目标。使用产物时保留返回的租约。

缓存绑定配置、输入、上游内容、提供者身份及输出摘要。自定义缓存要求明确的确定性和工具链身份；动态处理器和保护／信任相关步骤采取保守重建。输入变化后须重新规划。

进程步骤使用可执行文件和参数数组，不通过 shell。脚本步骤提供 `build_read_text`、`build_write_text`、`build_message`；只允许脚本目录、声明的输入目录及显式标准库根中的导入。文件、环境、控制台、任务、地址及非确定性标准库能力被禁用，并设置 VM 执行、调用深度、缓冲及数值限制。原生提供者和进程属于宿主授权能力，不代表操作系统沙箱。

Runtime、依赖环境和启动器独立可选。源码应用应保留 runtime＋compile 用途；编译应用通常仅需 runtime。`bundle.runtime` 使用经过验证的 `veroscript-runtime.json` 组件，只复制描述中列出的运行文件，不复制整套 SDK。生成启动器需要匹配的显式模板；PE/ELF 单文件复用同一应用内容和经过校验的缓存。Mach-O 或其他平台封装由自定义提供者承担。

保护配置只保存密钥 ID／版本；使用 `--crypto-library <文件> --keyring <文件>` 配置原生保护服务，禁止把密钥文件加入发行物。安装分发的 ZIP 和清单始终可读。`publish` 仍只向注册源发布安装分发包。完整格式见 Schema 和 Application 服务文档。

源码应用会在发布前编译实际装入包内的源码；宏、隐式导入和可移植选项保存在应用描述中。开发机的 importPaths／stdlibSearchPaths 不能嵌入源码应用，请使用 files、modules 与显式环境；编辑器覆盖先物化为生成源码树。额外样例 `Resources/Examples/generated` 演示脚本生成器与类型化源码树引用。目录和单文件程序使用内容签名，平台签名为独立步骤。

重新组装已签名应用时，必须显式指定新产物的签名策略。

`--allow-process` 与 `--allow-script` 分别授权执行进程和脚本自定义步骤。产物引用只在声明的输入字段解析，提供者 `options` 即使包含 `target`／`output` 也保持不透明 JSON。重新报告输入不能替换冻结摘要；宿主 import／stdlib 根相对项目解析，保留在目标根之前的搜索优先级。启动器模板在规划时冻结，改变模板或其字节使规划／缓存失效。

资源文件映射使用产物签名；显式保护策略先验证既有资源签名，再变换或签署。重新加密只解码既有信封一次，改变签名字节要求新签名策略；新签名替换原有旁车，未变化的复制保留签名，验证失败禁止发布。

Windows Runtime 清单会保留已暂存的 MSVC 运行库及授权说明；携带 Runtime 的目录／ZIP 应用也会将这些库放在启动器旁。单文件启动器仍需满足原生加载器的前置条件：动态 CRT 构建要求目标系统具有匹配的 VC++ Redistributable；需要免安装该运行库时，应从源码对相关原生组件采用一致的静态 CRT 构建。

嵌入宿主执行 `script` 构建步骤时，需通过 `FServices::Compile.ScriptCompileExtensions.VmBackendFactory` 或 `VmBackendPath` 显式提供 VM，两者互斥；缺失时报告 `script-vm`。桌面 `veroscript build` 自动使用该产品装配的 VM，保留步骤的权限、指令、期限、取消和内存限制。VM 路径与输入摘要参与缓存校验；使用自定义工厂时，应提供稳定的 `FServices::Identity`，否则脚本步骤不复用缓存。

<!-- docs-navigation -->

---

[用户文档](../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
