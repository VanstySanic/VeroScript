# 打包与发行规范

适用范围：[VeroScript 本项目开发与贡献](README.md)。本规范定义官方发行集合、候选构建和出版验收，不改变外部项目的许可或打包方式。

## 规范与实现状态

目录、分类和新格式已由统一产品注册表及发行脚本实现；纯规划和静态回归不能替代成功的真实构建与载荷完整性校验；目标运行与安装生命周期验收另行记录真实结果，包括失败和未执行。 只接受新契约，不提供旧格式兼容、转换、双写或迁移；清理旧产物后从源码全量重建。

实施须同步产品注册、版本解析、静态规划、生产器、安装载荷、清单、校验、发布事务、清理和 CI。格式使用明确的 schemaVersion，新读取器只接受新契约；旧格式安全拒绝，不保留兼容分支。产品版本与格式版本独立，产品版本以发行登记为准。实施入口见[发行布局](../../../../VeroScriptInstaller/Resources/Docs/Developer/zh-CN/release-layout.md)。旧 `migrate-layout` 不属于新流程。

## 目录与职责

正式根目录为源码根下的 `Release/`，不依赖外层目录名 `Script` 或 `VeroScript`。移动或重命名检出目录后，保持内部相对路径即可；编译缓存按其路径依赖重新构建。候选、构建缓存和验收日志放在 `Release/` 外的专用暂存目录。

```text
Release/
  release-index.json
  indexes/<target>.json
  Runtime/<target>/<version>/
  CoreSDK/<target>/<version>/
  Interop/<target>/<version>/
  Resources/any/<version>/Resources/
  Modules/
    Execution/{Jit,Vm}/<target>/<version>/
    Network/{Json,Network,Http,WebSocket}/<target>/<version>/
    Network/Backend/{Boost,Curl}/<target>/<version>/
    Multimedia/{Platform,Graphics,Audio,Image,Font,Gui}/<target>/<version>/
    Multimedia/Backend/{Sdl3,Stb,FontBackend,RmlUi}/<target>/<version>/
  Extensions/
    Language/{Cpp,CSharp,Python}/<target>/<version>/
    Editor/VSCode/{VSCode,GuiVSCode}/<target>/<version>/
    Editor/JetBrains/{JetBrains,GuiJetBrains}/<target>/<version>/
    Platform/Mobile/{Android,iOS}/<target>/<version>/
    Platform/WebAssembly/<target>/<version>/
```

`<business-route>` 包含已注册的业务分类路径和业务名；`<target>` 表示分发范围，可为原生／移动目标、`any` 或 `wasm32`，不表示构建主机。`{A,B}` 表示分别创建目录。

各业务版本目录只创建实际需要的分类，清单和摘要位于版本目录根部：

| 路径 | 用途 |
| --- | --- |
| `archives/` | 直接解压部署的运行分发归档 |
| `sdks/` | 含开发接口、构建导出及必要依赖的 SDK 归档 |
| `packages/` | `.vspkg`、`.vsix`、插件 ZIP、`.nupkg`、wheel、sdist、AAR 等生态分发包 |
| `installers/` | 受支持的产品安装器及其选定的下载封装 |
| `symbols/` | 对应业务的符号包，包括 `.snupkg` |
| `Resources/` | 仅 Resources 业务使用的松散 Assets、Docs、Scripts、Examples 树 |
| `release-manifest.json`、`checksums.txt` | 该业务、范围和版本的清单与摘要 |

分类由用途和清单角色决定，不能仅按扩展名猜测。生态包不再机械套一层相同内容的 ZIP；需要额外头文件或构建资源的 SDK 可保留成套归档。Unix 下载封装保留执行权限和必要的库链接；默认只保留安装器的一种下载形态，确有独立消费需求时才同时发布裸文件与压缩包。安装器内部的安装路径不随发行目录分类自动改变。

Runtime、CoreSDK、Interop、Resources 直接归集合根；Cpp、CSharp、Python、WebAssembly、四个编辑器产品及 Android、iOS 归 Extensions；执行后端、网络及多媒体模块归 Modules。包管理客户端随 Runtime/CoreSDK 分发，不新增独立业务。业务注册必须覆盖新增产品、依赖、角色、目标能力和排除理由；完整性按所选目标和发行配置判定，不能要求每个平台包含全部业务。

