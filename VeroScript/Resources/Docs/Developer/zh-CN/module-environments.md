# 已解析模块环境与原生扩展

## 职责与使用入口

Core 接收已经解析、筛选启用状态的不可变快照，并提供共享后端选择契约。包客户端与宿主读取项目配置、求解版本并提供已安装候选；Core 不下载、解压、安装，也不扫描安装位置或系统路径。安装不等于隐式导入所有声明。

宿主构建 `FModuleEnvironmentDesc`，调用 `FModuleEnvironment::Create`；将返回值设为 `FCompileOptions::ModuleEnvironment`，执行前再交给 `VeroScriptVm::SetModuleEnvironment`。源码导出使用 `FCodegenOptions::ModuleEnvironment`。可直接构建的公开 SDK 样例位于 `Resources/Examples/ModuleEnvironment`。

## 模块与可见性

`Scopes` 列出所有者与直接依赖，`EntryOwnerId` 指定入口所属范围；`bHostProvided` 明确授权所有模块访问宿主范围。`SourceRoots` 使用具有所有权的绝对规范文件路径；`Modules` 将公开导入名称精确映射到模块 ID、所有者、内容身份及文件，或带 `Source` 的 `veroscript-module:/...` 内存模块。

普通导入优先核对精确映射，与本地候选冲突时报错；显式 `./`／`../` 只使用导入者的相对路径，仍需通过根目录与依赖校验；目标缺失时不会改查其他搜索根。映射失效不回退到其他同名文件。`import`、`include`、导出导入及宏发现复用同一加载器。默认严格限制源码、关闭环境与工作目录等隐式查找；标准库也应显式提供搜索根及所属范围。

模块可以使用自身与直接依赖；普通再导出仍遵循语言可见性。原生声明携带 `FBindingOrigin`，编译时检查所属范围与导入可见性，公开 API 签名可以暴露使用其返回值所需的原生类型。这里是依赖隔离，不是执行不可信原生代码的安全沙箱。

发布后不得修改绑定声明、回调身份及提供者契约。`Apply` 事务化合并编译输入，拒绝冲突的活动目录。请以同一基础登记器构建统一绑定快照，不可直接拼接具有独立类型 ID 的活动绑定。显式源码覆盖仍按精确路径生效。

## 资源与本地化

`FResourceProviderBinding` 包含提供者、所有者、稳定契约身份及可选的纯资源 `ModuleIds`。`IResourceProvider` 接收 `(ProviderId, ModuleId, Group, Path)`、取消检查与字节上限；路径必须为不含穿越的可移植相对路径。成功但不存在与读取失败分别返回，存在的资源必须提供内容身份，可附带绝对物理路径用于诊断。

运行时使用环境的 `FModuleEnvironment::ReadResource`；原生处理器使用 `Context.ReadResource`；脚本编译扩展使用 `read_resource(provider,module,group,path)`。编译读取及“不存在”结果都进入现有依赖账本，资源变化会使准备结果失效。提供者须支持并行访问、遵守取消与预算，核心复核结果。`MaxResourceBytes` 限制单次读取，编译另有限制累计输入与依赖数。

复用现有 `I18n::FI18nServices`、类型化消息、上下文和限制，不增加另一套 JSON 本地化服务；包展示文本由包管理客户端解释。原生扩展可以通过 `GetService(id,version)` 获取宿主提供的类型化服务；确定性服务身份进入编译处理器缓存键。

## 原生接入与生命周期

包管理中的原生 ID（`entries.native[].id`）遵循 `[a-z0-9]+(?:-[a-z0-9]+)*`。包 Schema 允许 2–64 个字符，但 `Descriptor.Id[64]` 包含 NUL 终止符，实际可加载的原生 ID 最多为 63 个 ASCII 字符。描述符、清单 ID 与 `ExpectedId` 必须完全相同；`entries.backends[].module` 和静态注册键 `package:ID` 引用同一 ID。Core 底层描述符加载器只检查自身的有界 UTF-8 契约，不强制包命名语法。随附模块使用 `veroscript-sdl3`、`veroscript-rmlui`、`veroscript-stb`、`veroscript-font-backend` 与 `veroscript-gui-tools`；修改身份后需同步重建原生库、绑定目录与环境。

