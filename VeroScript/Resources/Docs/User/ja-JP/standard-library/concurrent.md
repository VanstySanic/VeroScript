# 同時実行モジュール

モジュールファイル: `concurrent.vs`

次のようにインポートします。

```VeroScript
import "concurrent.vs";
```

## チャンネル

`channel<T>` は、タスク間の通信に使用される共有コンテナです。標準ライブラリは、すべての転送可能なチャネル要素型の汎用操作を公開します。

|機能 |説明 |
| --- | --- |
| `std::concurrent::channel_send<T>(channel<T> ch, T value)` | 1 つの値をチャネルに送信します。 |
| `std::concurrent::channel_recv<T>(channel<T> ch)` |チャネルから 1 つの値を受け取ります。 |
| `std::concurrent::channel_close<T>(channel<T> ch)` |チャンネルを閉じます。 |
| `std::concurrent::make_channel<T>(int64 capacity = 0)` | 非境界または境界付きのチャネルを作成します。 |
| `ch.try_send(value)` / `ch.send_for(value, timeout_ms)` | ブロックせずに、またはタイムアウト付きで送信し、送信できない場合は `false` を返します。 |
| `ch.try_recv()` / `ch.recv_for(timeout_ms)` | `optional<T>` を返します。空の場合は空、タイムアウト、またはクローズを表します。 |
| `ch.pending()` / `ch.capacity()` / `ch.is_closed()` | キューの深さと容量は非負の `int64`（容量 `0` は無制限）、クローズ状態は `bool` を返します。 |

短い形式の `send`、`recv`、および `close` は、名前空間呼び出しと拡張構文の両方をサポートします (`ch.send(value)`、`ch.recv()`、および `ch.close()`)。 `T` は、チャネルでサポートされている転送可能なスカラー (符号付き/符号なし整数幅、`float`、`double`、`bool`、`string`、または `wstring`) または別の `channel<U>` です。ネストしたチャネルは、通常の可変オブジェクトを共有せずに、分離されたワーカー間で同期機能を安全に渡します。開いているチャネルからの受信は待機する場合があります。チャネルを閉じた後は、有効な送信または受信を必要とする操作を続行しないでください。

チャネル値は共有ハンドル セマンティクスを使用します。

## 分離されたタスク

`std::concurrent::spawn` は、分離されたタスクを開始し、タスク ID を返します。ワーカー エントリ ポイントはチャネル タイプと一致する必要があります。

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

`std::concurrent::join_task` はタスクが完了するのを待ちます。タスクは、通常のスクリプト ローカルを直接共有するのではなく、チャネルを通じてサポートされている値を交換する必要があります。

`std::concurrent::spawn<T>` は、指定されたワーカーが完全に一致するエントリ署名を持っている場合、サポートされているすべての `channel<T>` を受け入れます。

```text
int std::concurrent::spawn<T>(string name, channel<T> ch);
```

## 入力されたタスクの結果

`std::concurrent::spawn_task<T>` は、結果チャネルを通じて 1 つの結果を公開するワーカーに対して `task<T>` を返します。

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

文字列エントリは正確に `void compute(channel<T> output)` を指定し、戻る前に一つの結果を送信します。終了後の結果不足・複数結果はエラーです。終了前にブロックした worker には協調キャンセルが必要です。

`task<T>` は、サポートされているすべての転送可能なチャネル型に対して次のメンバーを提供します。

|メンバー |説明 |
| --- | --- |
| `valid()` |タスクがまだアクティブであるかどうかをテストします。 |
| `done()` |待機せずにタスクの完了をテストします。 |
| `await()` |タスクを待機し、結果を 1 つ受け取り、それを非アクティブとしてマークします。 |
| `await_for(timeout_ms)` | 完了待機のタイムアウトだけが空 optional を返し、タスクは有効なままです。終了後の結果不足・複数結果はエラーです。 |
| `cancel()` |協調的にタスクをキャンセルし、結果チャネルを閉じます。VM は終了までワーカーを保持します。 |

協調キャンセルはスレッドを強制終了またはデタッチしません。ワーカーはチャネルのクローズを監視して終了する必要があります。ホスト callback の寿命を守るため、VM/session の破棄はキャンセルを無視するワーカーを待機します。

## callable タスク エントリ