目录名称固定：`archives`、`sdks`、`packages`、`installers`、`symbols`、`indexes` 均用复数。Modules、Extensions 及其 Execution、Network、Multimedia、Language、Editor、Platform、Backend 等业务分类沿用源码拼写，业务 ID 和包名保持稳定。

## 目标范围与公共产物

原生产物按操作系统和架构区分，例如 `win-x64`、`win-arm64`、`linux-x64`、`linux-arm64`、`macos-x64`、`macos-arm64`。已登记的其他架构仍须声明实际支持的发行配置，不能从注册项推断整套发行已可用。`android` 是多 ABI 分发；`ios` 包括真机与模拟器，iPadOS 使用同一 SDK；WebAssembly SDK 使用 `wasm32`，其宿主安装器仍归宿主目标。

`any` 表示不按单一原生目标拆分的公共分发位置。纯平台中立文件和同时覆盖多个目标的同一生态包均可使用，但必须准确声明 `supportedTargets`、ABI、TFM、运行依赖和限制。`any` 不代表任意平台均受支持；Android 多 ABI AAR 和 iOS XCFramework 仍归各自平台。

公共分发在每个业务和版本下仅存一份，各目标通过清单引用复用。四种现有编辑器插件、平台中立 Resources、清理后的 Python sdist，以及覆盖 Android/iOS 的同一 C# NuGet 应采用公共位置。WebAssembly 的 JS/WASM SDK 统一生成后复用；不得把原生安装器一并标成通用包。混合业务可同时拥有 `any` 与原生目标目录；例如 Python 的 sdist 在 `any`，wheel 按实际平台和 ABI 分发。

只在业务、版本、用途、支持范围、依赖与 ABI 契约一致，且最终文件摘要一致时合并重复产物。同名或文件扩展名相同不构成去重依据。公共文件先按明确的生成契约形成唯一规范字节，再生成清单、签名及摘要；自有文本采用 UTF-8/LF，保留必要 BOM 和 batch 例外。二进制、签名文件、第三方原文及字节敏感夹具不得机械改写。正式分发不依赖跨目录符号链接或硬链接来完成公共包引用。

独立安装器、wheel 和离线 SDK 仍携带其声明的必要运行依赖。归档内部的可独立使用性高于磁盘去重；不能把必需库移到相邻 Release 目录，再假定下载者拥有该目录。

支持范围分开记录：`supportedTargets` 描述实际载荷目标，`consumerTargets` 描述允许引用的发行目标；例如 wasm32 SDK 可被原生宿主发行索引引用。原生业务与公共分发组合时保留真实生态版本，不混合不匹配版本。

## 独立版本与依赖

各业务、各目标允许独立版本；`ReleaseVersions.json` 提供默认配置，不是集合统一版本。公共分发也有自己的明确版本，平台更新不得静默覆盖仍被其他目标引用的公共内容。索引固定实际选择的版本及清单摘要，不隐式解析“最新”。NuGet、wheel、sdist、插件和二进制保留真实版本；禁止只改文件名或伪造平台标签。

原生版本、业务版本、公共 ABI、构建身份及包格式版本分别记录。Cpp/Core、GUI 附加插件/基础插件及模块依赖按实际版本、ABI、构建身份或库摘要核对。已对外发布的同一包身份不得悄悄替换为不同内容；本地未发布候选的重建必须更新摘要并重做受影响验收，产品版本策略仍需明确。

同一生态包身份必须对应唯一内容，不能把同 ID/版本的不同 NuGet 或 sdist 放进多个平台目录，再依赖目录名区分。多目标差异使用生态本身的 TFM、RID、ABI 或平台标签表达；无法合并时须明确定义不同分发身份，不破坏既有包 ID 与版本契约。

共享 `any`／`wasm32` 清单发布到受管集合后，同一业务／范围／版本的身份不可变，包括可选符号和声明的支持范围。应在封存前收齐公共库存；增删或替换内容须使用新的业务版本。仅对尚未对外发布的本地产物，明确清理集合后可按已配置版本干净重建；删除本地文件不能授权修改已分发给用户的同一包身份。

