# 並發模組

模組檔案：`concurrent.vs`

匯入方式：

```VeroScript
import "concurrent.vs";
```

## 通道

`channel<T>` 是用於任務之間通訊的共用容器。標準庫為所有可傳輸的通道元素類型提供泛型操作：

| 函式 | 說明 |
| --- | --- |
| `std::concurrent::channel_send<T>(channel<T> ch, T value)` | 傳送一個值給頻道。 |
| `std::concurrent::channel_recv<T>(channel<T> ch)` | 從頻道接收一個值。 |
| `std::concurrent::channel_close<T>(channel<T> ch)` | 關閉頻道。 |
| `std::concurrent::make_channel<T>(int64 capacity = 0)` | 建立無界或有界頻道。 |
| `ch.try_send(value)` / `ch.send_for(value, timeout_ms)` | 以非阻塞或限時方式傳送；無法傳送時傳回 `false`。 |
| `ch.try_recv()` / `ch.recv_for(timeout_ms)` | 傳回 `optional<T>`；空值表示頻道為空、逾時或已關閉。 |
| `ch.pending()` / `ch.capacity()` / `ch.is_closed()` | 佇列深度與容量回傳非負 `int64`（容量 `0` 表示無界）；關閉狀態回傳 `bool`。 |

較短的 `send`、`recv` 和 `close` 同時支援命名空間呼叫與擴充呼叫：`ch.send(value)`、`ch.recv()`、`ch.close()`。 `T` 可以是通道支援的可傳輸標量（不同寬度的有符號/無符號整數、`float`、`double`、`bool`、`string` 或 `wstring`），也可以是另一個 `channel<U>`。巢狀通道可以在隔離 worker 之間安全傳遞同步能力，而不共享普通可變物件。從開啟的通道接收值可能會等待；關閉通道後，不應繼續執行依賴有效發送或接收的操作。

通道值具有共享控制代碼語意。

## 隔離任務

`std::concurrent::spawn` 啟動一個隔離任務，並傳回任務 ID。任務入口必須是與通道類型相符的函式：

```VeroScript
import "concurrent.vs";

void worker(channel<int> ch)
{
	int value = ch.recv();
	ch.send(value + 1);
}

int main()
{
	channel<int> ch = channel<int>();
	int task_id = std::concurrent::spawn("worker", ch);
	ch.send(41);
	std::concurrent::join_task(task_id);
	int result = ch.recv();
	print(result);
	return 0;
}
```

`std::concurrent::join_task` 等待任務結束。任務之間不能直接共享普通腳本局部變數，應透過通道交換支援的值。

`std::concurrent::spawn<T>` 接受任意支援的 `channel<T>`，但指定的任務入口必須具有完全符合的簽章：

```text
int std::concurrent::spawn<T>(string name, channel<T> ch);
```

## 類型化任務結果

`std::concurrent::spawn_task<T>` 傳回 `task<T>`，適合任務透過結果通道傳回一個值：

```VeroScript
import "concurrent.vs";

void compute(channel<int> output)
{
	output.send(42);
}

int main()
{
	std::task<int> result = std::concurrent::spawn_task<int>("compute");
	int value = result.await();
	print(value);
	return 0;
}
```

字串入口必須具有精確簽名 `void compute(channel<T> output)`，返回前傳送唯一結果。worker 完成後結果缺失或多餘均報錯；尚未完成而阻塞的 worker 仍需協作取消。

`task<T>` 為所有支援的可傳輸通道類型提供下列成員：

| 成員 | 說明 |
| --- | --- |
| `valid()` | 判斷任務是否仍處於作用中狀態。 |
| `done()` | 不等待任務，直接檢查任務是否完成。 |
| `await()` | 等待任務、接收一個結果，並將任務標記為非活動。 |
| `await_for(timeout_ms)` | 只有完成等待逾時才傳回空 optional，任務保持作用中；worker 完成後必須有唯一結果，否則報錯。 |
| `cancel()` | 協作式取消任務並關閉結果通道；VM 會保留 worker 直到其退出。 |

協作式取消不會強制終止或分離執行緒。worker 應觀察通道關閉並退出；對於忽略取消的 worker，VM/Session 銷毀會繼續等待，以確保宿主 callback 的生命週期安全。

## callable 任務入口

匯入 `concurrent.vs` 後，可以將已解析的腳本函式、靜態方法或函式值（含 Lambda）直接傳入 `std::concurrent::spawn(callable, 參數...)` 或 `std::concurrent::spawn_task<T>(callable, 參數...)`。參數可有不同型別，依由左至右的順序求值一次。`std::concurrent::spawn` 要求回傳 `void`；`std::concurrent::spawn_task<T>` 要求回傳與 `T` 完全一致的可傳輸值。原有字串入口仍可使用。

