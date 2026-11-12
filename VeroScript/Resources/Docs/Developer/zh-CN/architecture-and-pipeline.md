# 核心架构与编译管线

[可替换 VM 后端](vm-backends.md)

字节码采用固定标识 `VSBC`，格式版本跟随产品版本，当前均为 `0.1.1`。旧开发构建的产物须重新生成。

核心拥有语言实现，是原生适配器的依赖根。其公共范围包括 lexer、parser、AST、preprocessor、compiler、bytecode、VM、代码生成、JIT、模块加载、包、调试、宿主绑定、反射、序列化、并发、变更机制与区域设置选择。

## 管线

实现会解析源文件、覆盖层、导入和包输入；在保留源位置的同时进行 token 化与预处理；解析公共 AST；完成语义分析和类型/泛型/可见性检查；发射字节码和调试元数据；链接模块/包信息；最后经由 VM 或选定的 JIT 策略执行。

每个阶段都会返回结构化诊断。消费者不得从流消息或部分填充的字节码对象推断操作成功。

## 模块图与加载器输入

`VeroScriptModuleLoader` 会在合并程序前解析主模块、普通导入、隐式导入、stdlib 模块及包/模板模块图。导入路径和 stdlib 根目录会规范化并去重。源覆盖层按照规范化路径替代磁盘读取，使编辑器或 Web 宿主无需写入文件即可编译内存中的模块图。

预处理器 `-D` 定义和结构化预定义宏在影响源指纹前会被规范化。宿主类型名称会在解析前提供，因此诸如 `Counter c = Counter(1);` 的声明能依据宿主绑定目录正确分类。循环导入和项目范围访问策略是显式的加载器设置，不是隐藏的全局编译器状态。

引入 stdlib 根或模块资源时，加载器可合并相邻的 `localization/` 树。区域设置合并按规范目录 ID 进行，并保留 `en-US` 回退；因此资源包必须将其 stdlib 与本地化目录一同部署。

## 公共/私有边界

`Public/` 包含受支持的头文件。`Private/` 包含编译器、parser、VM、JIT 和内部数据结构。公共头文件测试确保已安装头文件不依赖缺失的私有文件。外部工具链和适配器必须使用公共契约；源码树内确实需要 compiler AST 或 runtime 内部结构的目标，必须由 CMake 显式授予私有 include 路径，并作为经审查的 friend 依赖，而不是 SDK 消费者。

<a id="core-vm-ownership"></a>

Core 提供程序、值、逻辑帧、语言操作、执行控制和调度；完整字节码解释循环属于独立模块 `VeroScriptBytecodeVm`（`Modules/Execution/VeroScriptBytecodeVm`）。后端 ID 为 `vs-bytecode`，安装包名为 `vs-bytecode-vm`。纯 Core 不隐式选择 VM 或 JIT。普通 Runtime 显式装配 `Vm/` 下的官方动态库；Interop、Web 和移动静态组合由产品层显式装配。 JIT: `Modules/Execution/VeroScriptAsmJit`; Core: `Private/JitSupport`.

## 静态声明、泛型与捕获

静态字段会注册为模块拥有的全局变量，并通过模块初始化完成初始化。静态访问可以使用类型 receiver 或已求值对象 receiver；后者在读取后备全局变量前，仍会为副作用求值 receiver。泛型类型实例化会替换字段类型，并独立注册其静态存储。

编译器会在 AST 中记录 lambda 捕获与泛型边界，并将其源位置带入字节码诊断。静态类型限制、捕获有效性、类型参数边界和继承规则都是编译器语义。frontend、AOT emitter 或工具链消费者必须使用记录的声明身份，不能从显示名称重建这些规则。

## 作用域分析控制与依赖元数据

`FAnalysisControlScope` 拥有调用线程上同步分析的取消和耗时回调。嵌套作用域会恢复上一层控制；回调捕获的对象必须在作用域期间有效，所有 `FAnalysisStage` 必须先于控制作用域结束。取消谓词返回 true 时，`CheckAnalysisCancellation()` 抛出 `FAnalysisCancelled`，与语法诊断分开处理；它不取消 VM 执行。被取消的词法器可以再次分词，不会保留不完整的 token 流。

`FAnalysisStage` 在同一线程报告毫秒耗时，其名称必须在阶段期间有效。观察回调的异常会被忽略，避免在栈展开时覆盖取消或解析异常。嵌套阶段的时间有重叠，不能直接相加作为独立工作量。

`VeroScriptModuleLoader::GetLoadedFileStamps()` 提供物理源码或包读取之前记录的修改时间和大小；内存输入和覆盖层没有磁盘记录。返回引用仅在下一次加载或加载器销毁之前有效。缓存使用方必须拒绝 `bValid == false`，并在复用 AST 前重新检查磁盘元数据，包括解析期间发生的变更。这些记录是元数据检查而非字节哈希；文档版本、选项和覆盖层仍需独立失效处理。