服务 ID、后端选择值及接口契约 ID 各自遵循原有协议。例如，原生扩展 `veroscript-gui-tools` 请求的是独立的宿主服务 `veroscript.gui.tools`，后者仍作为 `ToolServiceId` 查找键。

`VeroScriptNativeExtensionLoader.h` 提供 `LoadNativeExtension`，只接受显式绝对库文件，不搜索包名、PATH、安装目录或工作目录。静态模块使用 `QueryNativeExtension`。C 形式查询入口为 `veroscript_extension_query`，协议版本为 **0.1.1**；登记前校验期望 ID、版本、角色。

固定布局描述符校验公开 SDK ABI、核心构建、编译器／STL／运行库、平台架构、RTTI 与异常模式。C 查询不代表后续 C++ 对象具有稳定 C ABI。MSVC 动态扩展要求共享 CRT；iOS／WebAssembly 使用静态登记。二进制哈希仅作缓存身份，不承担真实性验证；客户端负责校验分发产物并持有不可变文件租约。

`FNativeExtensionRegistry::Add` 事务化登记模块。模块 `Create` 接收 `INativeExtensionHost`，登记函数、类型／成员、泛型、别名、注解词汇、编译处理器或服务提供者。模块使用宿主虚表，不另行链接静态核心。失败不发布半成品。通过 `GetBindings`、`GetProcessors`、`GetProviders`、`GetOwners`、`GetDependencies`、`GetFingerprint` 组装模块环境；请以 `GetDependencies` 的实际已加载契约填入可用依赖，不要仅以包版本推测；其中版本是原生组件版本，不是自动替换成分发包版本。原生绑定提供者必须具有精确可用执行契约。

声明只能在登记阶段修改。登记后保留的接口可创建宿主对象、读取资源、通过桥接提交编译操作。对象使用 `MakeHostObject`／`MakeHostObjectNamed`，由核心分配控制块并调用不抛异常的销毁函数。回调、处理器工厂／实例、提供者、服务实例及逃逸对象都保留模块代码。模块实例不要强持有反向拥有该登记的对象，避免引用环；此类关联使用弱引用。销毁函数不得抛异常。

登记提供者不初始化 GUI 等服务。上层选择服务／后端并校验契约后，显式调用 `CreateNativeService`；返回实例持有环境和代码。JIT 继续使用原有 `LoadJitBackend` 独立协议。

原生泛型沿用既有登记契约：发布只读环境前，在暂存的 `FHostBindingRegistry` 上用 `FHostBindingRegistry::InstantiateGenericHostType` 完成所需具体类型。执行期不修改已发布目录；脚本泛型仍由普通编译阶段实例化。

## 缓存、发行与重载

编译身份包含图、源码身份、绑定、处理器及配置、注解定义与编译策略；资源按实际读取加入依赖。执行身份另存运行配置。原生登记的配置键值自动进入处理器身份，即使宿主未另外填写配置指纹，修改配置也会使旧准备结果失效。缺乏确定性身份的宿主服务会禁用原生处理器缓存；其他影响生成结果的透明度不足的回调也应由宿主提供身份或禁用缓存。

`FExternalDependency` 保存精确 ID／版本／ABI／构建／内容身份，核心只校验、合并与序列化，不求解版本。字节码、`.vslib`／`.vsapp`、链接保留要求；执行在初始化与 JIT 之前拒绝缺失或不匹配契约。开放泛型保存生产者范围，导入方提供兼容绑定及映射要求；被打包的辅助模块所保留的其他所有者 ID，也须在有效环境中声明其范围与直接依赖。源码可已内嵌于库，但执行契约和作用域仍不可遗漏。产品与字节码版本仍为 **0.1.1**，不兼容旧的未发行字节码。

独立 AOT 可复用环境导出纯脚本及生成声明；具有运行时原生包契约的程序会明确拒绝导出，后续需要目标运行时适配后才能支持，不能静默换成普通外部符号。

重载在空闲边界发布新环境，现有任务持有旧快照。未声明 `NativeExtensionParallelRegistrations` 的模块再次登记需要重启。禁止原地替换已加载库，请使用新的不可变版本路径。原生对象带登记身份，旧对象不能进入新登记的回调，即便类型同名。线程、取消规则继续生效，阻塞的原生回调不能强制打断；已启动工作任务持有原环境。

