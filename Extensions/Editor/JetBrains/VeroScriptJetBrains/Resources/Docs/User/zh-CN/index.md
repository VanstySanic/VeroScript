# VeroScript — Rider

## 安装

通过“设置 → 插件 → 从磁盘安装插件”安装 ZIP。Rider 2026.2.2（262.10315.191）是 SDK 验证基线及声明的最低构建；更高版本仍需兼容性验证。编辑、运行、检查及本地调试需要解压 配套 Runtime，在“语言和框架 → VeroScript”选择其根目录，直下包含 `veroscript`、`veroscript-lsp`、`veroscript-dap`、`stdlib/` 和 `localization/`。项目个人 Runtime 优先于应用默认值；两项都为空时通过 `PATH` 查找。基础着色无需 Runtime。替换工具后重启语言服务。

更新或移动 Runtime 时，保留完整的解压目录，包括 `Vm/` 及配套原生库。
工具负责提供默认 VM，无需新增 Rider 设置。

## 设置

项目语义设置使用经过验证的 JSON 对象。支持 `importPaths`、`implicitImportPaths`、`stdlibPath` 或 `stdlibSearchPaths`、`bindingCatalogPaths`、`defines`、`predefinedMacros`、`allowCircularImports`、`allowProjectWideAccess`、`lspTraceAnalysis`、`lspRequestTimeoutMs`、`lspAnalysisTimeoutMs`、`maxCompletionItems`、`maxWorkspaceSymbolResults`、`maxReferenceResults`、`maxWorkspaceFiles`、`maxWorkspaceFileBytes`。绑定目录描述符号，实际回调由宿主注册。共享设置保存在 `.idea/veroscript.xml`，Runtime 路径为个人设置。工具语言为 BCP 47，留空跟随 IDE；界面提供六语言。

## 编辑

`//` 注释行末的反斜杠紧邻 LF 或 CRLF 时，下一物理行仍属于注释。本地着色和空块回车遵循同一规则，GUI 标记中的内联 VS 也相同；反斜杠后有空格或制表符时不续行。

悬停将符号类别与名称分开展示，内置基础类型和容器类型统一使用一个类别标题；源码定义的类型别名保留原类别及声明。签名、重载、说明、示例及定义位置仍来自 Runtime，并保留其内容。

VeroScript 设置中的“在空代码块内按 Enter 时，将左大括号移到新行”默认启用。在同一行的空声明块或控制块内回车，会将左大括号移到独立行，并将光标放在内部缩进行。注释、字符串、选区、多光标及不明确或跨行的头部使用普通回车。仅修改此设置不会重启 LSP；全文及范围格式化仍采用服务端样式。

诊断保留 Runtime 的结果。`void t(){}` 是合法的完整函数；`void t` 尚未完成，需要 `()`。编辑期间的诊断会保留，在服务端接受补全后的代码时清除。

## 路径

项目路径以项目根目录为基准。工作目录也从根目录解析；入口、输入及运行覆盖路径（含 JIT）以解析后的工作目录为基准。继承的项目路径保持原基准；`$PROJECT_DIR$` 始终指向根目录。缺省运行选项继承项目设置，显式列表（包括空列表）在启动时替换；运行覆盖不修改 LSP 配置。

## 运行与检查

右击保存的 `.vs` 或新建 VeroScript 运行配置。没有 `main` 的顶层语句也可以作为入口。`run` 执行，`check` 只编译且不接受程序参数，`attach` 使用 Debug 连接。参数遵循 IDE 引号规则（`""` 为空参数），环境是字符串 JSON 对象。普通输入按字节传入，调试输入为预先准备的 UTF-8 文本。本地操作先保存文档。Current File 使用 Rider 设置；未显示时可在 Registry 启用 `run.current.file.item.in.run.configurations.combobox`。

上下文操作仅接受已保存的本地文件，独立选择 Run/Check，并清除模板中的附加专用字段。
Check 同时清除继承的脚本参数、输入及 JIT 路径，保留适用的工作目录、环境和编译设置。