`concurrent.vs` をインポートすると、解決済みのスクリプト関数、静的メソッド、関数値（ラムダを含む）を `std::concurrent::spawn(callable, 引数...)` または `std::concurrent::spawn_task<T>(callable, 引数...)` に直接渡せます。異なる型の引数を左から一度だけ評価します。`std::concurrent::spawn` は `void` 戻り値、`std::concurrent::spawn_task<T>` は `T` と厳密に一致する転送可能な戻り値を必要とします。従来の文字列エントリも引き続き利用できます。

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

引数と値キャプチャに使用できるのは転送可能なスカラー、文字列、チャネル ハンドルだけです。参照キャプチャ、`this` キャプチャ、インスタンス メソッド、可変オブジェクト、配列、`ref` パラメーター、参照戻り値は worker に渡せません。worker は独立した VM と入力のスナップショットを使います。`await()` は現在のスレッドをブロックします。キャンセルは協調的で、worker は `std::concurrent::cancel_requested()` を確認できます。C++、Python、C# AOT は現在 callable エントリをコンパイル時に拒否します。[並行処理の例](../../../../Scripts/examples/concurrent_demo.vs)を参照してください。

## 同期機能

`task_group<T>` は同じ型のタスクをまとめ、`add`、`await_all`、`cancel_all` を
提供します。対応する free helper は `std::concurrent::when_all` と
`std::concurrent::cancel_all` です。

`mutex` と `counting_semaphore` は同期チャネル ハンドルの別名であり、nested
channel を通して転送できます。`std::concurrent::make_mutex`、`std::concurrent::mutex_lock`、`std::concurrent::mutex_try_lock`、
`std::concurrent::mutex_lock_for`、`std::concurrent::mutex_unlock`、`std::concurrent::make_semaphore`、`std::concurrent::semaphore_acquire`、
`std::concurrent::semaphore_try_acquire`、`std::concurrent::semaphore_acquire_for`、`std::concurrent::semaphore_release` を使用します。
二重 unlock と semaphore の最大値超過は runtime error です。

`std::concurrent::make_synchronized<T>(initial)` は容量 1 の channel cell を作ります。
`std::concurrent::synchronized_load`、`std::concurrent::synchronized_store`、`std::concurrent::synchronized_exchange`、
`std::concurrent::synchronized_fetch_add` はアクセスを直列化しますが、hardware `atomic<T>` の
lock-free 保証を意味しません。

## タスクのキャンセル要求

`std::concurrent::cancel_task(int id)` は有効なタスク ID に協調的なキャンセルを要求します。ID は消費されず、後で `std::concurrent::join_task(id)` を呼べます。無効な ID は実行時エラーです。`std::concurrent::cancel_requested()` は要求後の対象 worker 内で `true`、worker 外では `false` です。worker は期限付き待機の合間に確認してください。ブロック中の操作を中断したりスレッドを強制終了したりしません。`task<T>.cancel()` は結果チャネルも閉じます。

`pending(ch)`、`capacity(ch)`、`task_group<T>.size()` は非負の `int64` を返します。`std::concurrent::make_channel<T>` の容量は `int64` で、`0` は無制限、正の値は容量付きチャネルを指定します。負値や表現できない容量はエラーで、実際の確保にはホストの上限が適用されます。タスク ID の型は引き続き `int` です。

- [標準ライブラリ基盤](foundation.md)

<a id="concurrency-aot-boundary"></a>

独立 AOT の制限：task<T>、task_group、when_all と mutex／semaphore／synchronized は、文字列を指定する spawn_task を含め、Core の能力識別、所有者、消費状態の検査を必要とします。C++／Python／C# の独立 AOT は出力前に明示的に拒否します。Core の VM／Mixed／JIT で実行してください。C++／Python の通常の channel、文字列 spawn、join は引き続き利用できます。

所有元の Core VM が終了すると、所有するワーカーの Core チャネル待機を中断してから終了を待ちます。直接引数のチャネルを閉じる既存動作は維持され、この通知自体は他のチャネルを閉じません。明示的キャンセルは協調的なままで、分離済みワーカーは独立し、ブロックするホストコールバックにはホスト側の協力が必要です。

<!-- docs-navigation -->

---

[ユーザー文書](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../../en-US/standard-library/concurrent.md) · [fr-FR](../../fr-FR/standard-library/concurrent.md) · [ja-JP](concurrent.md) · [ko-KR](../../ko-KR/standard-library/concurrent.md) · [zh-CN](../../zh-CN/standard-library/concurrent.md) · [zh-TW](../../zh-TW/standard-library/concurrent.md)
