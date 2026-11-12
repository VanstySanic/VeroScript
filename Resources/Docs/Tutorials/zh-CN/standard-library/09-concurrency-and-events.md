# 09：并发和事件

并发最容易出错的地方是共享状态。本篇不直接共享普通局部变量，而是用通道传递结果；随后用多播委托把结果通知给多个处理函数。

## 第一步：让任务处理一个值

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

任务通过 `channel<int>` 接收和发送值。`std::concurrent::spawn` 接受已解析的脚本函数、静态方法或函数值，并返回任务 ID；`join_task` 等待任务结束。`spawn_task<T>` 接受相同的可调用对象，并通过 `task<T>.await()` 返回带类型的结果。参数和 lambda 捕获必须是可传输值，例如标量、字符串或通道句柄；每个任务都在隔离的 VM 中运行。`send`、`recv`、`close` 是通过 VeroScript 统一扩展调用语法暴露的普通可见自由函数。脚本不能把普通局部变量直接当作跨任务共享内存；需要交换值时应使用通道。可调用任务受执行后端限制，详见并发参考。

取消采用协作方式。程序其他部分调用 `std::concurrent::cancel_task(id)` 后，正在运行的任务可轮询 `std::concurrent::cancel_requested()`。此请求不会打断阻塞操作，也不会强制终止任务；`task<T>.cancel()` 还会关闭该任务的结果通道。

## 第二步：广播任务结果

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

多播委托保存多个 `void` 处理函数。`broadcast` 会按当前注册顺序调用它们，适合“一个结果、多个响应者”的场景。

## 第三步：决定什么时候使用哪种机制

- 函数调用：调用者知道具体函数，并需要立即得到返回值。
- `delegate`：只需要一个可替换处理函数。
- `multicast_delegate`：需要通知多个处理函数。
- `channel<T>`：需要在任务之间传递值。
- `spawn_task<T>`：运行可调用对象并返回可等待的带类型结果。
- `make_synchronized<T>`、互斥量和信号量：为可传递的共享状态提供显式同步句柄。

并发代码要特别注意等待顺序。发送方等待接收方，而接收方又等待发送方，是最常见的死锁来源。先设计消息流，再写 `spawn` 和 `join_task`。

## 练习

1. 把 `worker` 改成接收两个整数并返回它们的和。
2. 为 `on_result` 增加一个只在结果大于 `40` 时输出的处理函数。
3. 使用 `channel<string>` 传递一条状态消息，并在主函数中关闭通道。

可运行示例：[`examples/concurrent_demo.vs`](../../../../../VeroScript/Resources/Scripts/examples/concurrent_demo.vs) 和 [`examples/delegates.vs`](../../../../../VeroScript/Resources/Scripts/examples/delegates.vs)。详细规则见[并发模块](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/concurrent.md)和[事件模块](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/events.md)。

下一篇学习如何把多个源文件交付为包：[构建包并分发应用](../toolchain/10-packages-and-distribution.md)。

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/standard-library/09-concurrency-and-events.md) · [fr-FR](../../fr-FR/standard-library/09-concurrency-and-events.md) · [ja-JP](../../ja-JP/standard-library/09-concurrency-and-events.md) · [ko-KR](../../ko-KR/standard-library/09-concurrency-and-events.md) · [zh-CN](09-concurrency-and-events.md) · [zh-TW](../../zh-TW/standard-library/09-concurrency-and-events.md)
