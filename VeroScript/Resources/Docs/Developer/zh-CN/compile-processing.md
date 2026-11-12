# 通用编译处理与源码元数据

核心 0.1.1 提供宿主注册的原生编译处理器。处理器可以转换源码、生成完整类型和成员、修改声明、补全或替换正文，并附加注释和注解。生成结果进入普通访问检查、泛型实例化和字节码编译。GUI、RPC 或序列化的业务规则由扩展定义。

## 注册与调用

使用 `VeroScriptCompileProcessing.h` 中的 `FCompileProcessorRegistry`，填写 `FCompileProcessorDesc`，注册后调用 `Freeze()`。将注册表放入 `FCompileOptions::CompileProcessing.Registry`，通过 `VeroScriptCompile::CompileSource` 或 `CompileFile` 编译。

描述包含实现身份、版本、配置键、顺序、阶段、订阅、允许操作和工厂。阶段与操作使用 `CompilePhaseMask`、`CompileOperationMask` 构造掩码；操作掩码为 0 时可以只查询和报告诊断。同一事件匹配多个订阅只调用一次，不同处理器依次按 `Order`、`ProcessorId` 排序。处理器排序不会使互相冲突的操作合法。

工厂为每次编译创建独立实例，实例在该任务的多个阶段复用。回调中的上下文、AST 和查询指针只在回调期间有效；需要保存的信息应复制为自有值。最终准备结果不保留实例或回调。宿主应通过工厂捕获共享所有者，保证原生模块及配置在任务期间存活。

原有 `FAnnotationProcessorRegistry` 是便利入口，其输出接入相同的声明事务与包记录：新增方法进入 Members；默认正文在 Bodies 处理，已经绑定的用户正文优先。新扩展推荐直接使用通用注册表，并明确声明所需阶段。

## 固定阶段

| 阶段 | 输入与允许操作 |
| --- | --- |
| RawSource | 原始模块文本；可提交 EditSource，包括自定义语法与导入转换 |
| ActiveSource | 宏和条件编译后的文本；可提交 EditSource，必须保持已解析的导入序列与宏边界 |
| Types | 原始声明轮廓；新增或替换 class、struct、interface、enum、类型别名及其元数据 |
| Members | 已登记类型；新增或替换字段、构造、析构、方法、自由函数与全局变量，或设置元数据 |
| Bodies | 已绑定用户实现、已解析正文；CompleteBody、ReplaceBody、SetMetadata |

Types 完成后绑定类型、别名和继承；Members 完成后绑定跨文件定义并解析暂存正文与默认值。Bodies 不改变布局或签名，之后进行完整语义检查。没有对应处理器时不调用该阶段的原生回调。

订阅可以选择 Module、Declaration、Annotation、Comment、Syntax。注解名称遵循定义的大小写策略，默认精确匹配，例如 `@Param` 与 `UMETA` 是两个名称；`@Param` 和 `@Param()` 是同一语法含义。结构阶段只把原始声明、注解和注释作为默认候选事件，生成或后附加的标记不会递归触发。后续阶段可以从只读目录查询已经生成的声明。

## 提交生成结果

`AddDeclaration` 的 `FCompileScope` 指定逻辑模块、namespace 路径、可选类型 OwnerId 和当前 Snapshot。顶层类型在 Types 生成；类型内部成员在 Members 生成。使用普通源码片段表达访问权限、export、构造、virtual、override、final 和泛型参数，核心不推断业务规则。

`AddDeclaration` 不携带 TargetId；新增类型成员通过 Scope.OwnerId 指定所属类型。携带替换目标的新增操作会在处理器入口和包解码时拒绝，不能绕过替换权限及目标校验。`ReplaceDeclaration` 保持目标种类、名称和泛型参数数量。`CompleteBody` 只接受缺失正文的可调用声明；`ReplaceBody` 替换已有正文。纯虚方法或 deleted 声明不能提交正文；纯虚析构沿用语言中仍需提供正文的规则。目标修改必须携带当前 Snapshot，过期目标、同目标修改、父子替换以及旧正文节点与正文替换的冲突会产生诊断。

替换声明时，未提供新注解或明确注解集合会保留原有注解，同时维护 `has_annotation`／`metadata` 查询所使用的普通投影。未覆盖的前置／行尾注释及说明保留；新源码中的声明注释按普通规则关联，被替换正文或类型作用域的旧内部注释不再挂到新声明。明确提交的空集合仍表示清空。

整体替换声明会清理旧成员、参数和语法节点的元数据子树，再登记新声明内容。旧子声明的源码注释标记为 Removed，不会转为模块说明，也不会从包的初始快照恢复为当前节点。被替换目标的稳定身份和外部说明继续保留。

替换正文同样保留声明说明，清理原正文的语法／注释节点，再收集新正文。旧注释仅在历史文档中保留并标记为 Removed，不参与当前声明查询；发行包中的泛型正文重放遵循相同规则。