## 本地调试与 JIT

普通运行支持 `vm`、`mixed`、`jit`。JIT 文件／目录字段可选，用于固定兼容模块。留空会省略 CLI 路径覆盖，继承模块环境的选择；未配置后端时默认为 auto。禁用 JIT 或没有可用自动候选时，mixed 使用 VM，强制 jit 在执行前失败。检查和本地调试不加载该模块；附加须清空路径。调试使用 VM。入口暂停为 `inherit`/`true`/`false`，深度 0 保留默认值；启动准备预算为 1–3600 秒，默认 30。支持行/条件断点、暂停、步进、栈、作用域、分页变量、求值及赋值；恢复后引用失效。自动悬停只求值名称路径，其他表达式使用显式求值。program 模式运行中修改断点会在下次暂停时同步。

变量子项每批最多显示 100 项；宿主只提供完整列表而不提供数量信息时也能正确展开。继续执行后，待展开的旧页失效。

调试器标记为只读的变量不显示修改入口；其他变量是否可修改仍取决于宿主能力和调试上下文。

## 程序附加

attach/program 连接 TCP DAP 宿主，不是任意 PID 或正在执行的 VM 接管。先启动 `veroscript --debug --dap-port=4711 script.vs`，配置地址和端口。入口留空继承宿主；工作目录、输入和环境留空，后端和深度由宿主选择。只发送显式语义覆盖。非空参数直接覆盖，勾选空参数覆盖可清空；部分空语义列表仍继承宿主，不能用于清空宿主默认值。

程序和运行域附加都会去除主机地址首尾空白，并拒绝内部空白或控制字符。

## 运行域附加

attach/domain 可填写预期运行域 ID，无需本地 Runtime 或当前文件。运行覆盖留空，环境 `{}`、入口 `inherit`、深度 0、后端 mixed，保留父环境继承。宿主须声明 `veroscriptSupportsDomainAttach`，否则明确失败且不加载程序。同一连接覆盖连续宿主调用，暂停、步进、求值、条件及修改遵循宿主能力。控制台显示身份及不透明版本，不自动比较源码或同步 LSP。物理源码路径须能在本机访问，不提供路径映射。适配器源码引用以只读快照展示。连接预算 5 秒，初始化 10 秒，运行域附加使用启动预算。断开发送 `terminateDebuggee=false`，取消和进程归宿主管理；连接丢失结束会话，不自动重连。

## 排查

通过语言服务状态、调试控制台和 IDE 日志排查，使用同一 Runtime 的完整工具。接入目标为本地 Rider 项目及 Windows/Linux x64 Runtime；其他 IDE、Remote Development、Windows 直接启动 WSL、macOS/ARM64、真实应用或 UE 运行域集成需另行验收。

```json
{"importPaths":["src"],"defines":["FEATURE=1"],"bindingCatalogPaths":["bindings/catalog.json"]}
```

[LSP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/lsp.md)

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/debugging.md)


## 受管理内存

使用配套的当前 Runtime 工具时，`std::memory` 的补全、悬停与语义着色由 LSP 提供。暂停调试后，在变量或监视中展开 buffer 或 view，可查看 `[byte_length]`、buffer 的 `[object_id]` 及可用地址。检查项只读，地址仅为临时观察值；插件不提供任意内存读写。继续执行后变量引用失效，需要在新的暂停位置重新展开。

[std::memory](../../../../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/memory.md)

## 编译扩展与生成源码

当前工具支持公共宏（`#define_export`、`export import`）及源码编译扩展（`import compiler "extension.vs";`）。请使用配套的 CLI/LSP/DAP 工具并保持导入路径一致。生成方法与虚拟模块提供 DAP `sourceReference` 时，会以只读调试快照打开。快照不是项目源码，不能 Run、Check、设置断点或同步到 LSP。读取源码失败会显示错误，但不会结束暂停的调试会话。原生宿主的处理器注册表与模块环境仍由宿主配置，插件不将其作为额外的语义 JSON 字段暴露。

<!-- docs-navigation -->

---

[用户文档](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
