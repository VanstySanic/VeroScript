# C# セッションとランタイム

## 実行モード

`Run(VeroScriptRunMode.Script)` はモジュールを初期化して `main` を呼び出します。
`Eval` は増分評価を維持します。`EvalOnce` は `main` を呼ばずに初期化を実行し、
選択した JIT バックエンドでの初期化を許可します。通常のセッションは実行ごとに
新しい状態を初期化します。ホットリロードは初期化済みのグローバル状態を保持し、その初期化には VM を使います。
`VeroScriptCapabilities.EvalOnce` が必要です。古いランタイムでは実行前に
`NotSupportedException` が発生します。実行ポリシーと JIT の選択は別々に設定します。

## typed-loop 統計

`NativeTypedLoopExecutions` は native typed-loop kernel の invocation を数えます。source-visible runtime failure を報告して終了する invocation も含まれます。これは execution observability の counter であり、success counter ではありません。

## ネイティブ JIT の能力とカウンター

`NativeLeafExecutions`、`NativeCfgExecutions`、`BridgeExecutions`、`LinearExecutions`、
`VmTrampolineExecutions` は直近の実行で通常の実行可能エントリへ実際に入った回数を表し、
エラーや制御転送による終了も含みます。対応する `*Entries` は生成済み成果物の数です。
ウォームキャッシュではコンパイルせずに実行でき、`GetJitCacheStats().CompileAttempts` は
保持キャッシュコンテキストの累積値を維持します。`ScalarNativeFastPathCalls` はホストの
ネイティブ呼び出し数です。直接呼び出しと型付きループカーネルは入れ子のイベントなので、
通常のエントリ数へ加算しないでください。対応する古いランタイムが提供しないカウンターは
ゼロになります。これらは集計統計であり、関数ごとの任意の JIT 診断はネイティブ Core/C++ API に属します。

`CostModelVmFallbacks` は、有効なコンパイル済み経路が VM より高コストと
予測されて選ばれた `Fallbacks` の部分集合です。コンパイル失敗でもスクリプト
意味論の変更でもありません。`NativeTypedLoopExecutions` は native backend の
typed-loop kernel invocation を数え、source-visible runtime failure を報告する
invocation も含みます。success counter ではありません。
`session.GetJitBackendInfo().CapabilityFlags` の `VeroScriptJitBackendCapabilities.NativeCode` と
`VeroScriptJitBackendCapabilities.NativeTypedLoops` を個別に確認してください。どちらも
`HostPlatform` やゼロ以外のカウンターから推測してはいけません。

## デプロイと runtime メタデータ

`VeroScript.CSharp` package を参照し、プロセスの OS と architecture に一致する
`VeroScriptInterop` native library を配置してください。managed package は .NET 8
と .NET 9 の asset を提供しますが、.NET runtime や native RID asset は
インストールしません。安全な loader directory にない場合は
`VEROSCRIPT_INTEROP_LIBRARY` に絶対パスを設定してください。相対パスは拒否されます。
最初の SDK 呼び出しの前にパスを設定してください。以降の呼び出しは選択済みのネイティブライブラリを使用します。

`VeroScriptRuntime` は `AbiVersion`、`RuntimeVersion`、`HostPlatform`、
`Capabilities`、`CurrentLanguage`、利用可能な language JSON を提供します。
互換性の判断にはこれらの値を使い、platform から意味を推測しないでください。
diagnostic code と arguments は表示言語を変えても安定しています。

`VeroScriptSession` は設定、ソース、パッケージ、ホストバインディング、値、診断を管理します。作成スレッドにアフィニティがあり、`Dispose` で終了します。

`VeroScriptConfiguration` の主なプロパティは `ImportPaths`、`ImplicitImportPaths`、`ScriptArguments`、`Defines`、`PredefinedMacros`、`StandardLibrarySearchPaths`、`SourceOverlays`、`BytecodeCacheDirectory`、`UseBytecodeCache`、`MaxCallDepth`、`MaxInstructions`、`TimeoutMilliseconds`、`EnableExecutionControl`、`CapabilityPolicyEnabled`、`AllowedScriptCapabilities`、`ExecutionBackend`、`AllowCircularImports`、`AllowProjectWideAccess` です。

`LoadSource`、`LoadFile`、`LoadPackage`、`Reload`、`ReloadPackage`、`BuildPackageFile`、`Clear` で内容を管理し、`Run`、`CallFunction`、`CallMethod`、`Construct`、`GetField`、`SetField`、`GetGlobal`、`SetGlobal` で実行・アクセスします。

