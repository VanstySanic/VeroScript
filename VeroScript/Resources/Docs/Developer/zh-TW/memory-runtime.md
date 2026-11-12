# 受管理記憶體實作契約

Core 負責 `Public/VeroScriptMemory.h` 與 `Private/Memory/` 中的記憶體服務。編譯轉換位於 `Private/Compiler/VeroScriptCompilerMemory.cpp`，執行分派位於 `Private/ExecutionSupport/VeroScriptVmMemory.cpp`。`stdlib/memory.vs` 是模組入口，不是第二份實作。配接器必須共用這些服務，不得另建身分登錄或削弱擁有權檢查。

## 不變條件與驗證

存取內容前必須驗證可信控制代碼來源。身分登錄只使用弱擁有權資訊，不能延長存留期，也不能把重用位址誤認為原物件。區域共用擁有者，檢視描述元包含位移、長度、世代與唯讀種類。`FMemoryContext` 擁有存取域；worker 共用預算核算，但所屬執行緒各自獨立。允許跨執行緒呼叫的 callable 也不能移轉記憶體存取域。

所有操作，包括零長度範圍，都必須檢查存取域、世代、邊界與寫入權限。擁有儲存的擴充在提交前計入新舊兩份配置。批次操作每 64 KiB 檢查執行管制，按 ceil(bytes/64) 計量；純量寫入在修改位元組前檢查。租借阻止結構變化及衝突存取，釋放回呼在核心鎖外只呼叫一次且不能擲回例外。

## 宿主與限額

C++ 使用 `VeroScriptMemory.h` 及持續的 `FMemoryContext`，或 Session 的 `GetMemoryContext()`。C ABI 使用 `vs_interop_memory_*`，每次指定一個 Session 或活躍呼叫內容。`vs_interop_buffer` 仍是獨立的序列化結果緩衝區。

外部區域僅註冊成功後接管釋放責任，別名共享失效。租借對衝突存取及結構改變立即回傳 Busy。原生工作完成後只釋放一次不透明權杖；任何執行緒與 Session 銷毀後均可釋放。最終外部回呼在核心鎖外執行且不可擲回例外。未租借位址僅供即時觀察。

MaxBufferBytes 限制單一容量或外部長度；MaxRetainedBytes 累計擁有容量及借用資料，包含成長過程的新舊配置峰值。0 表示不限。OwnedBytes、BorrowedBytes、PeakOwnedBytes 不含 VM 或物件中繼資料。Run、Clear、重載不重置保留占用。Worker 共用預算但有獨立存取域；區域不能透過任務擷取、參數、回傳或通道傳遞。

C# 使用 `session.Memory`、`SetMemoryLimits` 與 `using var lease = memory.AcquireLease(bytes, true)`。Python 使用 `session.memory`、`set_memory_limits` 與 `with memory.acquire_lease(bytes, write=True) as lease`；關閉租借前釋放 memoryview。外部資料保留到最後區域或租借釋放。回呼透過 `vs_interop_memory_call_argument_address`、C# `context.GetArgumentAddress` 或 Python `current_memory().argument_address` 查詢原始 ref/const-ref 目標；傳值封送副本回傳 Unsupported。

## 後端及偵錯

支援 VM 和 Mixed。AsmJit 在入口前拒絕位址敏感及核心記憶體成員函式，依既有路徑退回 VM；內建資料操作使用核心服務。治理維持既有 VM 策略。獨立 C++/C#/Python AOT 拒絕實際用途，但接受未使用的 memory/core 匯入。位元碼與套件不保存原生指標、租借權杖或身分登錄表。預設物件快照拒絕巢狀區域；請使用明確資料快照或序列化器。

暫停偵錯顯示核心資訊。DAP variablesReference 仍是協定標識，不啟用任意 readMemory/writeMemory。Android JNI 與 iOS Runner 沿用核心，預設關閉原生位址。直接使用 C#/Python Session 時，由宿主明確設定配額與能力。Wasm 以 bigint 保留 ID、長度及限額，區域屬於各實例，堆積成長後重建線性記憶體視圖。Wasm 宿主與指令碼 Session 預算獨立，預設單區域 16 MiB、保留總量 32 MiB。

六個 Core 語言目錄均維護 `memory.json`，保持穩定 ID 與相同鍵集。驗證擁有/外部區域、身分、檢視失效、限額、Busy 租借、回呼擁有權、跨執行緒釋放、Session 銷毀、重新載入、快照、worker 限制與 AOT 拒絕。執行 Core/Cpp/Interop 記憶體測試、繫結記憶體契約、本地化部署及文件檢查。Web 還須涵蓋堆積擴充與互動輸入排斥；行動端實機驗收另外進行。


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory-runtime.md) · [fr-FR](../fr-FR/memory-runtime.md) · [ja-JP](../ja-JP/memory-runtime.md) · [ko-KR](../ko-KR/memory-runtime.md) · [zh-CN](../zh-CN/memory-runtime.md) · [zh-TW](memory-runtime.md)
