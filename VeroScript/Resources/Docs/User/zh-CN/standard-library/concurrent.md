# 并发模块

模块文件：`concurrent.vs`

导入方式：

```VeroScript
import "concurrent.vs";
```

## 通道

`channel<T>` 是用于任务之间通信的共享容器。标准库为所有可传输的通道元素类型提供泛型操作：

| 函数 | 说明 |
| --- | --- |
| `std::concurrent::channel_send<T>(channel<T> ch, T value)` | 向通道发送一个值。 |
| `std::concurrent::channel_recv<T>(channel<T> ch)` | 从通道接收一个值。 |
| `std::concurrent::channel_close<T>(channel<T> ch)` | 关闭通道。 |
| `std::concurrent::make_channel<T>(int64 capacity = 0)` | 创建无界或有界通道。 |
| `ch.try_send(value)` / `ch.send_for(value, timeout_ms)` | 非阻塞或限时发送；无法发送时返回 `false`。 |
| `ch.try_recv()` / `ch.recv_for(timeout_ms)` | 返回 `optional<T>`；空值表示通道为空、超时或已关闭。 |
| `ch.pending()` / `ch.capacity()` / `ch.is_closed()` | 队列深度与容量返回非负 `int64`（容量 `0` 表示无界）；关闭状态返回 `bool`。 |

较短的 `send`、`recv` 和 `close` 同时支持命名空间调用与扩展调用：`ch.send(value)`、`ch.recv()`、`ch.close()`。`T` 可以是通道支持的可传输标量（不同宽度的有符号/无符号整数、`float`、`double`、`bool`、`string` 或 `wstring`），也可以是另一个 `channel<U>`。嵌套通道可以在隔离 worker 之间安全传递同步能力，而不共享普通可变对象。从打开的通道接收值可能会等待；关闭通道后，不应继续执行依赖有效发送或接收的操作。

通道值具有共享句柄语义。

## 隔离任务

`std::concurrent::spawn` 启动一个隔离任务，并返回任务 ID。任务入口必须是与通道类型匹配的函数：

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

`std::concurrent::join_task` 等待任务结束。任务之间不能直接共享普通脚本局部变量，应通过通道交换支持的值。

`std::concurrent::spawn<T>` 接受任意受支持的 `channel<T>`，但指定的任务入口必须具有完全匹配的签名：

```text
int std::concurrent::spawn<T>(string name, channel<T> ch);
```

## 类型化任务结果

`std::concurrent::spawn_task<T>` 返回 `task<T>`，适合任务通过结果通道返回一个值：

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

字符串入口必须具有精确签名 `void compute(channel<T> output)`，返回前发送唯一结果。worker 完成后结果缺失或多余均报错；尚未完成而阻塞的 worker 仍需协作取消。

`task<T>` 为所有受支持的可传输通道类型提供以下成员：

| 成员 | 说明 |
| --- | --- |
| `valid()` | 判断任务是否仍然处于活动状态。 |
| `done()` | 不等待任务，直接检查任务是否完成。 |
| `await()` | 等待任务、接收一个结果，并将任务标记为非活动。 |
| `await_for(timeout_ms)` | 只有完成等待超时才返回空 optional，任务保持活动；worker 完成后必须有唯一结果，否则报错。 |
| `cancel()` | 协作式取消任务并关闭结果通道；VM 会保留 worker 直到其退出。 |

协作式取消不会强制终止或分离线程。worker 应观察通道关闭并退出；对于忽略取消的 worker，VM/Session 销毁会继续等待，以确保宿主 callback 的生命周期安全。

## callable 任务入口

导入 `concurrent.vs` 后，可以将已解析的脚本函数、静态方法或函数值（含 Lambda）直接传给 `std::concurrent::spawn(callable, 参数...)` 或 `std::concurrent::spawn_task<T>(callable, 参数...)`。参数可以是不同类型，按从左到右的顺序求值一次。`std::concurrent::spawn` 要求返回 `void`；`std::concurrent::spawn_task<T>` 要求返回与 `T` 精确一致的可传输值。现有字符串入口保持可用。

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