## 验证与集成

`VeroScriptModuleEnvironmentTests`、`VeroScriptNativeExtensionTests`、公开头测试及 `VeroScriptModuleEnvironmentExample` 验证 Core 契约；动态测试库刻意不链接 Core。各发行目标仍需自身的加载和设备验证。安装及环境选择由包客户端、CLI、Interop/各语言适配器负责，Core 接收其已解析快照。

## 命令行宿主服务

`CommandLine` 角色（`0x8`，包内拼写为 `cli`）使用匹配 SDK 的 `veroscript.cli` 服务，版本为 `0.1.1`。CLI 提供 `ICommandLineExtensionService`；原生模块在 Create 期间通过 `GetService` 获取，并在注册关闭前提交描述符。组合角色模块应允许普通 SDK 宿主不提供 CLI 服务。包客户端在分析模式及未提供此服务的宿主中跳过仅有 CLI 角色的条目。参数解析和命令分派由 CLI 负责。

[CLI](../../../../../VeroScriptCli/Resources/Docs/Developer/zh-CN/process-and-assets.md)

## 后端选择与环境适配

`VeroScriptBackendSelection.h` 定义 `ResolveBackendSelection`、`SelectServiceBackend` 和 `ResolveExecutionBackends`。省略请求继承 `SelectedBackends`，配置中保存的空值表示 `none`；显式 `auto` 覆盖配置默认值。候选按 ID 排序，空、保留或重复候选 ID 被拒绝。自动候选仅在工厂创建／准备失败时继续尝试；取消直接传播，运行中的操作不重放。

`FBackendSelectionResult` 区分 `Selected`、`Disabled`、`Unavailable`、`Failed`，保留 `BackendId`、拒绝的尝试和错误。自动可选服务不可用时，环境构建仍可成功，调用者须同时检查返回的工厂。`FInstalledEnvironment::GetBackendCandidates` 仅查询元数据，排除禁用包；`LoadJitBackend`／`LoadVmBackend` 可选返回选择记录。`BackendDiagnostics` 保留不可用的自动提供者模块诊断。

`FModuleEnvironmentDesc` 携带 `SelectedBackends`、`ServiceProviders`、`BackendDiagnostics` 和 `ExecutionBackends`。包适配层提供不可变解析回调及 `ExecutionBackends.Identity`，并保留安装快照和代码租约；`AnalysisOnly` 不安装执行解析器。`FNativeServiceProviderDesc` 在选择头文件中声明，原生扩展辅助函数先验证服务／后端／契约／版本组合再创建实例。C、C++、C#、Python 共用原生 Session 解析，不分别实现包发现。

`ResolveExecutionBackends` 先准备输出副本，再提交。显式 `VmBackendFactory`／`VmBackendPath` 优先于环境默认值；工厂、路径与显式清除互斥。`FallbackVmBackendFactory`／`FallbackVmBackendPath` 是产品装配输入，仅在自动环境选择没有可用 VM 时使用，不能覆盖显式失败或禁用。`bDisableJitBackend` 阻止环境启用默认 JIT。Session 保留产品 VM 后备；`SetFallbackVmBackendFactory` 修改该后备，执行前可用 `SetVmBackendPath("auto")` 恢复。执行开始后 VM 域仍固定，包括 JIT；Clear／reload 不解除固定。工厂、实例和保留代码存续期间保留模块与环境所有者。

配置模块环境后，填写了 `Origin.ProviderId` 的处理器只接收有权访问该提供者的源码所属域事件，也可通过可见的已导出 `Origin.ModulePath` 生效。打包保留的生产者所属域、已捕获源码所属域优先于物理路径查询。未填写提供者的宿主全局处理器，以及无模块环境的宿主，保留原有作用范围。脚本处理器仍通过 `import compiler` 或配置的脚本根显式启用；事件筛选不授予源码、资源或原生绑定依赖。

<!-- docs-navigation -->

---

[开发文档](index.md) · [en-US](../en-US/module-environments.md) · [fr-FR](../fr-FR/module-environments.md) · [ja-JP](../ja-JP/module-environments.md) · [ko-KR](../ko-KR/module-environments.md) · [zh-CN](module-environments.md) · [zh-TW](../zh-TW/module-environments.md)