`any`／`wasm32` 清单不记录发布宿主的原生版本。C# 的原生版本上下文记录在各目标索引的业务条目中，公共包身份由自身内容与契约决定。

## 索引、清单和引用

`release-index.json` 使用格式版本 **1**，产品标识为 `VeroScript Release Set`；`indexes/<target>.json` 使用版本 **5**，业务 `release-manifest.json` 使用版本 **3**。临时候选清单使用版本 **2**、产品标识 `VeroScript Release Candidate`。`checksums.txt` 按业务、范围和版本生成。索引业务的 `manifests[]` 逐项记录范围、真实版本、相对路径与 SHA-256；分类和产物角色来自统一产品注册表。

根 `release-index.json` 是集合入口，记录布局/格式版本、代际标识和各 `indexes/<target>.json` 的相对路径及 SHA-256，不宣称所有业务共用一个产品版本。目标索引独立记录该目标的当前选择；公共索引管理公共分发。

同一业务在一个目标索引中可引用多份业务清单，例如 Python 的目标 wheel 清单和 `any` sdist 清单。每条引用必须包含明确的 `target`、`version`、清单相对路径及 SHA-256；不能保留旧格式“一个业务只对应一个同目标清单”的假设。不同业务、不同目标可独立选择版本；同一业务在同一目标索引中组合的公共组件与原生组件须保持生态／产品版本一致，并共同通过兼容性和完整性校验。

业务清单必须记录业务、分发范围、真实版本、构建/源码身份、`supportedTargets`、依赖、角色及完整文件库存。每个文件至少包含分类后的相对路径、大小和 SHA-256；原生产物记录操作系统、架构、ABI、最低系统/API、工具链及所含切片，生态包补充 Python ABI、TFM 等必要选择条件。`checksums.txt` 由同一库存生成。目标选择、文件路径、生态元数据和二进制身份必须一致。

所有路径相对于其规定的根解析；清单内文件相对于业务版本根，索引内清单路径相对于 Release 根。拒绝路径穿越、绝对路径、链接逃逸、大小写或保留名别名、重复条目、未知文件和引用环。跨范围引用只能指向受管且支持该目标的清单；完整发行校验覆盖整个依赖与引用闭包，不通过目录名猜测内容。

库存覆盖实际产物与松散资源，不包含本清单和 `checksums.txt` 自身，避免摘要自引用；上级索引固定清单摘要，根入口固定索引摘要。松散文件和归档还须按格式记录或校验文件类型、可执行权限与安全的相对库链接；不能只比较普通文件内容而丢失可运行性。摘要验证不代替发布者身份和实际签名策略。

索引必须区分所选组件已发布与整平台完整发行。记录配置要求的业务集合、缺项、partial 和未验收项；单组件成功不能认证整平台。没有正式产物的目标不得伪造成功索引，其构建/验收缺口留在候选报告中。

## 功能载荷与源码包

Runtime、SDK、Extensions、Modules 和安装器默认只包含功能、运行资源、元数据及法律声明，不附带 README、手册、教程、示例、测试、基准或历史记录。Resources 集中提供集成 User 文档和相关资源；Standards 仅供开发与贡献，通过 All 文档提供，不进入 User 或功能包。保留许可证、NOTICE、必要本地化、图标、语法、代码片段和运行数据。Node.js/Sharp 等开发期图标生成工具不成为 Runtime/SDK 运行依赖。

VS Code VSIX 根目录的 `extension/readme.md` 属于扩展详情页元数据。这一特定展示文件可随包提供，须与模块 README 正本文本一致，并链接包内资源或在线指南。其他 README、手册及 Developer/Tutorials 目录仍不进入功能包。

所有 Modules，包括 Jit 及未来模块，均不生成独立产品安装器；调试符号按所属业务放在独立符号分发中。安装器不提供安装文档/示例选项，固定分页向导和所有权规则继续适用；可选文件关联、图标及 PATH 的取消和卸载只清理本次安装拥有的设置。

