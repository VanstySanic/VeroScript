# C# runtime と Interop adapter

## typed-loop 統計

`NativeTypedLoopExecutions` は native typed-loop kernel の invocation を数えます。source-visible runtime failure を報告して終了する invocation も含まれます。これは execution observability の counter であり、success counter ではありません。

`VeroScriptCSharp` は `VeroScriptInterop` の managed wrapper です。wrapper は ergonomic conversion、lease、callback root、managed exception を、C ABI は native layout、status、lifetime rule を所有します。

## 互換性の検証

`VeroScriptRuntime` は ABI version、semantic runtime version、host platform、capability bit、current language、available-language JSON、catalog load/merge を公開します。`EnsureCompatible()` は managed entry gate です。ABI と product semantic version は別契約で、表示 version に結合しません。`LoadCatalogDirectory` は `en-US` baseline を検証して catalog tree を置換し、`MergeCatalogDirectory` は検証済み tree を追加し既存 ID override を明示できます。JSON コピーや固定 culture list で merge を模倣してはいけません。

`EnsureCompatible()` は ABI の全体値を `SupportedAbiVersion`（現在は `0x00000101`、0.1.1）と比較します。不一致の場合は `NotSupportedException` を送出し、メジャーバージョンの一致や数値上新しい ABI だけでは受け入れません。ABI 変更時は `NativeMethods.AbiVersion`、ネイティブヘッダー、公開 API 契約、パッケージ文書を同期してください。

## session と handle

`VeroScriptSession` は native handle と managed root を所有し、idle/running/creator-thread rule に従います。`VeroScriptValue` は owning native value または `BorrowScope` 中だけ有効な borrowed view です。`VeroScriptPackage` は handle、metadata、canonical signed byte、loadable module state を所有します。`VeroScriptCallable` は owner thread を記録でき、active invocation 中の dispose は native release まで defer されますが、cross-thread invocation を合法にはしません。

## marshaling

adapter は fixed-width integer、floating value、boolean、UTF-8/wide string、enum、host object、buffer、array/map/set/queue/stack/deque/channel を明示的に marshal します。borrowed view は owner release 前に copy し、managed exception は native callback boundary で ABI error channel に変換して unmanaged C frame を越えません。

adapter は fixed-width number、boolean、UTF-8/wide string、enum、host object、buffer、array/map/set/queue/stack/deque/channel を正確に marshal します。borrowed view は owner release 前に copy します。managed exception は native callback boundary で ABI error channel に変換し、unmanaged C frame を越えません。

## configuration、cancellation、JIT

`VeroScriptConfiguration` は overlay/import policy に加え `MaxCallDepth`、`MaxInstructions`、`TimeoutMilliseconds`、`EnableExecutionControl`、`CapabilityPolicyEnabled`、`AllowedScriptCapabilities`、`ExecutionBackend` を持ちます。instruction/timeout zero は unlimited です。capability policy は VM が file/environment/console/task restriction を強制し、JIT は迂回しません。

`RequestCancellation` は non-owner thread から安全な cooperative request で、active native/managed callback を強制停止しません。`SetJitPolicy` は adaptive setting を適用して reusable code を invalidate し、`GetLastJitStats` は last run-scoped delta を返します。eligible な通常 `CallFunction` は既定で fresh VM 間の code reuse が可能で、`VeroScriptJitPolicy.ReuseSessionCode` を false にして opt out します。再利用には VM 以外の有効なバックエンドと、リビジョンを持つネイティブバインディングカタログが必要です。バインディング、監視コールバック、プログラム世代の変更はキャッシュを無効化します。実行ガバナンスが有効でも、要件が一致するキャッシュコンテキストは再利用できます。compile-attempt/cached-entry budget のゼロは回数無制限ですが、native-code/payload のバイト上限は引き続き適用されます。

実行ガバナンスが有効な `Mixed` は既定で VM を使います。対象の負荷を測定してから、`VeroScriptJitPolicy.EnableControlledMixed` で制御付き JIT を明示的に有効にしてください。明示的な `Jit` 選択はこのスイッチに依存しません。未対応またはコストモデルが VM を優先するエントリは、制限を保った VM にフォールバックします。バックエンドの `ControlledExecution` は対応能力を示し、`ControlledNativeExecutions` と `AotExecutions` は実際の制御付きネイティブ入口と AOT 入口の呼び出し数です。