```VeroScript
import "concurrent.vs";

int twice(int value)
{
	return value * 2;
}

int main()
{
	std::task<int> work = std::concurrent::spawn_task<int>(twice, 21);
	int factor = 3;
	std::task<int> captured = std::concurrent::spawn_task<int>(
		[factor](int value) -> int => value * factor, 7);
	print(work.await());
	print(captured.await());
	return 0;
}
```

參數及按值擷取僅能包含可傳輸的純量、字串或通道控制代碼；參照擷取、`this` 擷取、實例方法、一般可變物件、陣列、`ref` 形參及參照回傳不得跨越任務邊界。worker 使用獨立 VM 和輸入快照；`await()` 仍會阻塞目前執行緒。取消屬於協作式請求，worker 可查詢 `std::concurrent::cancel_requested()`。C++、Python、C# AOT 目前會在編譯階段明確拒絕 callable 入口。完整用法見 [並行範例](../../../../Scripts/examples/concurrent_demo.vs)。

## 同步能力

`task_group<T>` 收集同類型任務，並提供 `add`、`await_all` 和 `cancel_all`；對應的自由函式是 `std::concurrent::when_all` 和 `std::concurrent::cancel_all`。

`mutex` 和 `counting_semaphore` 是同步通道控制代碼的明確別名，可以透過巢狀通道傳遞。使用 `std::concurrent::make_mutex`、`std::concurrent::mutex_lock`、`std::concurrent::mutex_try_lock`、`std::concurrent::mutex_lock_for`、`std::concurrent::mutex_unlock`，以及 `std::concurrent::make_semaphore`、`std::concurrent::semaphore_acquire`、`std::concurrent::semaphore_try_acquire`、`std::concurrent::semaphore_acquire_for`、`std::concurrent::semaphore_release`。重複解鎖或超過信號量最大值會產生執行階段錯誤。

`std::concurrent::make_synchronized<T>(initial)` 建立容量為 1 的通道單元；`std::concurrent::synchronized_load`、`std::concurrent::synchronized_store`、`std::concurrent::synchronized_exchange` 和 `std::concurrent::synchronized_fetch_add` 將存取序列化，但不承諾硬體 `atomic<T>` 的無鎖保證。

## 任務取消請求

`std::concurrent::cancel_task(int id)` 對有效任務 ID 發出協作式取消請求，不消耗 ID；之後仍可用 `std::concurrent::join_task(id)` 等待。無效 ID 會產生執行階段錯誤。`std::concurrent::cancel_requested()` 在收到請求的 worker 內回傳 `true`，在 worker 外回傳 `false`。worker 應在限時等待之間輪詢；請求不會中斷已阻塞的操作或強制終止執行緒。`task<T>.cancel()` 還會關閉結果通道。

`pending(ch)`、`capacity(ch)` 和 `task_group<T>.size()` 傳回非負 `int64`。`std::concurrent::make_channel<T>` 接收 `int64` 容量：`0` 表示無界，正數表示有界通道。負數或無法表示的容量會報錯，實際配置仍受宿主限制。工作 ID 仍為 `int`。

- [標準函式庫基礎](foundation.md)

<a id="concurrency-aot-boundary"></a>

獨立 AOT 的邊界：task<T>、task_group、when_all 及 mutex／semaphore／synchronized 現在依賴 Core 的能力身分、擁有者與消費狀態檢查，包含字串形式的 spawn_task。C++／Python／C# 獨立 AOT 會在產生前明確拒絕這些操作；請使用 Core 的 VM／Mixed／JIT。C++／Python 原有的一般 channel、字串 spawn 與 join 路徑仍保留。

擁有該工作的 Core VM 結束時，會先中斷 owned worker 中的 Core 通道等待，再等待工作結束。既有直接引數通道的關閉行為保留，此退出通知本身不會關閉其他通道。明確取消仍為協作式，detached worker 保持獨立；阻塞的宿主回呼仍需宿主配合。

<!-- docs-navigation -->

---

[使用者文件](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-TW/index.md)

語言: [en-US](../../en-US/standard-library/concurrent.md) · [fr-FR](../../fr-FR/standard-library/concurrent.md) · [ja-JP](../../ja-JP/standard-library/concurrent.md) · [ko-KR](../../ko-KR/standard-library/concurrent.md) · [zh-CN](../../zh-CN/standard-library/concurrent.md) · [zh-TW](concurrent.md)
