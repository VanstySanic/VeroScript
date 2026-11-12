# 偵錯既有 Session

此橋不會選擇 VM 後端。執行前須依[宿主整合](embedding.md)明確設定具備 `VmDebugCapability` 的後端；連接除錯器不會自動提供直譯器。

執行前透過 `SetDebugBridge` 安裝 `VeroScriptSessionDebug.h` 中的 `ISessionDebugBridge`。`BeginExecution` 可為目前呼叫傳回共用偵錯器，選擇 VM 偵錯，不會接管或重播已執行中的 JIT 呼叫。涵蓋 `Run`、物件建構、方法／函式呼叫、保留 callable 和類別預設值。包含失敗及例外展開，每次 begin 回呼對應一次 `EndExecution`。工作執行緒不是此橋的額外偵錯內容。

全部橋回呼和暫停 VM 的檢查、求值、修改及恢復都屬於 Session 控制執行緒。暫停回呼須在該執行緒處理要求。只有 `RequestPause()` 和取消可跨執行緒直接控制：使用共用 `FExecutionControl::RequestCancel()`，或在保證 Session 存活時呼叫 `Session::RequestCancel()`。借用的程式、繫結、原始碼 overlay 和入口名檢視在回呼傳回時失效；複製所需中繼資料。overlay 是目前組態，不是凍結的原始碼快照；原始碼及繫結修訂由宿主另行公開。

<a id="debug-disconnect-lifetime"></a>

中斷連線按宿主策略釋放暫停連線，不自動取消執行或銷毀 Session。`Detached` 在移除／替換及 Session 銷毀前呼叫。排空回呼時不要等待排給同一控制執行緒的工作。關閉前先釋放暫停，依意圖要求取消，等待執行與通訊關閉後再銷毀 Session。鎖存的取消權杖須重設後再重用。阻塞原生回呼需要自行協作。

[DAP](../../../../../../../VeroScriptTooling/Resources/Docs/User/zh-TW/debugging.md)

<!-- docs-navigation -->

---

[使用者文件](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/session-debugging.md) · [fr-FR](../fr-FR/session-debugging.md) · [ja-JP](../ja-JP/session-debugging.md) · [ko-KR](../ko-KR/session-debugging.md) · [zh-CN](../zh-CN/session-debugging.md) · [zh-TW](session-debugging.md)
