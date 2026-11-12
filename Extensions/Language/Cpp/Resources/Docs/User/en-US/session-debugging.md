# Debugging an existing Session

This bridge does not select a VM backend. Before executing, explicitly configure a backend with `VmDebugCapability` as described in [Host integration](embedding.md); attaching a debugger does not supply an interpreter.

Install `ISessionDebugBridge` from `VeroScriptSessionDebug.h` with `SetDebugBridge` before execution. `BeginExecution` may return a shared debugger for the current call; this selects VM debugging without replaying an already executing JIT call. `Run`, object construction, method/function calls, retained callables and class defaults are covered. `EndExecution` runs once for each begin callback, including failure and exception unwinding. Workers are not additional contexts of this bridge.

All bridge callbacks and stopped-VM inspection, evaluation, mutation and resume belong to the Session controlling thread. A stop callback must process requests there while paused. Only `RequestPause()` and cancellation are cross-thread controls: use the shared `FExecutionControl::RequestCancel()`, or `Session::RequestCancel()` with a guaranteed Session lifetime. Borrowed program, binding, source-overlay and entry-name views expire when the callback returns. Copy needed metadata; source overlays are current configuration, not a frozen source snapshot. Publish source/binding revisions separately.

<a id="debug-disconnect-lifetime"></a>

Disconnect releases the paused connection according to host policy; it does not by itself cancel execution or destroy the Session. `Detached` runs on removal/replacement and before Session destruction. Drain callbacks without waiting for work queued to the same controlling thread. Before teardown, release paused execution, request cancellation if intended, wait for execution and transport shutdown, then destroy the Session. Reset a latched cancellation token before reuse. Native blocking callbacks require their own cooperation.

[DAP](../../../../../../../VeroScriptTooling/Resources/Docs/User/en-US/debugging.md)

<!-- docs-navigation -->

---

[User documentation](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](session-debugging.md) · [fr-FR](../fr-FR/session-debugging.md) · [ja-JP](../ja-JP/session-debugging.md) · [ko-KR](../ko-KR/session-debugging.md) · [zh-CN](../zh-CN/session-debugging.md) · [zh-TW](../zh-TW/session-debugging.md)
