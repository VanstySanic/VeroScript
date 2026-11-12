# 脚本编译扩展

编译扩展使用普通 VeroScript 编写，通过 `std::compiler` 注册注解说明和处理器。核心在编译业务源码前加载扩展，处理结果进入已有的源码、类型、成员和正文处理管线。业务运行时使用生成后的普通字节码。

## 加载

业务入口的文件前部可以写：

```vs
import compiler "builder.vs";
```

前面可有 BOM、空白、注释和其他编译扩展导入。路径必须是字面量，扩展不能依赖自身尚未注册的处理器语法。普通 import 用于辅助代码，不执行注册入口；注册依赖使用 import compiler，拒绝依赖循环。后加载的业务模块只能引用入口或宿主配置已经加载的扩展。

C++ 宿主也可以设置 `FCompileOptions::ScriptCompileExtensions.Roots`，每项包含 Path 与可选 RegistrationEntry；`VeroScriptModuleLoader::SetScriptCompileExtensions`、C++ Session 和 Codegen 提供相同配置。`bAllowSourceImports=false` 关闭源码启用扩展，配置根仍可使用。源码覆盖层、导入路径和标准库根沿用现有加载规则。

## 注册与回调

扩展导入 `compiler.vs`，导出根模块自己的注册函数：

```vs
import "compiler.vs";
export void register_extension(std::compiler::registration reg) {}
```

回调签名为 `fn(std::compiler::context,std::compiler::output)->void`。注册接收处理器描述和一个零参数工厂；工厂返回回调，可使用全局函数或 Lambda。每个编译任务新建 VM 和持久状态，工厂一次创建，回调跨阶段复用；不同任务不共享脚本状态。

`annotation_definition`、`annotation_argument_definition`、`processor_definition`、`subscription` 用显式构造创建。数组赋值使用带类型的表达式，例如 `array<std::compiler::phase>{std::compiler::phase::types}`。说明可独立注册；Targets 默认为全部，说明不赋予业务行为。处理器的 Version、Phases、Subscriptions 必填，Operations 为空时可只查询和报告诊断。

原生和脚本处理器按 Order、ProcessorId 排序，身份冲突拒绝。生成声明不递归触发事件；同一批次读取应用操作前的快照，下一阶段可以查询已应用的生成结果。

## 查询与生成

context 提供阶段、事件、模块、声明、所属类型、源码、注解、注释、语法节点、声明目录与作用域查询。声明信息包含原文和规范类型、参数、默认值、修饰符、基类、泛型参数及约束；未绑定信息不会被当成完整语义信息。

常用 output 接口：

| 方法 | 参数 |
| --- | --- |
| add_declaration | scope、源码 |
| edit_source | source_range、替换源码 |
| replace_declaration | 声明 id、源码 |
| complete_body、replace_body | 声明 id、正文源码 |
| set_metadata | 声明 id、metadata |
| report_diagnostic | 文本、严重度：0 错误／1 警告／2 信息／3 提示 |
| submit_operation | generation_operation，包含完整元数据、Children、Origin 和来源映射 |

元数据中 `annotations_present`／`comments_present` 区分不修改与显式清空。完整操作使用 `generation_operation`，操作枚举使用 `operation`；提交仍经过原生管线的权限、目标、冲突和类型校验。新增声明不能携带替换目标。

registration 和 output 是当前调用有效的服务，context 的服务能力也只在回调期间有效。复制出的只读信息可以保留；旧 scope 或服务句柄不能修改下一批次的声明。UTF-8 字节范围不能切分字符，编译扩展导入前缀禁止修改。

泛型在模板声明上处理一次，由普通实例化代入。虚方法覆盖、用户跨文件实现绑定及默认正文补全沿用现有规则。开放泛型业务包保存生成操作和必要元数据，消费者不执行生产者扩展。

## 配置、资源与执行限制

初始化、注册、工厂和回调共用总指令预算、期限及取消控制。编译 VM 禁止控制台、直接文件和环境操作、任务创建、目录变更、地址／外部身份和时钟读取。额外原生编译绑定由宿主显式提供；未声明确定性时不复用准备结果缓存。

