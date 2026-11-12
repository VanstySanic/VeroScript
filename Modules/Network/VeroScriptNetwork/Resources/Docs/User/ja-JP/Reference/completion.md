# 完了通知とプログラムの寿命

スクリプトの値キャプチャは Core の読み取り専用バインディング規則に従います。キャプチャしたホスト値を変更するネイティブメソッドには、`mutable` Lambda または書き込み可能なバインディングの参照キャプチャを使用します。初期化キャプチャとローカル static は通常の Core の意味論に従います。 ローカル static に保存した context もプログラム世代に属し、その世代の終了時に閉じられます。

3 protocol の operation は on_complete(fn(operation)->void) を一つ登録でき、delegate/lambda/bound method を Core generation root で保持します。完了後の登録も即時呼び出さず dispatch を待ちます。受け取る operation は独立所有して保存できます。

poll は HostDriven I/O のみ、両 mode とも作成 script thread の dispatch(max_callbacks) が必要です。0 は実行なし、最大 1,048,576、戻り値は処理した完了記録数です。terminal だけで queue 到着を仮定しません。callback が作った記録は次 batch、同 context 再帰不可です。callback の argument/allocation/native failure は script 呼び出しを失敗させ、network result は不変、再実行せず残り script callback を停止します。dispatch 状態は復元されます。VM 予算/期限/取消は協調的で、任意の blocking C++ を強制停止しません。

I/O は借用 VM/ScriptInvoker/Args/ref を保持せず、dispatch 中だけ現在の正当な service を借用します。他 thread cleanup は root を owner safe point へ退役します。persistent Session の通常 function return は同 generation を保持します。raw VM top-level 終了、Session close、replacement/reload 開始で root/context を無効化し取消します。replacement 前 compile failure は旧 callback を保持、migration rollback は無効化済み network を戻さず context 再作成が必要です。外部 native reference でも script callback は復活せず、退役後登録/dispatch は generation error です。

callback 内 shutdown は自分を待たず、未派発 callback を捨て、所有 result は残り、capture cycle を解消します。通常 host export root は置換前解放、internal completion root は能動無効化です。persistent 初期化失敗は frame/global 破棄前に新 root を取消します。cleanup error は通知し、cleanup/destructor 中に async root を再登録できません。他 host/AOT は個別 adapter が必要です。

## 例

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

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../../en-US/Reference/completion.md) · [fr-FR](../../fr-FR/Reference/completion.md) · [ja-JP](completion.md) · [ko-KR](../../ko-KR/Reference/completion.md) · [zh-CN](../../zh-CN/Reference/completion.md) · [zh-TW](../../zh-TW/Reference/completion.md)