`SetInput` は、次の実行で標準ライブラリの入力関数が使用するバッファーを設定します。

オプションの呼び出しの前には `HasFunction` と `HasMethod` を使用します。状態を保持するソースのリロードは、`Reload` の前に明示的に有効化します。

```CSharp
session.HotReloadEnabled = true;
var reloaded = session.Reload();
```

`Reload`、`ReloadPackage`、または置き換えを初期化する実行の後に、作成元スレッドで `GetReloadStatus()` を呼び出します。`Pending` は初期化待ち、`RolledBack` は前の世代への復帰、`AppliedWithErrors` はコミット後のエラーを表します。`CommittedGeneration` と `OperationId` で結果を確認できます。`ResetRequired` では `Clear` を呼び出し、プログラムを読み込み直します。クリアできない場合はセッションを再作成します。この照会は診断を保持し、`VeroScriptCapabilities.ReloadStatus` を必要とします。

バックエンドリゾルバーが `ApplyConfiguration` を拒否すると、ネイティブ側の理由を含む `VeroScriptException` が発生し、以前のキャンセル設定が保持されます。`HotReloadEnabled` の変更が拒否された場合も例外が発生します。`GetReloadStatus()` で状態を確認し、エラーに示された復旧手順に従ってください。 キャンセル要求が失敗した場合は、キャンセル操作自身のネイティブ状態を例外で報告します。実行制御が無効な場合、以前のネイティブエラーは作成元スレッドから引き続き確認できます。

`VeroScriptRunResult` は `Success`、`ExitRequested`、`ExitCode`、診断数を分離します。`GetDiagnostics`、`GetDiagnosticArguments`、`FormatDiagnostic` で構造化診断を扱ってください。`SetJitPolicy` と `GetLastJitStats` で hot threshold、試行数、キャッシュ、予算を管理します。統計の `DirectNativeCalls` は VM の値ブリッジを通らず別のネイティブエントリに到達した世代単位の JIT 呼び出し数です。セッションの再利用可能コードキャッシュはバイトコード キャッシュとは別です。`VeroScriptRuntime.Capabilities` に `SessionJitCache` がある場合、`SetJitCacheLimits`、`GetJitCacheLimits`、`GetJitCacheStats` で、条件を満たす実行間に保持するコードを管理します。`VeroScriptJitPolicy.ReuseSessionCode` は既定で `true` です。`MaxCompileAttempts` または `MaxCachedEntries` のゼロは対応する回数を無制限にしますが、保持するネイティブコードとペイロード全体のバイト上限は引き続き適用されます。再利用には VM 以外の有効なバックエンドと、ネイティブ関数バインディングの安定したリビジョンが必要です。バインディングやコールバックを変更するとキャッシュは無効になります。実行ガバナンスが有効でも、要件が一致するキャッシュコンテキストは再利用できます。既定の上限はネイティブコード 8 MiB、保持ペイロード全体 32 MiB で、ゼロは該当リソースを保持しません。統計は現在のキャッシュコンテキストの安定した累積値であり、ライブまたはプロセス全体のメモリカウンターではありません。ポリシー、上限、バックエンド、構成、モジュール世代、バインディング、ホットリロード/可観測性状態を変更するとコンテキストは無効になります。上限到達時は意味論を変えず安全にフォールバックします。最適化は意味論を変えません。

`MaxInstructions` と `TimeoutMilliseconds` は実行ごとの協調的な制限です。`0` は制限なしを意味します。VM と利用可能な制御付き JIT 経路では、同じ命令計数、実行制限、キャンセル、能力制約が適用されます。制限やキャンセル要求によって、実行中のネイティブまたはマネージドのホストコールバックが強制終了されることはありません。

実行ガバナンスが有効な `Mixed` は既定で VM を使います。対象の負荷を測定してから、`VeroScriptJitPolicy.EnableControlledMixed` で制御付き JIT を明示的に有効にしてください。明示的な `Jit` 選択はこのスイッチに依存しません。未対応またはコストモデルが VM を優先するエントリは、制限を保った VM にフォールバックします。バックエンドの `ControlledExecution` は対応能力を示し、`ControlledNativeExecutions` と `AotExecutions` は実際の制御付きネイティブ入口と AOT 入口の呼び出し数です。

`EnableExecutionControl` を有効にしたら `RequestCancellation()` を呼び出して現在の実行をキャンセル要求できます。これはスレッドセーフで、作成スレッド以外から呼び出せる唯一のセッション操作です。呼び出しが戻るまでセッションを有効に保ってください。キャンセルは実行セーフポイントで協調的に観測されます。
構成を正常に再適用するとセッションのキャンセルトークンがリセットされるため、キャンセル後に次の実行を準備できます。

