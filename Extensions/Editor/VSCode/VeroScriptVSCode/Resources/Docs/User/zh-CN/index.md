# VeroScript VS Code 插件

部分编辑器版本限制空工作区中的终端进程。如果运行/检查因脚本工作目录而被拒绝，请将该目录作为工作区打开。运行域附加不使用终端。

## 安装与开始使用

使用桌面版 VS Code 1.106.0 或更新版本，在扩展视图选择“从 VSIX 安装”，安装本地插件包。解压与扩展宿主操作系统、架构匹配的 配套 Runtime，然后执行 **VeroScript: 选择 Runtime**，选择包含 veroscript、veroscript-lsp、veroscript-dap、stdlib 和 localization 的目录，Windows 程序带 .exe 后缀。

保存 .vs 文件后，通过命令面板执行 **运行文件**、**检查文件** 或 **调试文件**。基础着色、括号、注释和片段无需 Runtime。WSL、SSH 或容器环境需要在远程扩展宿主内安装插件与 Runtime；浏览器版不支持原生进程。

## 配置与路径

受信任的工作区可用 `veroscript.lsp.executablePath` 指定宿主提供的 stdio 语言服务器，路径遵循下文的共享路径规则。`veroscript.lsp.arguments` 将数组中的每一项按原样传为独立参数，保留空格和空字符串。未显式配置的编译选项由宿主决定。入口留空时使用 Runtime 语言服务器，修改入口或参数会重启语言服务器。 自定义语言服务器继承扩展宿主的环境变量，初始化期限为十秒。

接受命名空间补全后会继续显示下一级成员，包括嵌套命名空间；关闭 editor.suggestOnTriggerCharacters 时不会自动续弹。VeroScript 默认将 editor.wordBasedSuggestions 设为 off，避免注释中的单词绕过语义过滤；可在 [veroscript] 设置中覆盖。内置模板按输入前缀匹配，并进行保守的词法位置检查：导入和入口模板用于文件作用域，声明模板用于支持的声明区域，循环模板用于语句起点。注释、字符串和未完成的表达式中不推荐模板；用户自定义片段仍由 VS Code 管理。插件保留原生筛选与排序字段，VS Code 还会根据输入前缀进行匹配。

悬停签名使用 VeroScript 代码块，重载、说明和定义位置分段显示。补全详情避免重复签名，并保留原有说明、链接和示例。颜色跟随 VS Code 主题；参数提示中的当前参数仍由原生服务确定。

共享编译设置包括 veroscript.stdlibSearchPaths、importPaths、implicitImportPaths、defines、predefinedMacros、bindingCatalogPaths、allowCircularImports 和 allowProjectWideAccess。相对路径以文件所属工作区为基准；无工作区时以源文件目录为基准。共享路径支持 ${workspaceFolder}、${env:NAME}，运行/任务路径还支持 ${fileDirname}，任务 program 支持 ${file}。没有对应上下文或变量无法展开时会报错。

数组显式配置会替换继承值。stdlibSearchPaths 为空时使用 Runtime 标准库，其他空数组用于清空继承值。预定义宏包含 name、parameters、variadic、body；绑定目录仅提供元数据和桩绑定，不加载宿主的真实原生实现。

每个工作区根拥有独立 LSP；嵌套根和工作区之外的文件有明确的文档归属。普通配置更改会发送到原生服务；Runtime 更换会重启进程。替换 Runtime 文件后可执行 **重启语言服务器**。反复崩溃时，一分钟内最多自动重启三次，之后查看 **显示输出** 并处理原因。

导入文件的磁盘变更依赖文件系统通知。如果 Windows 挂载目录不能提供这些通知，请改用 Linux 原生文件系统中的工作区。

已配置导入/标准库目录及隐式导入中的打开文件，其未保存内容也会同步给引用方的语言服务器，诊断和编辑功能仍由文件所属上下文提供。配套 Runtime 主要发布被编辑文件的诊断；其他引用文件的诊断会在该文件重新分析时更新，例如编辑或保存该引用文件后。

交互请求预算默认 2000 毫秒，后台分析默认 5000 毫秒。可通过 lspRequestTimeoutMs、lspAnalysisTimeoutMs 和索引/结果上限设置调整。lspTraceAnalysis 可以记录原生分析耗时。

## 运行和检查

插件命令会检查入口、所属根及已配置导入范围内打开的未保存 .vs 文档，提供“保存并继续”。取消或保存失败则中止。直接执行 tasks.json 任务遵循 VS Code 的 task.saveBeforeRun；选择 never 会按磁盘内容执行。

任务 type 为 veroscript，action 为 run 或 check，program 是入口文件。还可覆盖编译选项、backend、args、cwd、env。program 的相对路径以最终 cwd 为基准；编译路径以所属工作区为基准。任务 env 显式配置时替换 run.env，未指定则继承。参数按独立 argv 传递，终端保留原生退出码和标准输入。