`FCompileMetadata` 可以明确替换注解或注释集合；未填写与填写空集合具有不同含义。片段的 `Children` 用片段内字节范围与节点种类选择一个解析后代，必须恰好匹配一个节点。种类为 `declaration`、`expression:<EExprKind 数值>`、`statement:<EStmtKind 数值>`；片段包装、延迟正文和源位置换算由核心完成。

注册描述的 Operations 是权限掩码，可以组合；每条提交操作的 Kind 必须是一个合法操作，不能组合多个种类。非法种类在应用前拒绝，诊断包含处理器身份和事件来源。

`EditSource` 使用输入文档的 UTF-8 字节范围。可选的连续区间映射使用替换文本内范围和拥有存储的输入来源；精确映射长度必须一致。没有精确映射的替换部分锚定操作 Origin。Raw、宏与 Active 映射随元数据保存，`ResolveSourceOrigins` 可查询来源链。Active 不能引入预处理指令或改变导入路径、顺序和重导出标记。

完整 C++ 样例位于 `Resources/Examples/CompileProcessing`，演示根据 `@Builder` 生成 CardBuilder 类型及 `Card::Create()` 静态工厂，原始代码可显式声明生成类型并进行链式调用。

## 注释 注解和说明

`VeroScriptSourceMetadata.h` 提供拥有存储的文档、节点、注释、完整注解参数树和说明快照。参数区分 Flag、Scalar、Group，Scalar 再区分标识符、字符串、整数、浮点和布尔值；保留重复记录、空值、原文与范围。

RawSource 的注释事件来自原始源码，ActiveSource 来自当前有效源码；Types、Members、Bodies 只派发当前有效文档中的原始注释，每个阶段派发一次。历史文档可查询，不重复参与事件派发。条件分支中未参与编译的注释不触发声明阶段处理。

生成文档中的注释同样参与声明／正文关联及反射，支持声明前的 export、类型默认访问修饰符、访问标签和 template 前缀。前缀之间可以包含注释，模板头注释内的尖括号不影响关联；不重复触发注释处理器，也不覆盖所属模块的根元数据。参数声明视图携带其源码位置。无效 `source_node_info` 的参数数量和子节点集合为空。

`FHostBindingRegistry::RegisterAnnotation` 可以单独注册说明，不要求处理器，也不会赋予业务行为。参数说明支持 Children。实例以 DefinitionId 关联生产者说明；包中的说明与消费者目录分别保存，不按名称覆盖生产者内容。

编译期间保留完整信息，产物通过 CommentRetention 选择 Full、Documentation 或 NoComments。Documentation 保留文档注释及已经计算的声明说明，包括普通前置注释提供的回退说明。Unavailable 表示没有源码信息。NoComments 仍保留注解及独立注册说明；Full 的空注释集合与没有保留注释不同。包中已裁剪的信息不能由消费者恢复。

脚本的 type、field_info、method_info、function_info、global_info、module_info 和 enum_member_info 可查询 `annotation_count()`、`annotations()`、`find_annotations(name)`、`comments()`、`documentation()`、`comments_available()`、`comments_retention()`、`declaration()` 和 `source_metadata()`。

返回的 source_node_info、annotation_info、annotation_argument_info、comment_info、annotation_definition_info 支持结构查询、范围、说明、参数树及严格的数值访问。参数查询保留所有重复记录。不存在的说明返回无效信息对象；越界索引或错误的数值种类产生正常运行诊断。

annotation_info 与 annotation_argument_info 的 `argument_count()`／`arguments()` 查询注解实例参数；annotation_definition_info 查询注册说明的参数目录，两者数量可以不同。未注册的注解或没有说明的参数，其 `definition()` 返回无效对象，名称、说明、详情、提供者为空，说明参数数量为零；继续按索引取说明参数会产生越界诊断。

信息对象持有不可变元数据快照，热重载后旧快照仍可读。查询元数据不会授权跨模块代执行方法或访问对象，既有可执行反射规则继续生效。

## 准备结果 包和缓存

源码元数据编码在同一快照内复用重复路径和标识字符串，完整保留原始值、文档与节点身份、位置及来源映射，编码总量上限仍为 64 MiB，展开后的字符串载荷另限 512 MiB。解码器也接受此前直接存储字符串的元数据格式。准备结果指纹、字节码和包共用此编码；核心实现身份变化后，需要重新构建匹配的核心与原生后端。

`FCompileResult::PreparedProgram` 提供稳定声明目录、生成虚拟文档、不可变元数据、操作记录、配置指纹和依赖。`MatchesHost`、`MatchesProcessing`、`MatchesDependencies` 用于复核候选结果，非缓存处理器的结果不得跨任务复用。源码、覆盖层、宏、导入与普通编译选项还应纳入宿主自己的缓存输入键。

直接 AST 可以使用 `PrepareWithProcessing` 的结构阶段。RawSource、ActiveSource 必须通过已经配置的源码加载服务，准备时移交拥有处理会话的 unique_ptr 程序；不能从普通 AST 推断之前已运行的源码转换，也不能克隆原生处理器的跨阶段状态。准备结果由核心建立的证明校验，调用者设置一个完成布尔值不能跳过检查。