sdist 是可构建源码分发的明确例外：必须包含从独立解压目录完成构建所需的最小源码、头文件、构建脚本、元数据和法律声明，不携带与构建无关的说明或示例。打包必须排除并检查 `.cxx`、`.gradle`、`.externalNativeBuild`、CMake 缓存、对象文件、编译产物和本机路径；不得仅依赖 `.gitignore` 或当前宿主的工作区恰好干净。必须核对实际归档成员，必要时从独立源码包重新构建消费者。

模块安装、包启用和执行后端／服务提供者选择是不同操作。宿主配置遵循省略／auto／none／显式选择的统一规则；auto 可以从已安装且启用的兼容候选中自动选择。发布或安装须保留宿主的后端配置及禁用选择，不得将可选的 Jit／提供者悄悄加入 Runtime/CoreSDK 的基础载荷。

图标以正式 Assets 目录与自动资源清单为来源，新增文件进入版本控制和 CI 输入，不固定历史文件数量。SVG/PNG/ICO/ICNS 等二进制或原始资源按源文件逐项核对；安装引用的图标必须位于稳定的产品载荷路径。修改嵌入图标后重新构建引用程序或安装器，不能用替换外置 ICO 代替 EXE 重建；文件关联、刷新和卸载继续按安装器所有权契约处理。

源码校验还会逐帧核对 Windows CLI、Runtime、launcher、LSP、DAP 和安装器 EXE 的内嵌图标，复用构建也须通过。VS Code 声明的产品 PNG 直接对照正式资源校验。

## Android、iOS 和模块 SDK

Android 主 SDK 整合核心/Interop、Mobile 公共适配、JNI、AAR、标准库、必要资源及许可证；iOS 主 SDK 整合对应的静态适配和 XCFramework。Mobile 公共适配层没有要求用户额外安装的独立产品。主 SDK 不因此自动包含所有可选模块。

`Modules/<category-route>/<business>/<android|ios>/<version>/sdks/` 是该模块的可选原生 CMake 开发分发，不是另一套 Android/iOS 主 SDK，也不等于桌面 `.vspkg`。保留必要头文件、CMake 导出及已声明的依赖闭包，分别记录内置与外部依赖，避免同一应用内冲突或重复链接。不能因主 AAR 存在就删去需要独立提供的 C++ API 或模块开发支持。

完整移动发行必须覆盖平台适配、CoreSDK、Cpp、Interop、Vm、CSharp、Python 及所选可选模块的契约。普通应用消费主 SDK 和实际需要的模块，无须下载全部开发分发。Android 当前包含 arm64-v8a/x86_64；iOS 包含 device-arm64、simulator-arm64、simulator-x86_64。逐切片验证真实二进制和消费者链接；相同 CPU 不代表相同操作系统或真机/模拟器 ABI。iOS 排除不可用的原生 JIT，不以改名模拟支持。

C# 多目标包按 TFM、原生库和资源实际选择；Python 按当前声明的 CPython 矩阵及移动切片选择。使用匹配的 .NET、Xcode、NDK 和目标依赖；SDK 编译版本与最低可运行系统版本分别记录。

## 交叉构建与验收

流水线必须区分源码/配置预检、真实构建、候选组装、静态校验、目标运行验收、发布与最终复核。支持交叉编译的目标应生成真实目标产物；缺少本机运行环境不等于不能编译。成功构建且完整性校验通过的包可以纳入 Release，包括尚未验收或运行验收失败的包，但必须保留明确的验收状态，不能把构建成功等同于验收通过。

构建主机和目标身份分别记录。交叉构建使用目标编译器、sysroot、头文件和完整依赖；需要运行的生成器使用构建主机版本。原生程序、C# AOT、Python wheel、安装器和第三方库分别确认支持，不能仅改变 RuntimeIdentifier、文件名或标签。Windows/macOS/Linux ARM64 不共享移动端 ARM64 的验收结果。

