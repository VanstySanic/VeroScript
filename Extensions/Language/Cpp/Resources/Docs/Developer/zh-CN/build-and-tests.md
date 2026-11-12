# C++ 构建与测试契约

## 目标

C++ 组通过 `VeroScriptCpp`、`VeroScriptCppAot`、`VeroScriptCppFrontend`、`VeroScriptCppBindGen` 和 `VeroScriptCppTranspiler` 构建。每个库目标排除专门测试源；测试链接其所验证的公共目标。

## 测试类别

测试覆盖公共头编译、引擎/会话集成、JIT 集成、AOT 源与 Bundle 输出、前端解析/合并/类型映射、BindGen 目录/引用生成，以及 Transpiler 源/映射生成。凡是改变公共结果、生成物、诊断或支持子集规则的修改，都必须扩展相应类别，不能只依赖端到端测试。

## 安装边界

公共头和生成库是安装输入；README 和生成 API 引用是文档，不是实现头。已安装的消费者不得需要构建树路径。

独立分发的 C++ 包在所需 `Runtime` 模块下提供 `VeroScriptCppConfig.cmake`。消费者将匹配的 Core SDK 根和 C++ 包的 `Runtime` 目录同时加入 `CMAKE_PREFIX_PATH`，调用 `find_package(VeroScriptCpp <version> EXACT CONFIG
REQUIRED)`，并链接 `VeroScript::VeroScriptCpp`。可选 `Tools` 与 `Aot` 组件仅在其同级模块已安装时导出导入目标。

C++ 包记录同一发布构建中 Core 库的 SHA-256 身份。若混用来自另一构建的同版本 Core SDK，配置必须失败。发布流水线在制作归档或安装器前，必须只使用暂存包路径编译并运行 `Extensions/Language/Cpp/VeroScriptCpp/Resources/Examples/CppMinimal`，不得使用仓库 include/library 路径。

## 内存集成门禁

`VeroScriptCppMemoryIntegrationTests` 覆盖 Engine 配额、Session 连续调用、原生内存服务、热重载身份保留及配额失败回滚、关闭域之前的脚本清理，以及 Session 销毁后的租借释放。
`CppMinimal/memory.cpp` 是安装后的 Runtime 消费程序。修改内存契约时，须同时运行这两项、`VeroScriptMemoryTests`、Session／调试／JIT 测试和 AOT 拒绝测试。可选后端参数用于验证真实 JIT 模块。参见[宿主内存规则](../../User/zh-CN/embedding.md)。

## 清理与 AOT 回归门禁

`VeroScriptCppSessionDebugTests` 覆盖清空、替换、关闭热重载、释放 callable 和析构时的清理回调重入保护，包括拒绝新增保留根及清理后复用。`VeroScriptCppJitContractTests` 检查结构化原生注册的缓存失效。发布前须使用 `VEROSCRIPT_AOT_REQUIRE_NATIVE=1` 运行 `VeroScriptCppAotHelloTests`，实际编译运行生成的 C++ 并与 VM 对照，验证自增／自减的副作用次数、生命周期、泛型数值宽度和溢出失败。`VeroScriptCppAotEmitTests` 还验证不支持的用户类型自增／自减会明确拒绝，包括泛型特化。

## 模块宏集成门禁

`VeroScriptCppEngineIntegrationTests` 覆盖宿主宏种子、源码覆盖、显式／转导出／隐式可见性、源码及文件缓存、依赖失效、热重载、依赖失败后旧程序保留、物理导入诊断，以及去除调试信息的库包宏接口。`VeroScriptCppAotHelloTests` 对公开可变参数宏和调用方展开执行原生／VM 对照。`CppMinimal/main.cpp` 通过暂存 SDK 头文件消费公开宏元数据。模块预处理或字节码接口变化时须运行这些门禁。

## 注解处理集成门禁

`VeroScriptCppAnnotationTests` 在 VM／Mixed／实际 JIT 下验证 Engine 检查和执行、Session 源码／文件加载及热重载、非致命诊断、依赖缓存和不可缓存处理器。还验证源码加载、文件加载、独立编译及重载期间的回调重入拒绝，以及取消后的状态保留和复用。`VeroScriptCppAotEmitTests` 检查提高生成预算后不会被第二次默认准备拒绝；原生 `VeroScriptCppAotHelloTests` 对照生成虚方法正文、跨文件泛型正文及声明／实现作用域。SDK 消费示例通过导出的头文件、处理器注册和 Session／AOT API 验证安装布局。

