# 开发文档 — Rider

编辑器与原生宿主的公共职责及六语言维护统一见 [公共编辑器集成](../../../../../../../../Resources/Docs/Developer/zh-CN/editors.md)。

采用 `VeroScriptBundle`／`DynamicBundle` 和本地化 `.properties` 资源。校验 `MessageFormat` 占位符及单引号转义，允许执行时再验证真实参数替换。

使用 JDK/JVM 25、Gradle 9.5.0、Kotlin 2.4.0、IntelliJ Platform 插件 2.19.0。SDK 固定 Rider 2026.2.2；`sinceBuild=262.10315.191`、`strict-until-build=262.*` 将安装限制在 262 分支，其他分支须另行验证。`VEROSCRIPT_TEST_RUNTIME` 指向当前 Runtime 后运行原生测试，否则明确跳过；可选 `VEROSCRIPT_TEST_JIT_BACKEND` 启用真实模块测试。SDK 结构检查和 ZIP 构建不能替代 Rider 界面验收。

```sh
./gradlew test verifyPluginProjectConfiguration verifyPluginStructure buildPlugin
./gradlew verifyPlugin
```

`gradlew.bat` (Windows) · `bash ./gradlew` (Linux) · `-PlocalIdePath=/absolute/path/to/Rider`

[Standards](../../../../../../../../Resources/Docs/Standards/zh-CN/GENERAL.md)


原生回归覆盖 `std::memory` 的命名空间与成员补全、源码拼写、悬停、诊断及同版本语义 token delta。DAP 测试通过插件的 LSP4J 传输展开 buffer、view 和监视，验证检查项拒绝修改，以及单步后对象身份保留、旧引用失效。元数据数值保持协议字符串；`variablesReference` 是不透明句柄，不是内存地址。

有子项数量信息时使用有界 DAP 分页，收到短页即结束展开；没有数量信息时获取一次完整快照并在本地分页，不能仅凭返回 100 项判断支持分页。异步响应与缓存页回调均须检查当前暂停代际和节点生命周期。回归覆盖空列表、99/100/101/250 项、短响应、恢复执行后的旧快照，以及真实 Runtime 的 100 元素数组。

被动调试悬浮提示只跟踪会话所属工程的编辑器。继续、步进及暂停请求的错误仅作用于该请求的执行代际；过期的失败不会影响新的暂停。当前代际的暂停失败会报告错误并保留连接，连接丢失仍独立结束会话。协议和弹窗测试夹具覆盖这些边界，包括暂停失败响应与停止通知的两种到达顺序。

导出别名回归覆盖通过源码门面使用 `export using` 和 `export typedef` 的补全、私有别名排除、悬停与定义导航。CLI Run/Check 还验证门面导入由同一 Runtime 构建的 `.vslib`。这些是原生协议及进程测试，不能替代 Rider 界面验收。

本地词法表与核心的 `constexpr` 关键字同步。DAP 变量的 `presentationHint.attributes` 包含 `readOnly` 时不提供修改入口；其他展示属性不改变可写性，宿主和运行域能力仍控制整体修改权限。回归用例覆盖关键字边界，以及只读、可写和附带其他展示属性的变量子项。

## 编译扩展与生成源码

DAP `stackTrace` 栈帧的 `column` 已使用从 1 开始的 UTF-16 列号，适用于本地文件、正 `sourceReference` 只读快照及在线调试域。编辑器行文本可用时，客户端只将计数起点改为 0，在该行范围内限界并避免拆开代理对；不能再次按 UTF-8 字节转换。坐标及控制器回归用例覆盖中文、补充字符和 CRLF。

调试源码引用在展示首批或分页栈帧之前异步解析。正引用号在单个连接内缓存为不可变只读文件；即使同时提供本地路径，也优先使用引用。快照不参与项目操作或 LSP。源码读取失败保留当前暂停；版本检查拒绝迟到的暂停结果，销毁时释放缓存。原生回归覆盖源码编译扩展导入、生成成员补全/悬停/导航及进入生成方法的单步调试，并覆盖导入公共宏。独立工具发现源码编译扩展导入；原生处理器注册表与 `ModuleEnvironment` 属于宿主 API 选项，不属于 LSP 初始化 JSON 设置。`veroscript/moduleSource` 提供宿主环境模块；当前插件启动独立 LSP，不连接宿主 LSP 端点。 失败的源码请求会移出缓存，后续加载栈帧时可重试；同一栈页内的重复引用只读取一次。

注解名称匹配由 Tooling/Core 处理；客户端保留源码拼写和服务端标签。原生回归覆盖不重启时切换目录定义的 `caseSensitive` 策略、LSP/DAP 中混合大小写的编译注解、`matches_name`/`case_sensitive` 补全及可调用类型参数标签。运行域协议夹具还覆盖 attach 响应之前到达的暂停事件，并验证能力信息可用及随后恢复执行。

## 编辑器展示

原始编辑器词法按预处理器规则处理行注释续行，仅接受紧邻反斜杠的 LF／CRLF，不忽略尾随水平空白。回归覆盖 token 重启、截断范围、线性字符读取量、GUI 内联着色，以及注释和已结束指令之后的空块回车。原生 LSP 回归覆盖嵌套可调用值、泛型、别名、数组／map 与隐式接收者链，以及私有成员遮蔽；DAP 回归保留 variant 检查项和监视子项的只读标记，同时验证普通局部变量仍可修改。

文档目标通过公开 LSP 传输异步请求，仅整理可识别的 Markdown 标题，保留服务端签名与说明。对此描述器关闭原生悬停目标以避免重复；通过文档修改戳、文件有效性和客户端状态拒绝过期结果，取消继续向上传递。IDE Markdown 渲染器提供跟随主题的代码块，调试数值弹窗独立保留。 所有服务端结果段落均仅合并类型类别后可识别的本地化内置类型元数据，源码类型别名与围栏代码示例保留原内容。

回车委托在 IDE 命令中执行一次替换，保留撤销并使用项目缩进。词法检查排除注释与字符串；指令、跨行头部、选区、多光标及需要扫描前 1 MiB 之外内容（含当前行后缀）的情况退回普通回车。项目大括号偏好不触发 LSP 重启；全文及范围格式化、诊断仍由服务端负责。测试覆盖渲染、异步结果失效、光标位置、撤销，以及从未完成函数到完整函数的原生诊断。Rider 界面验收仍需单独执行。

## 上下文配置

上下文配置只接受已保存的本地文件，将继承模板规范为 Run/Check，清除附加专用字段；Check
还清除脚本参数、输入及 JIT 路径，保留适用的工作目录、环境和编译设置。配置校验原样传播
IDE 取消异常，不能将其包装为配置错误。
Check 始终传递 `--backend=vm`，不改变已保存的 Run 后端偏好。

<a id="rider-platform-sdk"></a>

未设置 `localIdePath` 时，两个 Rider 项目通过 `rider(platformVersion) { useInstaller = false }` 获取多 OS SDK 归档，不能把平台安装器作为开发 SDK。`-PlocalIdePath=<directory>` 可选择已有本地 IDE。仍须符合上文固定的 Rider／构建分支、插件依赖及 JDK 要求，并分别验收目标 OS。

<!-- docs-navigation -->

---

[开发文档](../../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
