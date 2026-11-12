# Completion and program lifetime

Script value captures follow Core's readonly binding rules. To call a mutating native method on a captured host value, use a `mutable` lambda or a reference capture of a writable binding. Initialization captures and local statics use ordinary Core semantics. A context kept in a local static still belongs to its program generation and closes when that generation retires.

Network::operation, http::operation and websocket::operation accept one on_complete(fn(operation)->void), using delegates, lambdas or bound methods. Core generation roots retain functions/captures. Registering after completion still waits for explicit dispatch and never invokes inline. The callback receives an independently owned operation that can be retained.

context.poll drives only HostDriven I/O; both modes need context.dispatch(max_callbacks) on the creating script thread. The count is completed records handled this batch, 0 runs none and the maximum is 1,048,576. A terminal operation is not proof its completion record is already queued. New records from callbacks wait for another batch; recursion on the same context fails. A callback error fails the current script call without changing network results or rerunning it; later script callbacks in that batch are skipped, including argument/allocation/native failures. Dispatch state resets for later batches. VM work/instruction/cancel/deadline limits remain cooperative and cannot interrupt arbitrary blocking C++ callbacks.

Native I/O retains no borrowed VM, ScriptInvoker, Args or ref addresses. Owner-thread dispatch borrows only the current valid call services; cross-thread cleanup retires roots for an owner safe point. Persistent Session function returns preserve same-generation contexts/callbacks. A raw VM top-level end, session close or entry into replacement/reload invalidates roots, closes contexts and cancels pending work. Failed compilation before replacement preserves old callbacks; a later migration rollback does not revive invalidated network work, so recreate contexts. External native references do not revive script callbacks, and retired callback registration/dispatch reports generation errors.

Callbacks may shutdown their context without waiting on themselves. Shutdown discards queued callbacks, retains already owned results and breaks capture cycles through root cleanup. Ordinary exported host roots still block replacement until released; internal completion roots are actively invalidated. Failed persistent-state initialization revokes new roots before temporary frames/globals disappear. Cleanup errors remain visible, and cleanup/destruction cannot register new async roots. Other hosts/AOT need their own adapter; no language await is implied.

## Examples

```VeroScript
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

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](completion.md) · [fr-FR](../../fr-FR/Reference/completion.md) · [ja-JP](../../ja-JP/Reference/completion.md) · [ko-KR](../../ko-KR/Reference/completion.md) · [zh-CN](../../zh-CN/Reference/completion.md) · [zh-TW](../../zh-TW/Reference/completion.md)