开放泛型包保存转换后的模板源码、阶段操作、输入摘要、片段元数据和生产者初始快照。消费者按 Types、Members、Bodies 重放，不加载生产者处理器、不再次转换源码。闭合成员继续由包字节码交付，泛型沿用普通参数代入。结构变化后须重新生成旧缓存和开发包。

包中包含类型生成操作时，消费者同样先暂存正文，完成类型重放后再解析和检查；因此可以直接引用包中导出的生成类型。生成注释在重放时恢复，以保持成员生成输入检查及最终查询结果一致。操作来源映射的线格式偏移在转为本机宽度前执行范围检查。

取消是协作式的。核心在回调前后、内部遍历和阶段应用时检查；处理器在长计算中调用 `CheckCancelled`。依赖读取使用 `ReadDependency`，记录存在和缺失状态。操作、事件、输入、生成文本、元数据及依赖数量均有限额，原生阻塞调用不能由核心强行中断。

## 其他模块的接入

`FCompileResult::PreparedProgram` 是共享准备结果。C++ Session 提供 `SetCompileProcessing`，源码生成宿主传入 `FCodegenOptions::CompileProcessing`，Tooling 分析与协议选项携带相同配置。原生 Interop 宿主可通过 `VeroScriptInteropCompileProcessing.h` 的 `CreateCompileProcessingService` 创建不透明处理服务。这些接入点已经存在，不应再描述成仅完成 Core 的未来计划。

注册表与原生代码所有者由宿主显式提供。普通 CLI 不自动发现任意原生处理器工厂；脚本扩展及显式选择的原生模块沿用各自配置的加载入口。消费已准备程序时不得重跑处理器，也不得通过重新解析注释构造另一套声明。JIT 执行处理后的业务代码。平台与应用验收仍须分别完成。

## 注解名称匹配

按定义设置 `FAnnotationDefinition::CaseSensitive = false`，或传入 `FHostBindingRegistry::RegisterAnnotation` 的最后一个参数，或使用脚本 `annotation_definition.case_sensitive = false`。绑定目录 JSON 使用 `caseSensitive`；C Interop 的 `name_flags` 使用 `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE`，C# 使用 `CaseSensitive`，Python 使用 `case_sensitive`。原生扩展传递同一策略。大小写折叠仅处理 ASCII，不依赖系统区域设置。活动注册的名称范围重叠时拒绝注册。定义身份、编译缓存及模块环境指纹包含策略；字节码、库包和开放泛型保留生产方定义。消费者注册不会改变已编译注解的策略。元数据契约变化后应重新生成开发期产物。

## 作用域声明与块生成

声明视图提供 `ScopeId`、`ParentScopeId`、`bLocal`、`bNested` 及语义所属关系。`FCompileProcessingContext::Blocks` 是仅在回调期间有效的只读借用视图，包含块和语句边界锚点。Types 阶段通过 `FCompileScope::ScopeId`、`InsertionBeforeId` 添加局部类型或别名；空块或末尾追加使用结束锚点，module／owner 字段保持原意。

脚本处理器使用 `context.blocks()`、`context.block_scope(scope_id, before_id)`。块和锚点通过 `scope_id()`、`statements()`、`id()`、`range()` 等 getter 查询；源码范围保持普通值字段。不能自行构造 scope 或任意偏移绕过快照校验。

同一锚点上的操作顺序确定，声明从插入位置起可见。替换包含它的正文或声明与操作旧子锚点冲突；补全或替换正文会重建局部声明目录。生成的子声明接受普通检查，不再次触发处理器；泛型实例化替换准备后的声明，也不重跑处理器。作用域身份、生成操作、子目录及所属关系随 VSBC 和库包保留。0.1.1 布局身份变化后应重新生成开发产物。

## 声明追加批次与所属类型引用

`Private/Processing/VeroScriptDeclarationProcessing.cpp` 的 Members 阶段 `DeclarationAppendsOnly` 路径要求所有操作都是 `AddDeclaration`，且 `TargetId`、`ScopeId` 为空。无 `OwnerId` 的命名空间辅助函数可与已在校验快照中的 class／struct／interface 成员交错追加。阶段、快照、可写模块和冲突检查仍然有效；新生成的所属类型不会成为旧快照中的可操作目标。

`ReuseOwners` 仅在 `P.Classes`、`P.Structs`、`P.Interfaces` 的存储地址未改变时借用该声明快照。生成的辅助函数正文仍可能追加局部类型并扩充这些容器；存储变化后通过 `Collect(P)` 重新收集引用，不跨容器扩充保留声明／AST 指针。维护时检查交错辅助函数和成员、泛型所属类型、虚调用／接口调用及局部声明；`Private/Tests/VeroScriptCompileProcessingTests.cpp` 的 `FInterleavedAppendGenerator` 提供源码场景，其存在不代表测试已经运行通过。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

Language: [en-US](../en-US/compile-processing.md) · [fr-FR](../fr-FR/compile-processing.md) · [ja-JP](../ja-JP/compile-processing.md) · [ko-KR](../ko-KR/compile-processing.md) · [zh-CN](compile-processing.md) · [zh-TW](../zh-TW/compile-processing.md)
