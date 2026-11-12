# Interop ABI 与生命周期

`VeroScriptInterop` 是 C11-compatible process ABI。public header 为 session、value、callable、package、buffer、call context、interception、generic host type 与 external serialization 定义 opaque handle。

## VM 后端选择

`VS_INTEROP_CAP_VM_BACKEND_SELECTION` 用于发现 `vs_interop_session_set_vm_backend` 和 `vs_interop_session_get_vm_backend_info`。标准 Interop 产品显式组装静态字节码 VM；自定义构建可设置 `VEROSCRIPT_INTEROP_DEFAULT_VM=OFF`，再选择与 Core 匹配的外部 VM。后端选择独立于 VM/Mixed/JIT 执行策略，必须在首次执行前于创建者线程、会话空闲时设置；空路径清除选择，失败保留原选择。Core 一旦提交执行（包括 JIT 执行）即冻结选择，`vs_interop_session_clear` 不会解冻。

查询前初始化输出的 `struct_size`，查询失败保留输出。域状态使用 `VS_INTEROP_VM_SELECTED`、`VS_INTEROP_VM_FROZEN`，后端能力使用 `VS_INTEROP_VM_CAP_*`。文本视图借用至后端替换或会话销毁；静态选择的 `library_path` 为空。ApplyConfig 保留选择，脚本编译扩展继承会话 VM。

## version 与 status

ABI version、runtime semantic version、capability bit、execution backend、run mode、package kind、value kind 和 status code 是不同 field。ABI query function 分别报告它们；binding 不得把 ABI value 当作 product version 显示或比较。

当前 `VS_INTEROP_ABI_VERSION` 为 `0x00000101`（0.1.1），编码为 `(major << 16) | (minor << 8) | patch`：主版本占 16 位，次版本与补丁各占 8 位。C# 与 CPython 适配器要求完全相等；能力位以及按 `struct_size` 追加的尾部字段不能免除该版本检查。ABI 变更须独立于各业务包版本进行审查。

每个适用 operation 返回 ABI status。last-error buffer 用于 operation-level failure；structured diagnostic 通过 session diagnostic API 读取，并且仅在 presentation boundary 格式化。

## 所有权

opaque handle 根据 function 是 explicit owner 或 borrowed reference。string/byte view 不拥有资源。ABI 返回的 buffer 必须由 matching buffer function destroy，或在 owner release 前 copy。callback context 与 borrowed enumeration value 仅在 documented call 内 valid。

C header 从 C 与 C++ compile/link，因此 calling convention、struct layout、enum value、nullability 与 symbol name 都是 ABI 的一部分。即使是 additive function，也需要 header、implementation、test、adapter 同步更新。

## 会话 JIT 缓存扩展

`VS_INTEROP_CAP_SESSION_JIT_CACHE` gate session cache limit/statistic export。此 extension 向 `vs_interop_jit_policy` append `reuse_session_code`。为 original 32-byte layout 编译的 caller 仍然 valid，默认得到 enabled reuse。current caller 发送 full structure，并可把该 byte 设为 zero。这在提供 explicit opt-out 的同时保留 ABI 0.1.1。

代码复用适用于满足条件的普通 Run、EvalOnce 和函数调用，只保留编译状态，不保留 VM 全局变量或脚本对象。后端兼容且绑定修订号稳定时，持久会话也可复用编译代码。变更拦截器或变更回调会使保留代码失效。实际采用 VM 时绕过此缓存。受控执行可以保留匹配的后端缓存及治理身份，并共享累计编译配额；编译尝试/缓存项策略预算为零表示对应计数不限额，原生代码及完整载荷上限仍然有效。`vs_interop_session_set_jit_cache_limits` 使当前上下文失效；默认上限为 8 MiB/32 MiB，零表示禁止保留对应资源。受控 Mixed 默认走 VM；测量目标负载后可启用 `enable_controlled_mixed`，显式 JIT 选择独立于此开关。检查后端的 `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION` 和实际执行统计 `controlled_native_executions`；不支持或优先 VM 的入口继续保留治理检查。

`vs_interop_session_get_jit_cache_stats` 报告 current context 的 cumulative compile attempt、settled retained byte 与 resource-limit latch。它不同于 `vs_interop_session_get_last_jit_stats` 返回的 last-run delta，也不是 process peak-memory limit。policy、limit、backend、module 或相关 host observability 变更可以 invalidate context 并 reset statistic。

## 新 context 与 channel 契约

channel operation 使用 value handle contract，不会把 value 变为 borrowed callback object。blocking、timed、non-blocking operation 有不同 symbol/result semantics；`close` 是 waiter 的 synchronization point。host 不得从无关的 `VeroScriptStatus` value 推断 timeout 或 closed result。