参数和按值捕获仅能包含可传输的标量、字符串或通道句柄；引用捕获、`this` 捕获、实例方法、普通可变对象、数组、`ref` 形参和引用返回不允许跨任务边界。worker 使用独立 VM 和输入快照；`await()` 仍会阻塞当前线程。取消是协作式请求，worker 可查询 `std::concurrent::cancel_requested()`。C++、Python、C# AOT 目前会在编译期明确拒绝 callable 入口。完整用法见 [并发样例](../../../../Scripts/examples/concurrent_demo.vs)。

## 同步能力

`task_group<T>` 收集同类型任务，并提供 `add`、`await_all` 和 `cancel_all`；对应的自由函数是 `std::concurrent::when_all` 和 `std::concurrent::cancel_all`。

`mutex` 和 `counting_semaphore` 是同步通道句柄的显式别名，可以通过嵌套通道传递。使用 `std::concurrent::make_mutex`、`std::concurrent::mutex_lock`、`std::concurrent::mutex_try_lock`、`std::concurrent::mutex_lock_for`、`std::concurrent::mutex_unlock`，以及 `std::concurrent::make_semaphore`、`std::concurrent::semaphore_acquire`、`std::concurrent::semaphore_try_acquire`、`std::concurrent::semaphore_acquire_for`、`std::concurrent::semaphore_release`。重复解锁或超过信号量最大值会产生运行时错误。

`std::concurrent::make_synchronized<T>(initial)` 创建容量为 1 的通道单元；`std::concurrent::synchronized_load`、`std::concurrent::synchronized_store`、`std::concurrent::synchronized_exchange` 和 `std::concurrent::synchronized_fetch_add` 对访问进行串行化，但不承诺硬件 `atomic<T>` 的无锁保证。

## 任务取消请求

`std::concurrent::cancel_task(int id)` 对有效任务 ID 发出协作式取消请求，不消耗 ID；之后仍可用 `std::concurrent::join_task(id)` 等待。无效 ID 会报运行时错误。`std::concurrent::cancel_requested()` 在收到请求的 worker 内返回 `true`，在 worker 外返回 `false`。worker 应在限时等待之间轮询；请求不会打断已阻塞的操作或强制终止线程。`task<T>.cancel()` 还会关闭结果通道。

`pending(ch)`、`capacity(ch)` 和 `task_group<T>.size()` 返回非负 `int64`。`std::concurrent::make_channel<T>` 接收 `int64` 容量：`0` 表示无界，正数表示有界通道。负数或不可表示的容量会报错，实际分配仍受宿主限制。任务 ID 仍为 `int`。

- [标准库基础](foundation.md)

<a id="concurrency-aot-boundary"></a>

独立 AOT 的边界：task<T>、task_group、when_all 及 mutex／semaphore／synchronized 现在依赖 Core 的能力身份、拥有者和消费状态检查，包括字符串形式的 spawn_task。C++／Python／C# 独立 AOT 会在生成前明确拒绝这些操作；使用 Core 的 VM／Mixed／JIT 运行。C++／Python 原有普通 channel、字符串 spawn 和 join 路径继续保留。

拥有该任务的 Core VM 收尾时，会先中断 owned worker 中的 Core 通道等待，再等待任务结束。原有直接实参通道的关闭行为保留，此退出通知本身不会关闭其他通道。显式取消仍为协作式，detached worker 保持独立；阻塞的宿主回调仍需宿主配合。

<!-- docs-navigation -->

---

[用户文档](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/zh-CN/index.md)

语言: [en-US](../../en-US/standard-library/concurrent.md) · [fr-FR](../../fr-FR/standard-library/concurrent.md) · [ja-JP](../../ja-JP/standard-library/concurrent.md) · [ko-KR](../../ko-KR/standard-library/concurrent.md) · [zh-CN](concurrent.md) · [zh-TW](../../zh-TW/standard-library/concurrent.md)