插件启动 Runtime 进程时会清空继承的 VERO_PATH、VERO_STDLIB_PATH。请使用路径设置配置导入。env 不允许重新覆盖这两个变量。CLI 默认通过 VERO_LANG 跟随 VS Code 的诊断语言。

## 调试

F5 或 **调试文件** 使用原生 VM DAP。launch 支持 program、cwd、env、args、input、stopOnEntry、maxCallDepth 和共享编译选项。launch env 不继承 run.env。保存检查在 preLaunchTask 之前执行；准备期间文件再次变脏时会中止，避免使用旧磁盘内容。

支持原生协议提供的行/条件断点、单步、调用栈、作用域、表达式求值和变量修改。运行期间修改断点可能被原生适配器拒绝。当前不支持函数、异常、日志、命中次数断点、反向执行和原生 restart。调试控制台用于表达式求值，初始标准输入使用 input；需要交互输入时使用普通运行终端。Ctrl+F5/noDebug 会提示改用 **运行文件**。

program 模式的 attach 需要 program、port，可选 host，默认 127.0.0.1。宿主需事先监听 TCP DAP。附加不需要本地 Runtime，不注入工作区编译配置、cwd 或 env。它在该宿主中加载程序，并非附加到正在执行的 VM/PID。显式选项及空数组遵循外部宿主的原生回退规则；源码路径必须在两侧都有效，暂不提供路径映射。

### 宿主内嵌脚本

执行 **VeroScript: 附加到宿主**，或在资源管理器中右键 .vs 文件选择该命令。插件使用选中或当前已保存的脚本；没有合适文件时打开文件选择器，然后询问宿主地址和端口，默认 127.0.0.1:4711。任一输入取消都不会启动会话。附加前提供源码保存检查，启动后停在入口；IPv6 地址不带方括号。F5 配置生成和 launch.json 补全也提供 attach 模板。需要固定入口、参数或显式编译选项时，请保存为 launch.json 配置。

连接宿主应用提供的兼容 TCP DAP 端点。原生代码使用对应调试器，脚本使用本插件调试。

编辑宿主 API 时通过 veroscript.bindingCatalogPaths 配置匹配的元数据，并使编辑器的导入、宏和标准库版本与宿主一致。这些设置不会给本地 Runtime 注册真实回调。附加命令不注入编译配置，需要覆盖时在 launch.json 中显式配置。插件无法发现宿主私有的导入配置，已配置源码/导入范围之外的宿主依赖请自行保存。连接地址由扩展宿主解析，包括 Remote-WSL/SSH/容器环境。

### 运行域在线附加

执行 **VeroScript: 附加到运行域**，或使用 launch.json 运行域模板。配置 request 为 attach、attachMode 为 domain，提供 host/port，可选 domainId 用来校验目标身份；为空或省略时使用端点提供的运行域。不需要打开文件或本地 Runtime。此模式不会保存、加载或重启脚本，拒绝 program、编译/运行覆盖参数及 debugServer。同一调试连接可跨越多次宿主调用。 宿主结束调试会话后，插件会完成断开流程，不请求取消脚本执行。

端点必须支持运行域在线附加，否则连接失败且不会加载程序。初始连接及初始化期限为 10 秒。请在输出中确认宿主／运行域身份以及源码／绑定修订；修订是不透明标识，须自行核对源码和 API 元数据，目前没有自动比较或 LSP 同步。断开后，执行和取消仍由宿主控制。连接丢失会结束调试，不自动重连；不能接管已在执行 JIT 代码的 VM。

暂停、单步、表达式求值、条件断点和变量修改取决于宿主支持的功能；不支持的请求会明确报错，普通断点仍可使用。停止／断开会结束调试连接，需要取消脚本执行时，请通过宿主应用操作。

## 可选 JIT 模块

Runtime 与 JIT 后端独立分发。veroscript.run.jitBackendPath 可选，用于在普通运行中固定兼容的库文件或目录。留空会省略 CLI 路径覆盖，继承模块环境的选择；未配置后端时默认为 auto。禁用 JIT 或没有可用自动候选时，mixed 使用 VM，强制 jit 在执行前失败。任务的 jitBackendPath 可以覆盖设置，空字符串清除继承值。模块相对路径基于所属工作区或源文件目录，不受 cwd 影响，并支持运行路径变量。检查、LSP 和本地 VM 调试不加载此模块；launch/attach 拒绝 jitBackendPath。在线运行域由宿主选择后端。目录发现及 ABI 校验由原生加载器负责；LSP/DAP 的 jitCapability 表示核心服务契约，并非已安装的后端。

## 内存类型与调试查看

使用匹配的 Runtime 时，`std::memory` 类型和泛型调用由原生工具提供补全、悬停及参数提示。调试时可在变量面板展开缓冲区和视图的查看字段；这些字段只读，继续执行或单步后旧句柄失效。宿主提供的内存元数据保持原样转发；当前不支持任意内存读写请求或专用内存编辑器。

