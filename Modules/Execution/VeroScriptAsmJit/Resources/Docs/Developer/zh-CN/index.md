# 后端维护

参阅[核心 JIT 服务与并发](../../../../../../../VeroScript/Resources/Docs/Developer/zh-CN/jit-and-concurrency.md#native-abi-version)。

Core 负责执行契约、公开 JIT 契约、运行时服务、验证、调度和同代计量。具体 IR、降低、原生发码、可移植 Linear 执行和可执行存储归 `Modules/Execution/VeroScriptAsmJit`，不归 Core。`Private/JitSupport` 实现通用缓存和模块加载器。二进制后端兼容性由公开头指纹及编译器／STL／运行库／平台握手决定，不能仅凭产品版本判断。 字节码解释器归独立的 `VeroScriptBytecodeVm` 模块。

## 构建与 ABI

`VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` 构建不含具体后端的 Core。设为 `ON` 后，`VEROSCRIPT_ENABLE_ASMJIT=ON` 启用原生发码，`OFF` 保留可移植 Linear。源码宿主链接 `VeroScriptAsmJit` 并注入 `CreateAsmJitBackendFactory()`；`VeroScriptAsmJitBackend` 生成不含 Core 副本的共享模块。导入的发码目标须提供所需的兼容 PIC，其拥有者部署共享依赖。Windows 动态加载要求 `/MD` 或 `/MDd`。

```cmake
target_link_libraries(MyHost PRIVATE VeroScriptAsmJit)
```

## 运行时契约

区分编译结果 `Ready / Unsupported / Deferred / Failed` 与调用结果 `NotEntered / Returned / ControlTransfer / Failed`。只有 `NotEntered` 允许 VM 重放。发布前由 Core 验证所有权、服务及预算。可执行产物保留代码及模块所有权；调用上下文、帧和值仅在调用期间借用。嵌套调用后重新获取视图；每次快速路径判断重新读取实时错误／控制／回调标志。

Session 代码复用资格、执行治理、保留限制与记账统一维护在 [C++ Session](../../../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/zh-CN/session.md#session-jit-cache)。

## 发布验证

`veroscript_jit_query` POD 握手在创建 C++ 工厂前检查 API、公开头指纹、编译器／STL、C++ 模式、运行库与平台。工厂、工作线程和可执行产物存活时保留库。验证纯 Core、注入契约、不兼容库、显式文件／目录选择、不重放的回退、资源回收和 Session 缓存失效。用 `package-veroscript-jit.py` 打包；独立版本不能免除 ABI 匹配。iOS 真机和模拟器均禁止动态加载及原生发码；静态可移植后端须另行验证。

[Core](../../../../../../../VeroScript/Resources/Docs/Developer/zh-CN/jit-and-concurrency.md)

## 保留字节配额

`MaxNativeBytes` 或 `MaxRetainedPayloadBytes` 为零不允许保留对应字节；与表示无限制的零策略预算不同。

## 通用编译处理与源码元数据

处理器由 Core 在编译期执行；生成类型、构造函数、辅助方法、虚覆盖和泛型实例以普通字节码进入后端，不增加后端专用的生成阶段。开放泛型包由 Core 重放准备结果，后端只编译最终函数。

元数据描述对象保持 Core 的不可变查询快照。后端通过 `IJitCallServices::InvokeBuiltin`、值服务与正常方法调用处理查询、复制及释放；不会缓存原始注解节点指针，也不会另存一份说明目录。Linear 分析识别元数据查询的 `int`、`int64`、`uint64`、`double`、`bool` 和 `string` 返回类型；对象描述结果保持拥有所有权的通用值。

`VeroScriptAsmJitProcessingTests` 对照 VM、Mixed 和 JIT，覆盖生成类型、链式构建器、虚辅助方法、跨文件实现、开放泛型包、VSBC 往返、元数据值种类、说明、注释保留、越界错误及跨代描述对象。非 iOS 构建同时测试静态工厂与独立动态库。启用原生发码时，测试要求存在机器码产物及实际后端分派。

```shell
cmake --build <build> --config Release --target VeroScriptAsmJitProcessingTests
ctest --test-dir <build> -C Release -R VeroScriptAsmJitProcessingTests --output-on-failure
```

公开头指纹已经涵盖新处理接口与元数据布局。产品和后端版本仍为 0.1.1；现有二进制后端须使用当前 Core 的公开头及构建契约重新编译。

官方后端归档发布到 `Release/Modules/Execution/Jit/<target>/<version>/archives/`。Modules 不再新建独立安装器；功能载荷保留运行依赖及法律声明，不含文档、示例、测试、基准和符号。源码文档正本仍放在 Resources/Docs，通过 Resources 集成文档出版。

## 内置依赖维护

AsmJit 由本模块负责，源码保留在 `ThirdParty/asmjit`，保留[原始许可证](../../../../ThirdParty/asmjit/LICENSE.md)，在 [VERSION.txt](../../../../ThirdParty/asmjit/VERSION.txt) 记录上游修订。不要将第三方源码放入 Resources/Docs 或复制到 Core。`VEROSCRIPT_ENABLE_ASMJIT=OFF` 为已构建的后端选择可移植实现；`VEROSCRIPT_BUILD_ASMJIT_BACKEND=OFF` 则不构建本后端模块。

更新依赖时替换内置修订，保留原始声明及代码生成数据库的许可证，同步 VERSION.txt 和[模块声明](../../../Licenses/NOTICES.md)中逐字保留的许可证正文，重新配置并执行 JIT 与打包测试。保持编译器/STL/运行库兼容，验证原生发码与可移植两种配置。后端安装组件将 LICENSE.md 与 VERSION.txt 导出到 `licenses/asmjit/`，须与所属源码逐字节比较。公共声明索引位于 Resources/Licenses/README.md，依赖声明由使用该依赖的模块维护。

## 受控标量块与部分循环覆盖

受控短整数循环可由原生 CFG 和 Core 的原始标量块执行；每条字节码仍检查并计量限额、取消及超时，不批量预支计数。EnableScalarBlocks 在入口前拒绝自定义宿主执行检查和变更观察器。标量表示不适用时，在当前指令恢复普通服务，不重复检查计数，不重放函数。复杂循环仍按成本选择 VM。

类型一致的字符串 += 参与普通 CFG 校验；未优化的文本循环不会屏蔽同一函数中已具备原生实现的非文本循环。受控 Mixed 的显式启用规则保持不变。

短小、无分支的整数接收者表达式可优先使用 Core 的有界调用计划，以减少原生调用桥接成本。该选择发生在正常后端编译／校验之后；失败、变更观察器及执行治理仍走既有 VM 路径。更大正文、调用及循环继续独立选择后端计划。

`bPreferVmForNestedExecution` 仅适用于无执行治理的解释器调用者；宿主直接调用与原生调用者保留原生入口。

参阅[核心 JIT 服务与并发 — 验证](../../../../../../../VeroScript/Resources/Docs/Developer/zh-CN/jit-and-concurrency.md#construction-dispatch)。

<!-- docs-navigation -->

---

[开发文档](../../../../../../../Resources/Docs/Developer/zh-CN/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](index.md) · [zh-TW](../zh-TW/index.md)
