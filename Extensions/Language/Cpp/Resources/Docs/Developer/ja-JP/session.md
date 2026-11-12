# C++ embedding と session

## 状態を保持しないエンジン

`VeroScriptEngine` は persistent state を必要としない caller に `CheckSource`、`CheckFile`、`RunSource`、`RunFile` を提供します。file operation は bytecode cache directory を利用でき、source label、input/output/error stream、circular-import、project-wide-access は explicit parameter です。result は success/failure と structured diagnostic を持ち、error stream の一行は result object を置換しません。

## 状態を保持するセッション

`VeroScriptSession` は loaded module、host registration、I/O bridge、runtime configuration、optional hot-reload state を所有します。`IEngineHost` で構築し、configuration/binding を適用して `LoadSource`、file、validated package を load または module を compile/commit し、run/call/inspect/reflect を行います。configuration、load、compile、commit、reload、clear、binding mutation は run 中に拒否されます。`LoadFile` だけが file-backed reload path を記録し、reload は replacement と package kind を検証後に commit します。

## evaluation と value

`Run(ERunMode::Eval)` は `main` を呼ばずにモジュールを初期化します。呼び出し間のグローバル状態は `SetHotReloadEnabled(true)` の場合だけ保持されます。call は `FValue` を返し、object construction、field/global access、callable retention を行えます。retained callable は明示 release し、owner-thread-only invocation を設定できます。external serializer は registered runtime type/encoded version に従い、serialize、deserialize、reference enumeration は別 operation です。

## host binding と metadata

registration は native function、host type、generic host-type template、member、operator、clone callback、inheritable type、scoped enum、subclass factory を対象とします。registration error は stable diagnostic ID で報告され、reflection は loaded type、field、method、function、global、enum、interface を read-only metadata として enumerate します。

## JIT と clearing

<a id="session-jit-cache"></a>

再利用には選択済みファクトリー、VM 以外の有効ポリシー、`FJitPolicy::bReuseSessionCode` が必要です。対象の `Run`、`CallFunction`、`CallMethod`、保持した callable の呼び出しは世代キャッシュとコンパイル予算を共有します。`MaxCompileAttempts` または `MaxCachedEntries` のゼロは無制限を意味し、無効化ではありません。永続実行と安定したコールバックは再利用できます。ネイティブカタログは `IHostBindingCatalog::GetRevision()` で実行時の改訂を公開します。バックエンドが必要な実行制御機能を備えていれば、制御付きネイティブ実行でもコードを再利用でき、Session はその制御コンテキストを VM 間で保持します。構成、ポリシー、バックエンド、モジュール、バインディングの変更で保持コードを無効化し、リロード失敗時は以前のプログラムを保持します。既定値は実行コード 8 MiB、保持ペイロード 32 MiB です。`GetJitCacheStats` は確定した世代集計、`GetLastJitStats` は直近の呼び出しを示し、プロセス全体のメモリ計測ではありません。

## 実行の制御と制限

`SetExecutionLimits` は命令数と実時間を制限し、ゼロは無制限です。共有の取消制御を `SetExecutionControl` で設定し、`Run` 中も `RequestCancel` を呼べます。取消は協調的で、実行中のネイティブコールバックを強制停止しません。`SetCapabilities` の標準ライブラリ権限は、Core がインタープリタと対応するネイティブ経路の両方で検査します。`bAllowProcessDirectoryChange=false` は `chdir` を禁止し、`chdir` には環境書き込み権限も必要です。`SetExecutionBackend` は `Vm`、`Mixed`、`Jit` を選びます。制御付き `Mixed` は既定で VM を使い、`SetJitPolicy` と `FJitPolicy::bEnableControlledMixed=true` で動的ネイティブ実行を有効にできます。明示的な `Jit` と `Mixed` で選んだ静的 Core AOT はこのスイッチに依存しません。必要な制御機能がなければ実行前に VM へ戻り、デバッグ、保持された初期化区間、有界クリーンアップは VM を使用します。

## call、interception、persistence

public call surface は `HasFreeFunction`、`HasInstanceMethod`、`CallFunction`、`CallMethod`、`CallMethodBypassingInterceptor`、`ConstructObject`、`RetainCallable`、`InvokeCallable`、field/global access、`ApplyClassDefaults`、reflection を含みます。bypass は選択 entry interceptor だけを飛ばし nested call は通常どおり intercept されます。`RegisterExternalSerializer`、`SerializeObject`、`DeserializeObject`、`EnumerateObjectReferences` は Interop serialization/borrowed-reference contract に従います。callable の owner-thread、weak-`this` capture、module-generation invalidation は explicit lifetime rule です。`Reload`、`ReloadCompiledModule`、`ReloadPackage` は replacement を transactionally stage/validate し、failure は最後の正常 module と structured diagnostic を保持します。

## 保持バイト割当量

`MaxNativeBytes` または `MaxRetainedPayloadBytes` のゼロは該当領域の保持を許可しません。ポリシー予算のゼロ（無制限）とは異なります。

## キャンセルとホストへの通知

<a id="session-control-and-streams"></a>

`RequestCancel()` は現在の制御オブジェクトの共有スナップショットを原子的に取得します。所有者が実行していない間に制御を交換しても安全です。null は何もせず、別の制御にはキャンセルを転送しません。`GetExecutionControl()` は所有者スレッド専用の借用参照です。Session の破棄前にキャンセル呼び出しと実行の終了を待ちます。行のビューはコールバック中だけ有効です。通知コールバックが例外を送出するとストリームが失敗を示し、再利用や破棄時に同じイベントを再送しません。`ResetForReuse` はストリーム状態を復元し、デストラクターはコールバック例外を抑制します。

同じ `FExecutionControl` トークンを共有する Session はキャンセル状態も共有します。`Reset()` と再利用の前に以前のキャンセル呼び出しの完了を待ち、古い要求がそのトークンを再利用する次の実行をキャンセルしないようにします。

## 実装の責任範囲

<a id="session-debug-implementation"></a>

`VeroScriptSessionDebug.h` は Session デバッグブリッジと借用ビューを定義し、実装は現在 `VeroScriptSession.cpp` にあります。`VeroScriptLiveDebugDomain.h` はホスト所有のライブドメイン契約を定義し、`VeroScriptDapAdapter.cpp` が使用します。同名の `.cpp` には現在ヘッダーの読み込みのみがあります。名前だけで実装の責任範囲を判断せず、上記の制御スレッドと有効期間の契約を守ってください。

## 任意の JIT 診断

`FJitPolicy::Diagnostics.MaxFunctions` の既定ゼロは関数別収集を無効化します。既存 policy のコピーに件数・深さ上限と任意の `bMeasureTime` を設定し、`SetJitPolicy` を呼びます。

`GetLastJitDiagnostics()` は最新実行の所有 snapshot を返し、一時 VM の破棄後も有効です。後の呼出しを越えて保つならコピーします。Run、debug、関数/メソッド呼出し、構築、保持 Callable で収集し、収集無効の次実行または `Clear()` で消去します。`DroppedRecords` は部分収集を示し、計時無効なら時間はゼロです。

`GetLastJitStats()` の実際の入口は `NativeLeafExecutions`、`NativeCfgExecutions`、`BridgeExecutions`、`LinearExecutions`、`VmTrampolineExecutions` です。コンパイル数と区別し、入れ子の scalar/direct-call/typed-loop を関数入口数に加算しません。inclusive 時間も関数間で合算しません。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
