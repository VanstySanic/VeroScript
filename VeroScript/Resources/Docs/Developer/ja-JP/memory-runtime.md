# 管理メモリの実装契約

Core が `Public/VeroScriptMemory.h` と `Private/Memory/` のメモリサービスを所有します。コンパイラの変換は `Private/Compiler/VeroScriptCompilerMemory.cpp`、実行は `Private/ExecutionSupport/VeroScriptVmMemory.cpp` にあります。`stdlib/memory.vs` はモジュールの入口であり、別実装ではありません。アダプターはこれらのサービスを共有し、独自の ID レジストリや弱い所有権検証を導入しないでください。

## 不変条件と検証

ペイロードに触れる前に信頼されたハンドルの由来を確認します。ID は弱い所有権情報を使い、寿命を延ばさず、再利用されたアドレスを旧オブジェクトと混同しません。領域は所有者を共有し、ビュー記述子はオフセット、長さ、世代、読み取り専用種別を持ちます。`FMemoryContext` はアクセスドメインを所有し、worker は予算を共有しても所有スレッドは別です。別スレッドで呼べる callable もドメインを移動できません。

長さゼロを含む全操作がドメイン、世代、境界、書き込み権限を確認します。拡張は確定前に新旧両方の割り当てを計上します。一括操作は 64 KiB ごとに実行制御を確認し ceil(bytes/64) を計上し、スカラー書き込みはバイトに触れる前に確認します。貸出は構造変更と競合アクセスを防ぎます。解放 callback は Core のロック外で一度だけ実行され、例外を投げてはいけません。

## ホストと制限

C++ は `VeroScriptMemory.h` と持続する `FMemoryContext`、または Session の `GetMemoryContext()` を利用します。C ABI の `vs_interop_memory_*` には一つの Session または有効な呼び出しコンテキストを渡します。既存 `vs_interop_buffer` は別のシリアライズ結果バッファーです。

外部領域の解放責任は登録成功時のみ移転し、別名は無効化を共有します。競合アクセスや構造変更はリースにより即座に Busy になります。ネイティブ処理完了後、不透明トークンを一度だけ解放します。任意スレッド、Session 破棄後も可能です。最終解放コールバックはコアのロック外で実行し例外を投げてはいけません。リースなしのアドレスはその場の観察専用です。

MaxBufferBytes は単一容量／外部長、MaxRetainedBytes は所有容量と借用バイトの合計で拡張時の旧新割り当ても含みます。0 は無制限。OwnedBytes、BorrowedBytes、PeakOwnedBytes は VM／オブジェクトのメタデータを含みません。Run、Clear、再ロードで保持量はリセットされません。worker は予算を共有しアクセス域は別です。領域をタスク捕捉、引数、戻り値、チャネルで渡せません。

C# は `session.Memory`、`SetMemoryLimits`、`using var lease = memory.AcquireLease(bytes, true)`。Python は `session.memory`、`set_memory_limits`、`with memory.acquire_lease(bytes, write=True) as lease`。リース終了前に memoryview を解放します。外部バッファーは最後の領域／リースまで固定されます。元の ref/const-ref 対象は `vs_interop_memory_call_argument_address`、C# `context.GetArgumentAddress`、Python `current_memory().argument_address` で照会します。値渡しの変換コピーは Unsupported です。

## バックエンドとデバッグ

VM と Mixed に対応します。AsmJit はアドレス依存とコアメモリメンバー関数を実行前に拒否し既存の VM フォールバックを使います。メモリ組み込みはコアサービスを使います。実行制御は既存 VM 方針を維持します。独立 C++/C#/Python AOT は実際の用途を拒否し、未使用 memory/core import は許可します。バイトコード／パッケージはポインター、リース、ID 登録表を保存しません。既定オブジェクトスナップショットは入れ子領域を拒否し、明示バイトコピーか専用シリアライザーを使います。

停止中デバッグはコア情報を表示します。DAP variablesReference はプロトコル ID のままで任意 readMemory/writeMemory は有効化しません。Android JNI と iOS Runner はコアを共有し、既定でアドレス権限を無効にします。C#/Python Session を直接使う場合、ホストが予算と権限を明示的に設定します。Wasm は bigint で ID、長さ、制限を保ち、インスタンス所属を確認し、ヒープ成長後にビューを作り直します。ホスト領域予算とスクリプト Session 予算は別で、既定は一領域 16 MiB、保持合計 32 MiB です。

六つの Core 言語に同じキーと安定 ID の `memory.json` を維持します。所有・外部領域、ID、失効、予算、Busy、callback 所有権、別スレッド解放、Session 終了、reload、snapshot、worker 制限、AOT 拒否を確認します。Core/Cpp/Interop のメモリテスト、各 binding の契約、ロケール配布と文書検証を実行します。Web は heap 拡張と対話入力中の排他を検証し、モバイル実機検証は別途行います。


<!-- docs-navigation -->

---

[VeroScript](index.md) · [en-US](../en-US/memory-runtime.md) · [fr-FR](../fr-FR/memory-runtime.md) · [ja-JP](memory-runtime.md) · [ko-KR](../ko-KR/memory-runtime.md) · [zh-CN](../zh-CN/memory-runtime.md) · [zh-TW](../zh-TW/memory-runtime.md)