`SetJitCacheLimits`/`GetJitCacheLimits` は native-code/payload ceiling を制御し、既定は 8 MiB/32 MiB、zero は該当 resource の retention を禁止します。`GetJitCacheStats` は current context の cumulative compile、settled byte、limit latch を返し、live/process-peak counter ではありません。これらは `VeroScriptCapabilities.SessionJitCache` が必要で、compat runtime に無い場合は `NotSupportedException` です。保持状態の初期化は VM で実行し、同じ世代の対応呼び出しではコンパイル済みコードを再利用できます。`VeroScriptCapabilities.ReloadStatus` があれば、作成元スレッドの `GetReloadStatus()` で操作と世代のスナップショットを取得できます。

## セッションの操作

session は `LoadSource`、`LoadFile`、`LoadPackage`、`Reload`、`ReloadPackage`、`BuildPackageFile`、`Run`、diagnostic、I/O、function/method check、`CallFunction`、`CallMethod`、`CallMethodBypassingInterceptor`、`Construct`、field/global access、`ApplyClassDefaults` を提供します。bypass は entry interceptor のみを飛ばし nested call は intercept されます。host binding、reflection/binding JSON、container/channel、retained callable、external serialization は Interop の borrow/callback/ownership rule を保存し、callable の weak-`this` lifetime もその規則に従います。

## diagnostic と managed lifetime

`GetDiagnostics`、`GetDiagnosticArguments`、`FormatDiagnostic` は structured ID、file label、coordinate、argument を保存し、`VeroScriptRunResult` は success、exit state/code、diagnostic count を返します。borrow は scope 終了で失効し、callback exception は enumeration 後に再送出、buffer は lease 終了前に copy します。

## JIT 統計の対応付け

`VeroScriptJitStats` は scalar fast path、trampoline entry、`DirectNativeCalls` を別 metric として保持します。`CostModelVmFallbacks` は有効な compiled path が VM より高コストと予測されて選ばれた `Fallbacks` の部分集合です。`NativeTypedLoopExecutions` は source-visible runtime failure を報告して終了するものを含む kernel invocation を数え、success count ではありません。これらはそれぞれ `VeroScriptJitBackendCapabilities.NativeCode` と `VeroScriptJitBackendCapabilities.NativeTypedLoops` で確認し、どちらも `HostPlatform` から推測してはいけません。`VeroScriptJitCacheStats` は policy/limit/backend/module/host-observability の invalidation までの cumulative context を表します。

[C# セッションとランタイム — JIT の明示選択](../../User/ja-JP/session.md#jit-backend-capabilities) を参照してください。

[C# セッションとランタイム — JIT の明示選択](../../User/ja-JP/session.md#diagnostic-translation) を参照してください。

## 保持された callable の破棄

[C# セッションとランタイム — 保持された callable の破棄](../../User/ja-JP/session.md#callable-disposal) を参照してください。

ネイティブ呼び出しは実行とネイティブビューのコピーが終了するまで値とパッケージのマネージド所有者を保持します。ブロックするチャネル操作と map/set のビジターは受信側を保持し、引数や要素のリストはインデクサーが生成した各値のラッパーをネイティブ側での使用終了まで保持します。GC による早期ファイナライズを防ぎますが、明示的な破棄とコールバックの有効範囲の規則は引き続き適用されます。回帰検証ではネイティブ受信、ビジター、オンデマンドのリストマーシャリング中に GC を強制し、最適化された .NET 8/9 実行と NativeAOT のパッケージ利用側で確認します。

<a id="managed-marshalling-maintenance"></a>

ネイティブ引数と要素のマーシャリングでは、ハンドルを提供した値ラッパーそのものの参照をスナップショットとして保持します。リストのインデクサーやマネージドコールバックが入力配列の要素を置き換えても、元のラッパーはネイティブ使用の終了まで生存し、`ref`/`out` の書き戻しは元のラッパーを更新します。回帰テストでは map 構築、直接の関数呼び出し、保持した callable の呼び出し中に要素を置き換えて GC を強制します。

[Android と iOS / MAUI](../../User/ja-JP/mobile.md): `VeroScriptCSharp`, `__Internal`, `AndroidNativeLibrary`, `NativeReference`, `ForceLoad`, `ReferenceNativeSymbol`, `VeroScriptMobileResources`, `MobileContracts.csproj`, `test_mobile_sdk.py`.

モバイル対応パッケージも同じプロジェクトとパッケージ名を使用します。`VeroScriptIncludeMobile=true` により .NET 8/9 と .NET 10 Android/iOS のターゲット、および検証済みネイティブ SDK を含めます。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/runtime-and-interop.md) · [fr-FR](../fr-FR/runtime-and-interop.md) · [ja-JP](runtime-and-interop.md) · [ko-KR](../ko-KR/runtime-and-interop.md) · [zh-CN](../zh-CN/runtime-and-interop.md) · [zh-TW](../zh-TW/runtime-and-interop.md)