## 长度与索引

集合长度和线性索引使用 `int64`。底层存储保持 `size_t`，缩窄转换前同时检查脚本范围和平台范围。C# 容器仍受 CLR 限制。对应语义的字节码布局标识为 `0x43425356`（`VSBC`）。使用旧 32 位长度语义的字节码缓存必须从源码重新编译。C++ 宿主和 AOT 生成产物需使用更新后的头文件及标准库重新构建。

## 公开宏

参阅[预处理器 — 公开宏](../../User/zh-CN/language-basics/preprocessor.md#exported-macro-contract)。

VSBC 0.1.1 还在尾部保存经过校验的公开宏表；源码缓存和 `.vslib` 库保留该表，`.vsapp` 写入空表。旧二进制包和字节码缓存都需重新生成。模块预处理在最终输出的导入路径处暂停，目标接口准备完成后恢复；模块准备使用有界的堆上栈。

## 编译期注解处理

冻结后的原生处理器注册表通过 `FCompileOptions.AnnotationProcessing`、C++ Engine/Session、Tooling 或 `FCodegenOptions` 接入。裸 `@Name` 等价于 `@Name()`，保留有序标记、命名标量参数及嵌套分组；未知注解仍为元数据。单一声明阶段先绑定用户补定义，再生成方法正文及辅助方法，随后复用普通检查、虚方法分派及泛型代入。跨文件定义需要加载声明和实现模块。包保留生成成员及开放模板，消费时无需生产者处理器。Tooling 公开只读 `GeneratedDocuments`，嵌入工具负责显示虚拟文档；独立工具不会自动加载原生处理器，由宿主注册。C# / Python 通过 Interop 接入原生服务句柄。VSBC 0.1.1 保存结构化注解及生成操作，旧缓存和二进制包须重新生成。原生样例位于 `VeroScript/Resources/Examples/AnnotationProcessing`。

CLI 回归已覆盖两个库各自编译属性实例化，以及裁剪调试信息后的应用包执行。

## 宿主重载

参阅[宿主绑定与绑定目录 — 宿主重载](../../../../../Extensions/Language/Cpp/Resources/Docs/User/zh-CN/bindings.md#host-overload-resolution)。

## 共用模板服务

`VeroScriptTemplate.h` 集中提供 Type／Value 有序参数表、精确有类型常量、有界纯求值、实参编码及开放声明／实例快照。实例由编译器拥有，不修改输入 AST；默认项在声明上下文绑定。实例键包含外层及自有实参，VM、JIT 和 Core AOT 执行确定后的具体字节码与布局。

`ModuleMeta::EnumerateTemplateDeclarations` 返回拥有自身数据的开放声明快照，不执行正文。函数和类型快照中的 `TemplateInstance` 区分自有与外层实参。类型使用规范类型名；值使用精确的 `@value:` 编码，移除该前缀后由 `DecodeTemplateArgument` 解码，不经过 double。快照字符串及参数表与返回对象同寿命；处理器回调视图仅在回调期间有效。

原生处理视图的 `TemplateParameters` 与脚本 `declaration_info.template_parameters` 提供一致的有序种类、默认项和外层标识。编译库即使导出普通类型，也保留其开放方法、必要私有依赖和重放记录；加载闭合应用不运行处理器。内部布局／构建身份随契约更新，产品与 VSBC 版本保持 0.1.1；源码／AST 消费模块需与核心头及库统一重建。独立源码导出器在输出前诊断不支持的方法／值模板与引用；Core AOT 使用已选定的具体实例。

`VeroScriptTemplateQuery.h` 的 `FTemplateCallTypeQuery` 为已准备的只读程序提供签名查询，复用核心推导、默认项及重载选择；不编译正文，不运行处理器或宿主调用。调用者负责查询结果的可见性与访问过滤。返回字符串采用查询用类型拼写，不能当作展示文本或序列化身份；程序与绑定目录须长于查询实例，每个查询实例仅由一个分析线程使用，取消按现有契约传播。

## 运行时优化边界

短整数调用计划仅用于无调试、无热重载、无执行治理的有界调用，并保留调用深度限制。参数和值捕获须为 int；整数字段的试算失败不提交写入，交还普通 VM 以保留错误前的副作用。存在变更观察器时不使用字段计划。计划不跨模块或程序代际复用，借用模块在执行边界清空缓存。BigInt 哈希在不可变负载上按需原子缓存，数值复制仍产生独立包装，并保留自定义协议路径。

FScriptObject 通过 GetHostBase()/SetHostBase() 访问宿主基类；普通对象不分配宿主／复制构造的附加记录，复制后修改这些记录会分离状态。C++ SDK 与 JIT 后端须使用匹配的头文件和 Core 身份重新构建；C ABI 不变。

对象类型身份通过 `FScriptObject::GetTypeName()/SetTypeName()` 访问。类型名经有界名称缓存共享不可变存储，在 VM／模块释放后仍有效。借用的名称引用在该对象更换类型名时失效；更换名称会清除运行时布局提示。可调用签名缓存以模块、代际及实际签名校验，并在重入期间保留不可变记录；参数、引用和返回值检查保持原有规则。

## 字段默认值与静态依赖准备

`RegisterStaticMethodDeclarations` 在普通字段布局前公布可调用签名。闭合泛型 struct／class／interface 先登记静态存储并公布所属描述符与方法声明，再由 `CompileOwnFieldDefaults` 绑定实例默认值。保留命名空间、访问上下文、类型／值替换及字段元数据；可能扩展描述符表的回调后按索引重新获取。解析器按词法声明身份解析自身类型，同时保留显式外层实参、根限定与值绑定。

`FStaticInitializationDeferral` 阻止模块初始化代码进入逐实例字段缓冲。`bStaticFieldInitializationStarted` 区分早期布局与静态初始化阶段，`PendingGenericStaticFieldInits` 保留待处理所属类型。`FlushPendingGenericStaticFieldInitializers(RequiredType)` 仅取出指定所属类型，维持无关类型的顺序。初始化代码仍按底层全局存储发射一次，不随字段默认值或运行时静态读取反复执行。

`Private/Compiler/VeroScriptStaticInitializationDependencies.cpp` 从分离的初始化器低层字节码准备依赖。`PrepareStaticInitializationDependencies` 跟踪选定的调用／构造、字段默认值、复制和局部析构，并用状态修订追踪函数值来源、捕获与引用写入。保存的闭包、保留的分配、可空字段和空容器不能仅因声明类型而引入执行／析构依赖。未知回调目标不构成完整运行时调用图，分析也不执行原生回调副作用。

先将消费者表达式编译到临时缓冲，准备依赖后再按匹配的调试位置及全局初始化范围追加。所需泛型初始化保持在短路分支及字段缓冲外。分析检查取消，递归准备前快照描述符，以活动／修订保护代替执行业务代码。保留无关初始化的正常顺序，不承诺任意循环依赖求解。

维护时检查字段默认值调用后续静态声明、嵌套自身类型、别名、const／引用访问、选定构造、函数值／引用写入、复制／析构生命周期及无关所属类型。分别验证 Core VM、所选 JIT 和 Core AOT；独立源码导出属于另一契约。源码场景在 `Private/Tests/VeroScriptStaticMemberTests.cpp`、后端差分套件及 C++ Core AOT 套件中；存在这些场景不代表已经运行通过。

## 十进制转换实现

`Shared/Private/VeroScriptFloatParsing.h` 的 `FromCharsDecimal` 由解析器与标量文本服务共享。检测浮点 `std::from_chars` 支持；后备路径直接转换到目标宽度，以有效前缀和 sticky 位限制输入，使用 C 数字区域及最近舍入，并恢复调用者状态。非零次正规数可接受；非零输入舍入成零时报告范围失败且不赋值输出。`ParserDetail::ParseFloatLiteral` 单独保留 `f` 字面量仍可由 double 表示的下溢例外。中点、负零、长尾数、区域设置及失败保留输出应分别验证；源码场景存在不代表平台验收通过。

Core 绑定目录 JSON 与 Tooling 的 LSP／DAP JSON 也使用这一转换。`vs_use_shared_private_headers` 提供私有 include 路径，共享头文件不属于公开 SDK API。

`Shared/Private/VeroScriptJsonString.h` 为绑定目录、Interop 和原生语言工具提供无 Core 运行时依赖的 JSON 字符串转义。追加操作只写入字符串内容，不负责外层引号或 UTF-8 校验；调用方保持既有控制字符转义形式，并传入与输出缓冲区不重叠的输入。这是私有构建输入，必须随源码分发并计入各使用方的实现指纹。

<a id="floating-formatting-support"></a>

`Shared/Private/VeroScriptFloatFormatting.h` 为包宏序列化、应用 i18n、Tooling JSON、Json 和 GUI 数字控件提供 `ToCharsDecimal`／`ToCharsGeneral`。存在浮点 `std::to_chars` 重载时使用该重载；兼容路径使用 C 数字区域设置，按原始 float／double 位宽选择可往返的表示，并恢复调用者的 errno 与舍入状态。这是私有实现支持，不是公开 SDK API。将其纳入 Core／目标 AOT、Json 绑定、GUI 编译指纹及源码发行，并独立验证兼容路径与标准库 charconv。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/architecture-and-pipeline.md) · [fr-FR](../fr-FR/architecture-and-pipeline.md) · [ja-JP](../ja-JP/architecture-and-pipeline.md) · [ko-KR](../ko-KR/architecture-and-pipeline.md) · [zh-CN](architecture-and-pipeline.md) · [zh-TW](../zh-TW/architecture-and-pipeline.md)