generic host-type、call、interception、external serialization context pointer 在 callback duration 内 borrowed。accessor、mutator、error setter、continuation/invocation function 仅在 callback active 时 valid。return 后不得 retain context pointer、argument/field/byte view。callback exception 或 host failure 必须在 return 前转换为 context error channel。

callable handle 提供 explicit retain、validity、invoke、destroy operation。owner-thread-only invocation 与 weak-`this` capture 是独立 option，不会使 session generally thread-safe。需要跨 callback 保存的 external serializer byte/reference visitor value 必须 copy。这些 rule 对 C、C++、managed adapter 均适用。


<a id="localization-abi"></a>

## 本地化 ABI

`vs_interop_locale_translate` 遵循正常初始化与进程语言选择，分配模块所有的缓冲区。未知字节（包括 NUL）保留，无效输入清空输出；每条路径都须释放缓冲区。同步 C 头与 `.def` 导出，验证原生校验和六语言替换。内存分配失败可能保留英文回退。

## 出版验证

独立和完整发行的 Interop 打包都会使用暂存 SDK 编译并运行 C11 头文件测试，
链接每个公开函数。缺失导出或导入库条目会拒绝打包。外部示例通过
`VEROSCRIPT_SDK_ABI_TEST_SOURCE` 启用该检查。Linux 共享库测试还会在未加载模块的
反射错误路径注入分配失败，要求返回 `OUT_OF_MEMORY` 且异常不越过 C ABI；
普通的未加载模块情况返回 `ENGINE_ERROR`。

边界测试还覆盖版本查询首次分配失败（空 view、重试成功）、serializer 替换失败
（保留原字节）、无分配 multiset 计数及 memory guard 保留原始失败 status。
UTF-16 view 分配失败返回 `OUT_OF_MEMORY`，不修改输出参数；重试保留全部码元，
后续读取复用缓存视图。

Console 回调追加到 session-options 前缀，不改变 C ABI 版本；只读取 `struct_size`
覆盖的完整尾部字段，旧 options 仍有效。query/control 描述终端操作，可选 write
回调接收不追加换行的原始 UTF-8 字节，遵循现有回调生命周期和 worker 线程规则。
扩展测试在 session 最终析构时，从脚本析构函数调用 native、interception 和 mutation
callback。host callback data 必须存活至析构完成。

宏回归通过公开 C ABI 构建、解析、导入并执行包含再导出对象式、函数式和变参宏的库包，
同时覆盖保留及剥离调试信息。测试确认构建时条件导出、调用方展开、私有宏隔离、导入
错误的物理位置，以及当前 `VSBC` 读取器拒绝 `VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` 载荷。

安装后的原生注解 SDK 测试检查冻结注册表校验、独立 service retain/release、ApplyConfig
保留服务、创建线程限制，以及处理器和释放回调中的配置重入。它生成并调用方法，
验证四种诊断级别和主要错误选择，并确认清除服务会停用生成。原生创建函数需要配套
Core C++ SDK；新增五个 C 导出保持 ABI 0.1.1 和原有诊断布局兼容。

测试还覆盖 ApplyConfig 期间服务函数对象的复制与销毁：拒绝配置重入，
复制分配失败返回 `OUT_OF_MEMORY` 并保留已安装服务，成功重试清除旧错误。

回归还验证复制、销毁回调中的普通函数调用、执行与源码替换均被拒绝，
并保留当前程序。配置描述符先于服务函数对象复制完成验证，避免生命周期回调
覆盖原始验证错误。原生回调上下文调用仍支持合法的嵌套脚本执行。

新增回归用旧的成功、退出标志初始化结果，验证重入被拒绝后写回新的失败结果
及会话诊断，也覆盖 NULL 结果输出。失效 callable 返回失败且保留返回值。
同步销毁会话时，包装句柄存活至最终回调结束，会话先标记为关闭；回调中的
会话 API 访问与重复销毁被安全拒绝。destroy 返回后句柄无效。活动调用中的
延迟析构要求回调数据存活至该调用返回。

`vs_interop_session_retain_callback_user_data` 仅在成功时接管宿主协议回调数据。
最后一个注册表／值持有引用释放后，释放回调只调用一次，可能晚于会话销毁并在其他线程执行。
后续普通注册与泛型实例化复用相同数据时共享该持有者，重复接管被拒绝且不再次转移所有权。
回归覆盖逃逸值复制、其他线程的最终释放，以及逐次分配失败后的成员注册重试。

## 通用与脚本编译处理