C++ 会话默认将执行取消控制器用于编译扩展；通过 `SetExecutionControl` 替换控制器时，继承的配置同时更新。为编译扩展显式提供独立控制器时，会话保留该独立配置。

`std::compiler::configuration(key)` 返回具有 exists()／value() 的只读值；`read_dependency(path)` 返回 exists()／content()／path()。相对资源路径基于执行中的扩展根，读取记录含缺失状态。context 上的对应方法使用同一依赖账本。配置、源码与包内容、服务契约和全部依赖进入指纹及失效检查。

这些限制面向宿主明确启用的扩展，不是完整的不受信任代码沙箱；原生阻塞回调需要宿主协作取消。

## 扩展包和工具

使用 `CompileScriptCompileExtensionSource` 编译扩展代码，随后通过 `BuildPackage` 生成 Library 包，并将返回的 `CompileExtensionManifest` 复制到 `FPackageBuildOptions::CompileExtension`。清单已包含入口、稳定模块身份、0.1.1 版本、服务指纹、明确的宿主编译绑定契约和扩展依赖。此编译入口不运行初始化、注册或处理器；加载会在初始化前校验清单，裁剪调试信息后入口仍可解析。

清单生成按解析后的文件身份合并配置根和源码依赖，并排除生产者自身。清单字段超限或包数据截断时，包接口返回失败和诊断。工作区符号索引会重新构建包含脚本扩展的结果，包括未打开的源码文件，以响应扩展及资源依赖变化。

业务包只需要生成后的业务代码及泛型记录。工具分析业务源码使用同一核心准备阶段；分析扩展源码可以使用 `GetScriptCompileServiceCatalog()`，它提供类型和签名及不可执行的占位回调。

完整源码示例：[builder.vs](../../../Examples/ScriptCompileExtensions/builder.vs)、[main.vs](../../../Examples/ScriptCompileExtensions/main.vs)。

## 宿主、命令行和缓存

- C++：在 `FCompileOptions`／`FEngineConfig`／`FCodegenOptions` 设置 `ScriptCompileExtensions`；会话也提供 `SetScriptCompileExtensions`。
- C：`vs_interop_session_set_script_compile_options` 复制根、配置、路径限制和执行限额，NULL 恢复默认值。
- C#：`SetScriptCompileExtensions(VeroScriptCompileExtensionsOptions)`；Python：`set_script_compile_extensions(roots, configuration=...)`。会话配置仅在创建线程、空闲期间修改。
- CLI：`--compiler-extension <file>`、`--compiler-config <key=value>`、`--no-compiler-imports`、`--compiler-instructions <n>` 和 `--compiler-timeout <ms>`。`compiler-package source.vs output.vslib` 构建带清单的扩展包。源码前置导入无需额外参数。

源码导入与明确的根使用同一服务。运行时 JIT 后端只执行生成后的业务代码；编译扩展始终使用 VM。工具、C++／C#／Python 源码 AOT 使用同一准备阶段，导出的业务产物不保留处理器 VM。

核心缓存不可变的扩展代码，复核源码、普通依赖及导入解析；每个任务重新创建执行状态、运行注册并创建工厂。准备结果缓存由宿主管理，可在加载任务完成后调用 `MatchesPreparedProgram` 校验处理配置和全部依赖；宿主还需以自己的入口源码和导入图作为缓存键。命中时调用 `CompilePrepared` 复用快照并关闭任务，不运行处理回调；未命中时用该任务的处理配置加载原始源码，并将任务附到待准备的 `FProgram::ScriptCompileExtensionTask`，随后进行普通准备。清理成功后才发布结果。

源码 `VeroScript/Resources/Examples/ScriptCompileExtensions` 可独立使用 `find_package(VeroScript 0.1.1 EXACT CONFIG REQUIRED)` 构建。样例运行接受入口脚本和标准库目录参数；安装后 CTest 使用 SDK 的 `assets/stdlib`，不依赖开发源码路径。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

Language: [en-US](../en-US/script-compile-extensions.md) · [fr-FR](../fr-FR/script-compile-extensions.md) · [ja-JP](../ja-JP/script-compile-extensions.md) · [ko-KR](../ko-KR/script-compile-extensions.md) · [zh-CN](script-compile-extensions.md) · [zh-TW](../zh-TW/script-compile-extensions.md)