## 当前语言工具与模块源码

原始字符串 `r"..."` 可以跨行，反斜杠按原文保留，遇到后续第一个引号结束；着色和模板过滤无需 Runtime 也遵循此规则。`.vslib` 库文件的变化（包括大写扩展名）会转发给语言服务器。

服务器公布模块环境及源码请求方法时，跳转会把 `veroscript-module:` 文档打开为只读源码快照。不同工作区的同名原生路径保持隔离。服务器重启或编译快照变化后，请从所属源文件重新跳转；旧视图保留原来的文本。这些视图用于源码展示和语法着色，不提供编辑或快照内的额外语言查询。

原生编译／注解处理器和模块环境需要由 LSP／DAP 宿主提供。默认 Runtime 服务不会自动导入宿主应用的处理器或 CLI 包环境。`bindingCatalogPaths` 提供元数据，包括编写扩展时导出的编译服务目录；它不会安装回调或处理器。项目依赖这些能力时，请使用配置匹配的宿主服务。

注解名称遵循宿主定义或目录的 `caseSensitive` 策略；忽略大小写时仅折叠 ASCII 名称。插件保留规范名称和原生签名参数标签，参数名仍区分大小写。修改已有 `bindingCatalogPaths` 文件会重新加载元数据，无需重启服务器。整理导入应用服务器返回的精确编辑，保留注释、原始字符串、未激活分支及现有换行符。

## 大括号布局与类型悬停

在空声明块或控制块中按 Enter，会将同行的左大括号移到独立一行，并缩进光标；输入中的 `void a{}` 也支持。原本已换行的括号、注释、字符串和有歧义的初始化器使用普通回车。沿用 VS Code 缩进、换行符和撤销，支持条件一致的多光标；混合位置使用普通回车。补全列表、代码片段和内联建议保留自身的 Enter 行为。关闭 `veroscript.bracesOnNewLine` 可恢复所有位置的普通回车。此编辑行为无需 Runtime，不改变整份文档的格式化策略。内置类型悬停将本地化类别与类型名合并为一个标题，并保留说明。

开关默认开启，可在用户或工作区设置中切换。跨行声明头和构造初始化列表使用普通回车，避免套用续行的缩进。

VeroScript 文件在标签页和资源管理器中提供简洁的 V 图标，适配浅色与深色主题。是否显示以及是否采用自定义图标由当前文件图标主题决定；插件不会切换你的主题。

基本类型和 array 等内置容器统一使用紧凑的类型标题。补全详情中的内置说明和元数据按说明文字显示，真实声明保留 VeroScript 代码块。源码别名、重载、链接和示例均保留。

反斜杠续行的预处理指令及行注释内部不显示内置模板，结束后恢复正常补全。续行注释同时保留注释着色和普通 Enter 行为。

使用支持此功能的语言工具时，类或结构体中的补全可插入符合条件的继承虚方法和接口成员实现；在空白插入位置也可使用对应的 refactor.rewrite 代码操作。插件保留原生签名、泛型实参替换、插入范围及可编辑的代码片段方法体；已实现、final 或不可访问的成员由语言服务器过滤。

通过转导出导航到传递依赖时，插件保留实际目标，并打开所属服务器提供的只读模块源码。

## 数值与变量编辑

使用支持此功能的 Runtime 或宿主时，VS Code 根据每个变量的 DAP `presentationHint.attributes` 显示只读状态，包括不可变 `variant` 的检查子项、嵌套载荷及监视结果。插件保留这些提示，不受会话是否支持变量编辑的影响。写入校验由原生调试器负责；宿主未提供的只读提示不会由插件猜测。

`std::bigint`、`std::rational` 和 `std::numeric::rounding_mode` 等数值类型在语言服务结果中保留原生名称。调试值保留原生显示文本，不转换为 JavaScript 数字。可用 API 以相匹配的 Runtime 及其标准库为准。

## GUI 配套插件

可选安装 [VeroScript GUI 插件](../../../../../VeroScriptGuiVSCode/Resources/Docs/User/zh-CN/index.md)，提供 `.vswml`／`.vswcss` 编辑及显式原生检查、编译、预览任务。它依赖主插件工具 API 1，本地版本需要配套安装。GUI Runtime 和提供者另行准备；`.vs`、LSP 和 DAP 仍由主插件负责。

请整体保留解压后的 Runtime，包括随包提供的 `Vm/` 解释器模块及其依赖。CLI、LSP、DAP 使用 Runtime 装配的默认 VM；`run.backend: "vm"` 选择的是执行策略。迁移或更新时一起处理匹配的 Runtime 文件，然后重启语言服务器。

<!-- docs-navigation -->

---

[用户文档](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