`VS_INTEROP_COMPILE_CAP_GENERAL_PROCESSING` 标识 `VeroScriptInteropCompileProcessing.h` 的原生服务。使用冻结的 `FCompileProcessorRegistry` 和 `CreateCompileProcessingService` 创建；retain/release 提供独立 opaque handle。`vs_interop_session_set_compile_processing_service` 安装配置，NULL 清除，ApplyConfig 保留服务。使用匹配的 Core/Interop 原生 SDK，并让处理器代码与数据存活到最终释放。设置要求创建线程且会话空闲；处理器及服务复制／释放回调中的重入被拒绝。

`VS_INTEROP_COMPILE_CAP_SCRIPT_EXTENSIONS` 标识 `vs_interop_session_set_script_compile_options`。提供完整当前 `struct_size`；根、可选注册入口、配置及允许目录均被复制。路径／入口名拒绝嵌入 NUL；重复配置键、无效指针／数量、reserved byte 与非法 bool 不替换已接受配置。上限为 64 个根以及 4096 个配置项／允许目录。空入口名选择 `register_extension`。

NULL 恢复核心默认值：无配置根、允许源码导入、总计 1,000,000 指令和 2,000 ms。显式零执行限额表示无限制。禁用源码导入仍允许配置根。沿用覆盖层、导入与标准库路径；导入标准库源码的扩展需要包含 `compiler.vs` 的匹配目录。编译扩展在隔离 VM 执行，继承会话取消控制；ApplyConfig 保留配置。业务绑定不会隐式暴露。生成业务包不执行生产者扩展；运行时 JIT 执行生成的业务代码。

C 回归验证跨阶段状态、输入复制、生成／诊断、无效输入回滚、预算及取消失败与恢复、源码导入策略和包执行。安装后的原生消费者验证服务生命周期、配置保留及处理器／复制／释放回调中的重入。

## 已解析模块环境

`VS_INTEROP_CAP_MODULE_ENVIRONMENT` 表示支持模块环境桥接。使用匹配的原生 Core/Interop SDK 创建不可变 `FModuleEnvironment`，再通过 `VeroScriptInteropModuleEnvironment.h` 中的 `CreateModuleEnvironment` 包装。C 侧只传递不透明句柄：`vs_interop_module_environment_retain` 创建另一个独立拥有的句柄，每个句柄释放一次。配置成功后，会话独立持有快照、资源、原生代码所有者及依赖契约。

`vs_interop_session_apply_config_with_environment(session, config, environment)` 应用完整配置并选择快照，NULL 表示移除。普通 `vs_interop_session_apply_config` 保留当前环境。两者均要求空闲的创建线程；更换环境前必须 Clear 已加载程序。环境与本地绑定冲突时，在修改配置或取消控制前返回 `ENGINE_ERROR`。环境拥有绑定目录时，本地绑定修改返回 `ENGINE_ERROR`，应在原生侧预先组合不可变目录；未提供目录的环境仍允许普通 C 注册。

加载、执行、包及外部 JIT 使用 Core 的内存/文件模块映射、编译资源、本地化服务和精确外部依赖校验。本桥接消费已解析环境，不负责安装包或发现原生扩展。发布后的回调保持不可变，并使用匹配的原生 SDK 二进制。配置或会话最终销毁时可能释放所有者；析构不得抛异常，配置和执行重入会被拒绝。Core 应用配置期间发生分配或内部失败后，仍需重新应用完整配置再依赖其设置。

后端解析被拒绝时，在提交配置或取消状态之前返回 `ENGINE_ERROR`，可使用有效环境重试。热重载设置被拒绝时同样返回 `ENGINE_ERROR`。有界回滚仍处于 `RESET_REQUIRED` 时，须销毁并重建会话后再执行。

<a id="interop-regression-targets"></a>

模块 CMake 门禁包括 `VeroScriptInteropTests`、`VeroScriptInteropExtendedTests`、`VeroScriptInteropTypeTests`、`VeroScriptInteropMemoryTests` 和 C11 `VeroScriptInteropCHeaderTest`；启用时由 `VeroScriptInteropNativeSdkTests` 覆盖已安装的原生消费端。实际导出须与公开 ABI 清单一致，另行获取的 C11 示例须针对解压后的 SDK 验证。以匹配的 Core／VM 检查编译服务寿命、错误线程拒绝、回调与保留值，记录真实结果和跳过项；测试源码存在不等于发行验收通过。

<!-- docs-navigation -->

---

[开发文档](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/zh-CN/index.md)

语言: [en-US](../en-US/abi-and-lifetime.md) · [fr-FR](../fr-FR/abi-and-lifetime.md) · [ja-JP](../ja-JP/abi-and-lifetime.md) · [ko-KR](../ko-KR/abi-and-lifetime.md) · [zh-CN](abi-and-lifetime.md) · [zh-TW](../zh-TW/abi-and-lifetime.md)
