# 调试已有 Session

此桥不会选择 VM 后端。执行前须按[宿主集成](embedding.md)显式配置具备 `VmDebugCapability` 的后端；连接调试器不会自动提供解释器。

执行前通过 `SetDebugBridge` 安装 `VeroScriptSessionDebug.h` 中的 `ISessionDebugBridge`。`BeginExecution` 可为当前调用返回共享调试器，选择 VM 调试，不会接管或重放已经执行中的 JIT 调用。覆盖 `Run`、对象构造、方法／函数调用、保留 callable 和类默认值。包括失败及异常展开在内，每次 begin 回调对应一次 `EndExecution`。工作线程不是此桥的额外调试上下文。

全部桥回调和暂停 VM 的检查、求值、修改及恢复都属于 Session 控制线程。暂停回调须在该线程处理请求。只有 `RequestPause()` 和取消可跨线程直接控制：使用共享 `FExecutionControl::RequestCancel()`，或在保证 Session 存活时调用 `Session::RequestCancel()`。借用的程序、绑定、源码 overlay 和入口名视图在回调返回时失效；复制所需元数据。overlay 是当前配置，不是冻结的源码快照；源码及绑定修订由宿主另行公开。

<a id="debug-disconnect-lifetime"></a>

断开按宿主策略释放暂停连接，不自动取消执行或销毁 Session。`Detached` 在移除／替换及 Session 销毁前调用。排空回调时不要等待排给同一控制线程的工作。关闭前先释放暂停，根据意图请求取消，等待执行与通信关闭后再销毁 Session。锁存的取消令牌须重置后再重用。阻塞原生回调需要自行协作。

[DAP](../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-CN/debugging.md)

<!-- docs-navigation -->

---

[用户文档](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/session-debugging.md) · [fr-FR](../fr-FR/session-debugging.md) · [ja-JP](../ja-JP/session-debugging.md) · [ko-KR](../ko-KR/session-debugging.md) · [zh-CN](session-debugging.md) · [zh-TW](../zh-TW/session-debugging.md)
