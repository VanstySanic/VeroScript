# 09: 同時実行性とイベント

共有状態では同時実行が困難になります。このチュートリアルでは、通常のローカル変数を共有する代わりにチャネルを通じて値を送信し、マルチキャスト デリゲートを使用して複数のハンドラーに通知します。

## ステップ 1: タスクに値を処理させる

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

タスクは、`channel<int>` を通じて値を送受信します。`std::concurrent::spawn` は解決済みのスクリプト関数、静的メソッド、または関数値を受け取り、タスク ID を返します。`join_task` は完了を待ちます。`spawn_task<T>` も同じ callable を受け取り、`task<T>.await()` で型指定された結果を返します。引数とラムダのキャプチャには、スカラー、文字列、チャネル ハンドルなどの転送可能な値が必要です。各タスクは独立した VM で実行されます。`send`、`recv`、および `close` は、VeroScript の統一された拡張呼び出し構文を通じて公開される通常の可視自由関数です。通常のローカル変数はタスク通信チャネルの代わりにはなりません。callable task の対応状況は backend によって異なります。制限は Concurrency リファレンスを参照してください。

キャンセルは協調的です。プログラムの別の箇所で `std::concurrent::cancel_task(id)` を呼んだ後、実行中のタスクは `std::concurrent::cancel_requested()` を確認できます。この要求はブロック中の操作を中断せず、タスクを強制終了しません。`task<T>.cancel()` はそのタスクの結果チャネルも閉じます。

## ステップ 2: 結果をブロードキャストする

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

マルチキャスト デリゲートには、複数の `void` ハンドラーが保存されます。 `broadcast` は、現在の登録順序でそれらを呼び出します。これは、1 つの結果が複数の独立した応答をトリガーする必要がある場合に便利です。

## メカニズムの選択

- 関数呼び出し: 呼び出し元は関数を知っており、即時の結果を必要とします。
- `delegate`: 1 つの交換可能なハンドラー。
- `multicast_delegate`: 複数のハンドラーの通知。
- `channel<T>`: タスク間で交換される値。
- `spawn_task<T>`: callable が実行され、待機できる型指定された結果を生成するタスク。
- `make_synchronized<T>`、mutex、semaphore: 転送可能な共有状態のための明示的な同期ハンドル。

順番待ちに注意してください。送信者が受信者を待機している間に受信者が送信者を待機するのは、最も一般的なデッドロック パターンです。 `spawn` および `join_task` を追加する前に、メッセージ フローを設計します。

## 演習

1. 2 つの整数を受け取り、その合計を返すように `worker` を変更します。
2. 結果が `40` より大きい場合にのみ出力するハンドラーを追加します。
3. `channel<string>` を通じてステータス文字列を送信し、main 関数でチャネルを閉じます。

実行可能な例: [`examples/concurrent_demo.vs`](../../../../../VeroScript/Resources/Scripts/examples/concurrent_demo.vs) および [`examples/delegates.vs`](../../../../../VeroScript/Resources/Scripts/examples/delegates.vs)。完全なルールについては、[同時実行](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/concurrent.md) および [イベント](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/events.md) を参照してください。

次: [パッケージの構築とアプリケーションの配布](../toolchain/10-packages-and-distribution.md)。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/standard-library/09-concurrency-and-events.md) · [fr-FR](../../fr-FR/standard-library/09-concurrency-and-events.md) · [ja-JP](09-concurrency-and-events.md) · [ko-KR](../../ko-KR/standard-library/09-concurrency-and-events.md) · [zh-CN](../../zh-CN/standard-library/09-concurrency-and-events.md) · [zh-TW](../../zh-TW/standard-library/09-concurrency-and-events.md)