`CapabilityPolicyEnabled` を有効にすると、標準ライブラリの副作用は `AllowedScriptCapabilities` のフラグに限定されます。対象は `FileRead`、`FileWrite`、`FileDelete`、`EnvironmentRead`、`EnvironmentWrite`、`ConsoleInput`、`ConsoleOutput`、`TaskSpawn`、`ProcessDirectoryChange` です。`chdir` には `EnvironmentWrite` と `ProcessDirectoryChange` の両方が必要です。このポリシーは組み込み関数だけを対象とし、ホストコールバックの認可はホストが管理します。無効時は既定で全て許可されます。

## 実行結果

`VeroScriptRunResult.Success` はコンパイルまたは実行が成功したかを示します。スクリプトが返す非ゼロ終了コードも成功として扱われます。コンパイルとスクリプト実行の失敗は通常 `Success == false` として返ります。ネイティブの所有権拒否も構造化診断付きの失敗結果を返すことがあるため、結果と `GetDiagnostics()` を確認してください。その他のネイティブ状態エラーは `VeroScriptException` を、引数、寿命、ABI 互換性のマネージド検証はそれぞれに対応するマネージド例外を発生させます。

## バックエンドと callback

`Vm` はインタープリターのみを使います。既定の `Mixed` は対応関数に JIT を使い、それ以外は VM にフォールバックします。`Jit` は先行コンパイルを要求しますが、VM へのフォールバックを禁止するモードではありません。デバッガー接続中は VM を使います。ホットリロードで保持する状態は VM で初期化し、対応する呼び出しは選択済みの JIT バックエンドで実行できます。

セッションは作成スレッドに束縛されます。script callback は VM worker thread から同時に呼ばれることがあるため、共有 managed state を同期してください。出力、診断、mutation callback の例外は内部で処理されます。host function、interceptor、generic factory、serializer の例外は callback failure に変換されます。

## 設定プロパティの概要

| プロパティ | 役割 |
| --- | --- |
| `ImportPaths`、`ImplicitImportPaths` | import の検索パスと暗黙の import パス。 |
| `ScriptArguments` | `main(string[] args)` に渡す引数。 |
| `Defines`、`PredefinedMacros` | macro と条件付きコンパイル定義。 |
| `StandardLibrarySearchPaths`、`SourceOverlays` | 標準ライブラリのルートと優先されるメモリーソース。 |
| `BytecodeCacheDirectory`、`UseBytecodeCache` | bytecode cache の場所と有効化。 |
| `MaxCallDepth` | 再帰深度の上限。0 は runtime の既定値。 |
| `MaxInstructions`、`TimeoutMilliseconds` | 実行ごとの協調的な上限。0 は無制限。 |
| `EnableExecutionControl` | 協調キャンセルを有効化。 |
| `CapabilityPolicyEnabled`、`AllowedScriptCapabilities` | 標準ライブラリの副作用ポリシーと許可能力。 |
| `ExecutionBackend` | `Vm`、`Mixed`、`Jit` の選択。 |
| `AllowCircularImports`、`AllowProjectWideAccess` | 循環 import とプロジェクト全体アクセスの規則。 |

## 独立した VM の選択

標準 Interop 製品はバイトコード VM を明示的に組み込みます。VM の選択は
`ExecutionBackend` や JIT の選択とは独立しています。待機中の作成スレッドで
`SetVmBackend(path)` を呼び、互換ライブラリのファイルまたはディレクトリを選択します。
null または空文字列は選択を解除し、置換失敗時は元の選択を維持します。
`GetVmBackendInfo()` は識別、バージョン、ターゲット、パス、VM 能力、選択・凍結状態を
コピーします。静的な選択のパスは空です。VM 能力のビットはバイトコード（1）、実行制御（2）、
デバッグ（4）、ホットリロード（8）、後始末（16）で、プロセス能力とは別です。

`VeroScriptCapabilities.VmBackendSelection` を確認してください。同じ ABI の旧ランタイムに
機能がなければ、任意エクスポートの呼び出し前に `NotSupportedException` になります。
実行が開始されると JIT やコンパイル拡張も VM ドメインを凍結します。`Clear` や再読み込みで
凍結は解除されないため、変更には新しいセッションが必要です。選択解除は製品 VM を復元せず、
インタープリターがない状態になります。コピーした情報は置換や破棄後も読み取れます。

## JIT の明示選択

