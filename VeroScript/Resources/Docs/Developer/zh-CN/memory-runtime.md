# 受管理内存实现契约

Core 负责 `Public/VeroScriptMemory.h` 与 `Private/Memory/` 中的内存服务。编译降级位于 `Private/Compiler/VeroScriptCompilerMemory.cpp`，执行分派位于 `Private/ExecutionSupport/VeroScriptVmMemory.cpp`。`stdlib/memory.vs` 是模块入口，不是第二份实现。适配器必须复用这些服务，不得自行建立身份注册表或削弱所有权检查。

## 不变式与验证

访问载荷前必须验证可信句柄来源。身份注册只使用弱所有权信息，不能延长寿命，也不能把复用地址误认作原对象。区域共享所有者，视图描述符包含偏移、长度、代际和只读种类。`FMemoryContext` 拥有访问域；worker 共用预算核算，但所属线程各自独立。允许跨线程调用的 callable 也不能转移内存访问域。

所有操作，包括零长度范围，都必须检查访问域、代际、边界与写权限。拥有存储的增长在提交前计入新旧两份分配。批量操作每 64 KiB 检查执行治理，按 ceil(bytes/64) 计量；标量写入在修改字节前检查。租借阻止结构变化和冲突访问，释放回调在核心锁外仅调用一次且不能抛出异常。

## 宿主接入与限额

C++ 使用 `VeroScriptMemory.h` 和持续的 `FMemoryContext`，或 Session 的 `GetMemoryContext()`。C ABI 使用 `vs_interop_memory_*`，每次指定一个 Session 或活跃调用上下文。既有 `vs_interop_buffer` 仍是独立的序列化结果缓冲区。

外部区域仅注册成功后接管释放责任，所有别名共享失效状态。读写租借对冲突访问和结构变化立即返回 Busy。原生工作完成后仅释放一次不透明令牌；任意线程、Session 销毁后仍可释放。最终外部释放回调在核心锁外执行且不能抛异常。未租借取得的地址仅供即时观察。

MaxBufferBytes 限制每个容量或外部长度；MaxRetainedBytes 累计全部拥有容量及借用字节，包括增长过程中新旧分配的峰值。0 表示无限。OwnedBytes、BorrowedBytes、PeakOwnedBytes 不含 VM 和对象元数据。Run、Clear、重载不重置保留占用。Worker 共用预算但拥有独立访问域；区域不能通过任务捕获、参数、返回或通道传递。

C#：`session.Memory`、`SetMemoryLimits`、`using var lease = memory.AcquireLease(bytes, true)`。Python：`session.memory`、`set_memory_limits`、`with memory.acquire_lease(bytes, write=True) as lease`；关闭租借前释放其 memoryview。两者保留外部缓冲区直到最后区域或租借释放。原生回调通过 `vs_interop_memory_call_argument_address`、C# `context.GetArgumentAddress` 或 Python `current_memory().argument_address` 查询原始 ref/const-ref 目标；按值封送副本返回 Unsupported。

## 后端与调试

支持 VM 和 Mixed。AsmJit 在入口前拒绝地址敏感函数和核心内存成员函数，并沿现有路径回退 VM；实际内建字节操作复用核心服务。执行治理保持既有 VM 策略。独立 C++/C#/Python AOT 拒绝实际内存用途，允许未使用的 memory/core 导入。字节码和包保存操作，不保存原生指针、租借令牌或身份注册表。默认对象快照拒绝嵌套区域句柄；持久化应使用显式字节快照或显式序列化器。

暂停调试展示核心内存信息。DAP variablesReference 仍是协议标识，不启用任意 readMemory/writeMemory。Android JNI 与 iOS Runner 复用核心，默认关闭原生地址。直接使用 C#/Python Session 时，由宿主显式设置配额与能力。Wasm 以 bigint 保存 ID、长度和限额，区域属于各实例，租借访问在堆增长后重新创建线性内存视图。Wasm 宿主区域预算与脚本 Session 预算独立，默认单区域 16 MiB、保留总量 32 MiB。

六个 Core 语言目录均维护 `memory.json`，保持稳定 ID 和相同键集。验证拥有/外部区域、身份、视图失效、限额、Busy 租借、回调所有权、跨线程释放、Session 销毁、重载、快照、worker 限制和 AOT 拒绝。执行 Core/Cpp/Interop 内存测试、绑定内存契约、本地化部署和文档检查。Web 还须覆盖堆增长与交互输入排斥；移动端真机验收单独进行。


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory-runtime.md) · [fr-FR](../fr-FR/memory-runtime.md) · [ja-JP](../ja-JP/memory-runtime.md) · [ko-KR](../ko-KR/memory-runtime.md) · [zh-CN](memory-runtime.md) · [zh-TW](../zh-TW/memory-runtime.md)