验收记录区分 `passed`、`failed`、`not-run` 和 `not-applicable`，说明原因、执行环境、工具链、配置和证据摘要。交叉编译/链接、模拟器、真机、图形桌面、安装生命周期、签名/公证、C# AOT/裁剪和 Python 应用分别记账。真实构建及包体、架构、来源、依赖、清单和 SHA-256 校验是分发前必需条件；运行或设备验收缺口不再阻止已构建产物一并分发。未执行、失败、未签名或缺少工具链的检查不得写成通过。索引业务的 `qualification` 以版本 1 记录检查并绑定 `manifests[]` 的路径和摘要；任何载荷变化均使旧记录失效。目标索引的 `complete` 仅表示所需产物库存完整，`qualificationStatus` 汇总验收状态；只有库存完整且全部必需检查通过，`qualified` 才为 true。缺少记录为 `not-recorded`，不得推断成功。

macOS 安装器使用包含标准 `.app` 的 `.dmg`，携带正式 ICNS、Info.plist 和可执行载荷；Linux 安装器使用保留执行权限的 `.tar.gz`，Windows 使用 `.exe`。SDK 压缩归档的格式与安装器形式独立。先完成代码签名处理，再封装磁盘映像并校验；ad-hoc 签名不等于 Developer ID 或公证。非 macOS 主机审计 DMG 时需要构建工具 7-Zip（`VEROSCRIPT_7ZIP` 或 PATH），不增加产品运行依赖。

桌面交叉组包入口为 `Resources/Tools/Packaging/package-veroscript-desktop-cross.py`。ABI 字符串由目标编译器对正式头文件表达式求值，不能复制宿主 ABI。无法执行目标时，仅在声明来源逐文件一致、至少两个原生平台导出的目录一致时复用中立声明目录；32 位目标必须导出自身目录。可执行的目标工具或模拟器可通过 `--target-tool` 接入，目录查询仍不等于完整运行验收。宿主安装包工具可进行跨目标构建、摘要验证和封装，实际安装继续要求平台与架构匹配。

签名、公证及其他改变文件字节的后处理必须完成后，再记录最终分发摘要并绑定验收证据；内部载荷库存仍独立验证。不得沿用签名前摘要认证签名后的文件。发布后重新核对最终目录、归档、引用路径及内嵌载荷，任何字节变化均使对应证据失效。

## 发布、取消和清理

所有发布和清理共用集合锁。先验证候选、支持范围、引用闭包和来源，再在受管暂存区准备文件、清单和索引，按可恢复事务提交业务内容、目标索引及根入口；根入口最后提交。失败恢复本次修改的内容及引用。读者使用集合锁或代际/摘要复核，更新中不一致时重试，不能声称整个集合一次原子交换。

取消必须停止本次任务的构建子进程和传输服务，不发布未完成候选，不覆盖已有正式产物；保留必要诊断，恢复临时环境设置。再次启动前核对源码、配置、工具链和候选摘要，过期验收不得复用。

清理以全部目标索引和清单依赖的引用闭包为依据。默认不保留无引用旧版本，但仍被其他平台、Cpp 或 GUI 附加插件引用的版本必须保留。公共包最后一个引用消失后才可清理；未知、非受管文件或路径校验失败时停止，不按年龄或文件名猜测所有权。只清理经过验证的专用临时目录，不删除源码、用户文件或外部依赖目录。

清理旧格式时先核对所有权、源码保护范围、占用进程和可复用工具链，再删除旧 Release 产物及已无用途的候选/缓存。保留必要诊断，不保留旧包恢复副本，不继续解析旧索引的依赖来维持旧集合。之后从干净暂存区按新规则全量构建、验收并发布，产品版本以发行登记为准。新格式集合内部仍被当前目标或依赖引用的有效版本继续按引用闭包保护；这不构成旧规范兼容。

## 实施门禁

新实现必须提供：与真实生产器共用规则的纯规划；公共包引用与版本选择；分类和生态元数据校验；跨范围依赖、锁、事务回滚及引用清理测试；污染源码包与旧格式的拒绝；逐目标能力及验收记录；干净重建与失败恢复验证。生产器与验证器未完成这些要求前，不发布新产物，也不以旧规划输出认证新布局。

仅更新规范时不启动编译、安装器生成或发行打包。同步六语言、导航和现有实现说明，并运行下列文档校验；它们只临时集成检查，不向 Release 发布产物：

```text
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```