実行ポリシーと backend 選択は別です。インストール済みモジュール環境を渡さない場合、Runtime の既定モジュールはありません。未選択 mixed は VM、強制 jit は実行前失敗です。所有スレッドで Session が待機中に `VeroScriptSession.SetJitBackend(path)` を使い、別途導入した互換ファイル/ディレクトリを選択します。`SetJitBackend(null)` / `SetJitBackend("")` は選択解除です。変更失敗で前の選択を保持します。`GetJitBackend()` / `GetJitBackendInfo()` は実際の識別/ネイティブ能力を示し、`backend-services` は Core サービス契約のみです。実行ガバナンスには上記の制御付き JIT ポリシーが適用されます。検索と ABI は JIT ガイドを参照してください。

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/ja-JP/index.md)

```CSharp
using var session = new VeroScriptSession();
session.SetJitBackend("/absolute/path/to/Jit");
session.LoadSource("int main(){ return 0; }");
var result = session.Run();
```

<a id="jit-backend-capabilities"></a>

選択した backend の機能は `session.GetJitBackendInfo().CapabilityFlags` と `VeroScriptJitBackendCapabilities` で確認します。`Capabilities` は未知のビットを含むネイティブのビットマスクを保持します。スナップショットは文字列を所有し、backend の交換や session の破棄後も読み取れます。未選択の場合は `None` です。`VeroScriptRuntime.Capabilities` は Core サービスを表し、選択した backend のネイティブ機能を表しません。

<a id="diagnostic-translation"></a>

`VeroScriptRuntime.Translate(englishMessage)` は、プロセス全体の現在の言語とカタログを使用して診断メッセージを翻訳します。埋め込み NUL を含む未知のメッセージは変更せずに返し、空の入力は空の文字列を返します。null 入力と不正な UTF-16 は拒否します。バインディングは `vs_interop_locale_translate` が返すモジュール所有の UTF-8 バッファをマネージド文字列にコピーし、成功時も失敗時も `vs_interop_buffer_destroy` で解放します。

## 保持された callable の破棄

<a id="callable-disposal"></a>

`VeroScriptCallable.Dispose()` は管理ラッパーを直ちに無効化します。呼び出し中なら、最後の有効な呼び出しが戻るまでネイティブ破棄を延期します。破棄はネイティブ実行の終了を待たないため、コールバックは別スレッドによるこのラッパーの破棄を待機できます。同じラッパーの呼び出しは引き続き直列化されます。この寿命規則は所有スレッドの検査や、セッションへのアクセスをホスト側で直列化する要件を変更しません。

## アノテーションサービスと診断の重大度

`VeroScriptRuntime.CompileCapabilities` は実行能力や JIT 能力とは独立してコンパイル時サービスを返します。`AnnotationProcessing` がある場合、ネイティブ拡張が提供する有効な不透明ハンドルを `SetAnnotationProcessingService(nint)` に渡せます。セッションがアイドル状態のときに作成スレッドで設定・解除し、`0` は解除を意味します。セッションは設定をコピーするため、設定成功後に呼び出し側のハンドルを解放できます。`ApplyConfiguration` はサービスを保持します。プロセッサーのコードとコールバックデータは最後の使用まで有効に保ってください。同じ ABI の旧ランタイムは `None` を返し、この操作で `NotSupportedException` を送出します。

`GetDiagnostics()` は `Severity` の `Error`、`Warning`、`Information`、`Hint` を保持します。診断数が非ゼロでもコンパイル失敗とは限りません。プロセッサーの警告・情報・ヒントは `Success == true` と共存できます。重大度の照会がない旧ランタイムの診断も読み取れ、`Error` として返ります。

## ホストのコンソールコールバック

`VeroScriptSessionOptions.ConsoleQuery` は `VeroScriptConsoleInfo` を返し、出力端末、色・カーソル・画面消去の対応状況、表示列数と行数を通知します。非正の寸法は不明を表します。制御には問い合わせと `ConsoleControl` の両方が必要です。`ConsoleControl` は `VeroScriptConsoleOperation` と整数二つを受け取ります。ANSI 前景色・背景色は `0..15`、既定色は `-1`、行消去 after/before/all は `0/1/2`、列・行はゼロ始まり、カーソル表示は第一引数の `0/1` です。未使用の引数はゼロです。

`ConsoleWrite` は `std::console::write` の正確な文字列を受け取り、埋め込み NUL を保持し、改行を追加しません。省略時は `Print` のバッファ出力を維持します。同期処理が成功した場合だけ `true` を返します。問い合わせの例外は能力なし、制御の例外は失敗を表し、書き込みの `false` または例外はスクリプト実行を失敗させます。

