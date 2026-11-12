# 脚本完成回调与程序生命周期

脚本的按值捕获遵循 Core 的只读绑定规则。需要对捕获的宿主值调用可变原生成员时，使用 `mutable` Lambda，或对可写绑定进行引用捕获。初始化捕获与局部 static 沿用 Core 的普通语义。 保存在局部 static 中的 context 仍属于当前程序代次，代次退役时会关闭。

`Network::operation`、`http::operation` 和 `websocket::operation` 提供
`on_complete(fn(对应 operation)->void)`。回调可使用 delegate、lambda 和绑定方法；
函数及捕获值由 Core 的程序代次根保活。每个 operation 只能注册一个完成回调，最多执行一次。
操作已经结束时也可以注册；回调仍等待显式派发，不在注册调用内执行。

## 推进与派发

`context.poll()` 仅推进 HostDriven I/O。后台模式由原生线程推进 I/O。
两种模式都通过 `context.dispatch(max_callbacks)` 在创建 context 的脚本线程执行回调，
返回本批处理的完成记录数。传入 0 不运行回调；上限为 1,048,576。
操作终态发布与完成记录入队不是同一步，持续驱动与派发，不能依赖一次 dispatch 收齐刚完成的事件。

回调提交的新完成记录留待下一批；同一 context 禁止递归派发。不同 context 可以分别派发。
回调错误会使当前脚本调用失败，保留已有 VM 错误，不改变网络 operation 已发布的结果，也不会重跑回调。
后续批内脚本回调不再执行，包括准备参数或调用期间发生原生异常、分配失败的情况。
派发状态在退出时复位，后续批次仍可使用。批次数限制与 VM 工作/指令、取消、期限共同生效；
它不能强行终止任意阻塞的宿主 C++ 函数。

```veroscript
class CompletionFlag
{
public:
	bool done = false;
};
Network::context context = Network::create_context(false);
CompletionFlag flag = CompletionFlag();
Network::operation operation = Network::resolve(context, "localhost", 80, 5000);
operation.on_complete([flag](Network::operation completed) mutable -> void
{
	assert(completed.done());
	print(completed.message_id());
	flag.done = true;
});
for (int round = 0; round < 10000 && !flag.done; ++round)
{
	context.dispatch(64);
	if (!flag.done)
	{
		std::this_thread::sleep(1);
	}
}
assert(flag.done);
context.shutdown();
```

完整、有界循环样例位于三个模块各自的 `Resources/Examples/completion.vs`。

- [Network：解析完成通知](../../../../Examples/completion.vs)
- [HTTP：接受请求并回复](../../../../../../VeroScriptHttp/Resources/Examples/completion.vs)
- [WebSocket：连接、接收与发送](../../../../../../VeroScriptWebSocket/Resources/Examples/completion.vs)

HTTP 样例在接受完成回调中回复；WebSocket 样例在连接回调中提交下一次接收和发送。
回调收到的是独立、拥有的 operation 包装，可以保存在脚本状态中供后续查询。

## 保活、关闭与重载

I/O 不保存本次借用的 `ScriptInvoker`、VM、Args 或 ref 地址。
派发期间才借用当前合法调用服务；脚本函数值的访问和语言析构都在所属线程进行。
完成记录被其他线程清理时只退休根，语言清理由下一个所属线程安全点处理。

持久 C++ Session 的普通函数返回保留同代次的 context 和回调。裸 VM 的一次顶层执行结束、
Session 关闭或成功进入程序替换/重载时，旧代次根失效，并关闭其原生 context、取消未完成操作。
即使宿主继续持有原生结果或 context 的共享引用，也不会使旧脚本回调继续有效。
编译失败而未进入替换阶段时保留原程序与回调；进入替换/迁移后失效的网络操作不因迁移回滚而复活，
恢复的程序应重新创建 context。已退休 context 的派发或回调注册会报告代次错误。

回调可以调用 `context.shutdown()`；关闭不等待当前回调自己结束。
关闭会丢弃未派发回调，已发布结果仍按各自 Owner 存活。回调捕获 context 不形成永久强引用环：
代次根失效或关闭会解除完成注册，捕获的脚本对象经 Core 清理。

普通宿主导出根继续阻止程序替换，必须先释放；内部完成根由程序替换主动失效。
持久状态初始化失败时，也会在临时全局与帧仍有效时撤销新根。
清理失败会报告运行时错误；安全点清理及顶层结束的帧／全局析构期间不能重新注册异步根。

当前实现适用于使用上述 Core 调用服务的原生脚本宿主。浏览器/Node provider 与独立 AOT
仍须按各自实现和真实测试确认；本接口不表示语言新增了 await 或通用 future。

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../../en-US/Reference/completion.md) · [fr-FR](../../fr-FR/Reference/completion.md) · [ja-JP](../../ja-JP/Reference/completion.md) · [ko-KR](../../ko-KR/Reference/completion.md) · [zh-CN](completion.md) · [zh-TW](../../zh-TW/Reference/completion.md)