## 项目转译与路径事务门禁

`VeroScriptCppTranspilerTests` 验证最终模块边界、方法实现作用域、虚调用、默认参数及原文件诊断；`VeroScriptCppTranspilerNativeTests` 实际编译执行 C++ → VeroScript → C++ 的项目链路。`VeroScriptCppFrontendTests` 覆盖未支持语法的明确拒绝、路径规范化、硬链接去重及事务写入前的别名碰撞保护。批量解析、CLI 输入保护及输出事务共用 `FFilePathSet`；文件身份不可读取时保留原有比较逻辑。

## 终端宿主集成门禁

`VeroScriptCppConsoleTests` 在 VM／Mixed／实际 JIT 下检查终端能力、精确文本输出与 `print` 分组、权限拒绝、宿主回调失败、运行错误及指令耗尽后的样式／光标恢复，以及 Session 重复调用和普通输出流。`VeroScriptCppAotHelloTests` 的原生／VM 对照检查颜色与光标控制参数的求值顺序，包括校验失败前的副作用。暂存 SDK 的 `CppMinimal/main.cpp` 验证公共宿主接口及执行结束后的恢复；终端契约变化时须运行这些门禁。

原生门禁还检查有／无命令行参数的入口在终端适配器异常、标准及非标准异常、诊断流失败时返回失败并完成清理。

工作线程门禁还检查终端守卫构造中的标准／非标准异常会使任务失败，并确保终端恢复完成后才通知任务完成。

## AOT 求值与引用门禁

`VeroScriptCppAotHelloTests` 的原生／VM 对照覆盖普通、泛型、扩展及构造调用的参数顺序与转换，值快照、ref 参数、短路运算、复合赋值及全局 callable 初始化。函数值在参数之后求值，实例接收者先求值，与 VM 保持一致。后续参数扩容或重新绑定容器时，索引引用必须保持有效；索引转换失败时不得继续执行后续参数的副作用。纯标量表达式保持直接生成，捕获的值参数通过移动传递，避免额外复制。

赋值回归还检查保存的接收者销毁后，表达式结果仍是有效的值。简单索引写入将保存的容器写回原变量或字段；嵌套线性索引及限定全局变量则解析当前存储路径。右侧重新绑定时，别名和对象身份必须与 VM 一致。简单索引赋值在右侧副作用之后检查边界，引用路径则先转换索引。无需转换的纯标量索引写入保持直接生成，避免复制定长数组。

`CppMinimal` 消费示例在每次构建时，将匹配 Core SDK 的标准库和本地化资源部署到可执行文件旁，包括无需重新链接的增量构建。部署必须恢复缺失文件并更新已变化的 SDK 资源，在 Visual Studio 和 Ninja/Make 生成器下均成立。

参阅[C++ AOT — 引用返回与求值生命周期](../../User/zh-CN/aot.md#reference-return-lifetime)。

长期索引别名和索引引用返回需要托管存储路径。从临时值接收者取得的长期引用也需要托管所有者。这些情况会被 AOT 明确拒绝，请使用 VM 后端。

隐式结构体字段会执行其脚本字段默认初始化，包括泛型字段类型和定长数组。存储初始化不会执行结构体构造函数正文。

原生门禁验证局部字段别名保留、无参数调用的接收者生命周期、引用赋值目标只求值一次，以及数值引用的溢出和窄化。生成测试要求需要清理的接收者和返回局部别名被明确拒绝。成员、析构、静态字段及开放模板的定义位于完整类型布局和全局访问器声明之后；原生门禁覆盖前向全局访问，以及带约束模板的成员和静态字段。

门禁还验证直接及嵌套字段别名、for 初始化中的引用、成员及捕获的接收者、泛型字段和结构体字段默认存储。生成测试明确拒绝长期索引存储和临时值接收者别名。递归清理检查在可能注册泛型特化的查询之前保存类型名快照。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/build-and-tests.md) · [fr-FR](../fr-FR/build-and-tests.md) · [ja-JP](../ja-JP/build-and-tests.md) · [ko-KR](../ko-KR/build-and-tests.md) · [zh-CN](build-and-tests.md) · [zh-TW](../zh-TW/build-and-tests.md)