コールバックはセッション破棄の完了まで保持され、VM ワーカー上で並行実行される場合があるため、共有状態を同期してください。実行境界では失敗やキャンセル後もスタイルのリセットやカーソル表示が行われる場合があります。コールバック内でセッションを破棄したり通常のセッション操作へ再入したりしないでください。端末入力は出力能力とは独立しています。新しいコールバックには対応するネイティブランタイムが必要です。同じ ABI の旧ランタイムは行出力を維持し、末尾のコンソールコールバックを無視します。

C# SDK で読み込むスクリプトは、実行可能リフレクションと関数シグネチャで基本型エイリアスを使用できます。ネストした値コンテナーと参照ストレージも対象で、例えば `array<long>` を `array<int64>` として要求できます。符号、整数幅、オブジェクト型の識別、参照の読み取り専用性の検査は緩和されません。ソースとシリアル化されたパッケージは同じ Core 検査を使用します。

ホストプロトコルのコールバック（`Clone`、`Equals`、`Hash`、サブクラスファクトリー）は、プロトコルを保持する最後のネイティブ値が解放されるまで保持されます。セッション破棄後の呼び出しや別スレッドでの最終解放も可能です。破棄済みセッションへアクセスしないでください。マネージドコールバックにはオプションのネイティブ保持エクスポートが必要で、同じ ABI の旧ランタイムは登録前に `NotSupportedException` をスローします。登録後に保持が失敗すると、データ保護のためセッションを閉じます。ジェネリックファクトリーのロールバックは移譲済みまたはマネージド側の参照を一度だけ解放します。

## 汎用コンパイルサービスとモジュール環境

`SetCompileProcessingService(nint)` はネイティブ SDK のコンパイルパイプラインを接続し、`CompileCapabilities.GeneralProcessing` を必要とします。`0` で解除します。有効な不透明ハンドルを渡し、アイドル状態のセッションの作成スレッドで呼び出してください。セッションが設定をコピーした後、呼び出し側のハンドルを解放できます。通常の `ApplyConfiguration` はサービスを保持します。プロセッサーのコードとコールバックデータは最後の使用まで有効に保ってください。

`ApplyConfiguration(configuration, moduleEnvironment)` は不変のネイティブ SDK スナップショットを明示的に選択し、`Capabilities.ModuleEnvironment` を必要とします。`0` で解除し、引数が一つのオーバーロードは現在の選択を保持します。アイドル状態のセッションの作成スレッドで呼び出してください。成功後はセッションがスナップショットを保持するため、呼び出し側のハンドルを解放できます。変更前に `Clear()` を呼び出してください。環境所有のカタログへのローカル登録やカタログのインポートはできないため、ネイティブ側で統合スナップショットを準備します。ローカルバインディングの競合は設定やキャンセル状態を変更する前に拒否されます。プロバイダー、バインディング、プロセッサーのネイティブモジュールは最後の使用までロードしたままにしてください。未対応の旧ランタイムでは `NotSupportedException` が発生します。

## Session のバックエンド選択引数

`VeroScriptSession.SetVmBackend` / `SetJitBackend`

これらの Session セッターは、対応するファイル／ディレクトリ、または指定環境にリゾルバーがある場合はインストール済みバックエンド ID を受け取ります。`auto` は環境候補を要求し、VM では保持した製品の代替も使用できます。`none` またはネイティブの空文字列は選択を無効にし、`null` / `""` もその空要求になります。新規セッションでバックエンド指定を省略すると環境による選択が可能になり、明示的な解除とは異なります。既存セッションの再設定で VM 選択を省略した場合は、その実行ドメインを保持します。明示選択に失敗した場合は以前のバックエンドを保持します。作成スレッドの待機中に設定し、VM は実行前に変更してください。Clear/reload でも実行ドメインの固定は解除されません。JIT の選択と VM/Mixed/JIT 実行方針は独立です。

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

`VeroScriptRuntime.CompileCapabilities` は実行能力と別の `VeroScriptCompileCapabilities` を使います。同一 ABI の旧ランタイムに問い合わせ機能がなければ `VeroScriptCompileCapabilities.None` を返します。`GetJitBackendInfo().CapabilityFlags` は `DirectScalarCalls` などの `VeroScriptJitBackendCapabilities` を使い、バックエンドの対応を示すもので実際のネイティブ実行の証拠ではありません。各構造化診断は `VeroScriptDiagnostic.Severity` で分類します。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
