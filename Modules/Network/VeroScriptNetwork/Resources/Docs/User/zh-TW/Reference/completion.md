# 完成回呼與程式生命週期

腳本的按值捕獲遵循 Core 的唯讀綁定規則。需要對捕獲的宿主值呼叫可變原生成員時，使用 `mutable` Lambda，或對可寫綁定進行參考捕獲。初始化捕獲與區域 static 沿用 Core 的一般語意。 儲存在區域 static 中的 context 仍屬於目前程式世代，世代退役時會關閉。

Network/http/websocket 的 operation 接受一個 on_complete(fn(operation)->void)，可用 delegate/lambda/繫結方法，由 Core 代次根保有函式與捕捉。完成後登記也等明確 dispatch，不在登記時執行；傳入獨立擁有的 operation，可留待後續讀取。

poll 僅推進 HostDriven I/O，兩模式皆在建立腳本執行緒 dispatch(max_callbacks)。回傳本批完成記錄數，0 不執行、最多 1,048,576。終態不保證已入隊；回呼新增記錄留下一批，同 context 不可遞迴。回呼的參數/配置/原生失敗使本次腳本呼叫失敗，不改網路結果、不重跑，批內後續腳本回呼略過。派發狀態恢復，可再處理後續批次；VM 預算、取消、期限是協作式，不能強制中止任意阻塞 C++。

I/O 不保留借用 VM/ScriptInvoker/Args/ref，只於派發借用目前有效服務；跨執行緒清理退役根，由所屬安全點回收。持久 Session 函式返回保留同代次。裸 VM 頂層結束、Session 關閉或進入替換/reload 使根無效、關閉 context 並取消。替換前編譯失敗保留舊回呼；遷移回復不復活已失效網路工作，需重建 context。外部原生參考不延長舊腳本回呼；退役後登記/派發回報代次錯誤。

回呼內 shutdown 不等待自己，捨棄未派發、保留已擁有結果並解除捕捉循環。一般宿主匯出根須先釋放才能替換，內部完成根則主動失效。持久初始失敗在臨時 frame/global 存活時撤銷新根；清理錯誤可見，清理/解構不得再登記 async 根。其他宿主/AOT 須各自配接，不代表語言新增 await。

## 範例

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

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../../en-US/Reference/completion.md) · [fr-FR](../../fr-FR/Reference/completion.md) · [ja-JP](../../ja-JP/Reference/completion.md) · [ko-KR](../../ko-KR/Reference/completion.md) · [zh-CN](../../zh-CN/Reference/completion.md) · [zh-TW](completion.md)
