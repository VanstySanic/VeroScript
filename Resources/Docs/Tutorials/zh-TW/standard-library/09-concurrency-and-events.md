# 09：並發與事件

並發最容易出錯的地方是共享狀態。本篇不直接共享普通局部變數，而是用通道傳遞結果；隨後用多播委託把結果通知多個處理函式。

## 第一步：讓任務處理一個值

```VeroScript
import "concurrent.vs";

void worker(channel<int> input)
{
	int value = input.recv();
	input.send(value + 1);
}

int twice(int value)
{
	return value * 2;
}

int main()
{
	channel<int> pipe = channel<int>();
	int task_id = std::concurrent::spawn(worker, pipe);

	pipe.send(41);
	std::concurrent::join_task(task_id);

	int result = pipe.recv();
	assert(result == 42);
	task<int> doubled = std::concurrent::spawn_task<int>(twice, 21);
	assert(doubled.await() == 42);
	print(result);
	return 0;
}
```

任務透過 `channel<int>` 接收和傳送值。`std::concurrent::spawn` 接受已解析的腳本函式、靜態方法或函式值，並傳回任務 ID；`join_task` 等待任務結束。`spawn_task<T>` 接受相同的可呼叫物件，並透過 `task<T>.await()` 傳回帶型別的結果。引數和 lambda 擷取必須是可傳輸值，例如純量、字串或通道控制代碼；每個任務都在隔離的 VM 中執行。`send`、`recv`、`close` 是透過 VeroScript 統一擴充呼叫語法暴露的普通可見自由函式。腳本不能把普通局部變數直接當作跨任務共享記憶體；需要交換值時應使用通道。可呼叫任務受執行後端限制，詳見並發參考。

取消採協作方式。程式其他部分呼叫 `std::concurrent::cancel_task(id)` 後，正在執行的任務可輪詢 `std::concurrent::cancel_requested()`。此要求不會中斷阻塞操作，也不會強制終止任務；`task<T>.cancel()` 還會關閉該任務的結果通道。

## 第二步：廣播任務結果

```VeroScript
import "delegate.vs";

int main()
{
	multicast_delegate<int> on_result = multicast_delegate<int>();
	on_result.add(function(int value) -> void => print("log={}", value));
	on_result.add(function(int value) -> void => print("double={}", value * 2));

	on_result.broadcast(21);
	print("listeners={}", on_result.count());
	return 0;
}
```

多播委託儲存多個 `void` 處理函式。 `broadcast` 會依照目前註冊順序呼叫它們，適合「一個結果、多個回應者」的場景。

## 第三步：決定何時使用哪種機制

- 函式呼叫：呼叫者知道具體函式，並需要立即得到回傳值。
- `delegate`：只需要一個可替換處理函式。
- `multicast_delegate`：需要通知多個處理函式。
- `channel<T>`：需要在任務之間傳遞值。
- `spawn_task<T>`：執行可呼叫物件並傳回可等待的帶型別結果。
- `make_synchronized<T>`、互斥鎖和信號量：為可傳遞的共享狀態提供明確同步控制代碼。

並發程式碼要特別注意等待順序。發送方等待接收方，而接收方等待發送方，是最常見的死鎖來源。先設計訊息流，再寫 `spawn` 和 `join_task`。

## 練習

1. 把 `worker` 改成接收兩個整數並回傳它們的和。
2. 為 `on_result` 增加一個只在結果大於 `40` 時輸出的處理函式。
3. 使用 `channel<string>` 傳遞一條狀態訊息，並在主函式中關閉通道。

可執行範例：[`examples/concurrent_demo.vs`](../../../../../VeroScript/Resources/Scripts/examples/concurrent_demo.vs) 和 [`examples/delegates.vs`](../../../../../VeroScript/Resources/Scripts/examples/delegates.vs)。詳細規則請見[並發模組](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/concurrent.md)和[事件模組](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/events.md)。

下一篇學習如何把多個原始檔交付為套件：[建置套件並發佈應用程式](../toolchain/10-packages-and-distribution.md)。

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/standard-library/09-concurrency-and-events.md) · [fr-FR](../../fr-FR/standard-library/09-concurrency-and-events.md) · [ja-JP](../../ja-JP/standard-library/09-concurrency-and-events.md) · [ko-KR](../../ko-KR/standard-library/09-concurrency-and-events.md) · [zh-CN](../../zh-CN/standard-library/09-concurrency-and-events.md) · [zh-TW](09-concurrency-and-events.md)
