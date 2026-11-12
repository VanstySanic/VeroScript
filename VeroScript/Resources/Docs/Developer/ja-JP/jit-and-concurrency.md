# コア JIT サービスと並行処理

[交換可能な VM バックエンド](vm-backends.md)

<a id="native-abi-version"></a>

`VEROSCRIPT_VERSION_CODE` は製品バージョン `VEROSCRIPT_VERSION` を major:8/minor:8/patch:16 ビットでエンコードします。JIT モジュールのハンドシェイクと JIT/AOT の `CoreVersion` は同じ値を使い、C AOT 記述のフィールド名は `core_version` です。独立した JIT 契約や API の版番号はありません。バイナリ互換性は公開ヘッダーの指紋、記述構造の配置、ツールチェーン、プラットフォームで検証し、AOT は Core 実装の識別子も検証します。静的 C++ 利用側とバックエンドのバイナリは、一致するヘッダーとライブラリで再ビルドしてください。パッケージのリリースバージョンは独立したメタデータです。

## 所有権と選択

[交換可能な VM バックエンド](vm-backends.md#vm-provider-assembly) を参照してください。

[オプションの AsmJit モジュール](../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/ja-JP/index.md)が IR、lowering、ネイティブ生成、移植可能な Linear 実行、ABI アダプター、直接呼び出し先、実行 scratch を所有します。非公開ヘッダーとアーキテクチャマクロは Core SDK に含まれません。コアは `backend-services` を報告し、実際のターゲットとネイティブ能力は選択した工場の記述で取得します。

## コンパイルと呼び出し

`EJitCompilationResult` は `Ready`、`Unsupported`、`Deferred`、`Failed` を区別します。未対応と資源延期は VM に戻れますが、コンパイル失敗はエラーです。所有権、サービス、資源の検証後に公開します。プログラム ID はモジュールとネイティブバインドの世代を含みます。

`EJitInvocationResult` は `NotEntered`、`Returned`、`ControlTransfer`、`Failed` を区別します。最初から VM で実行できるのは `NotEntered` のみです。入った後のエラー、throw、exit、キャンセルは副作用を繰り返さず伝播します。コアが戻り値を検証します。実行成果物はコードとモジュール所有者を保持し、コンテキスト、フレームトークン、ビューは借用です。ネスト呼び出しやホスト再入後はオブジェクトとコンテナーのストレージを再取得します。

公開サービスは `VeroScriptJitRuntimeServices.h`、`VeroScriptJitFrameServices.h`、`VeroScriptJitValueServices.h`、`VeroScriptJitCallServices.h`、`VeroScriptJitScalarServices.h`。コピー、参照、const、メソッド分派、変更通知、エラー、論理呼び出し深度はコアの意味を保ちます。

`SetJitProgramOwner` の所有者は不変のモジュール世代を保持します。別の書き込み可能な別名から内容を変更せず、アイドル境界でモジュールを置き換えます。直接使う `VeroScriptVm` は設定済みのプログラム世代や所有者を自動変更しません。プログラム変更後も値を保持する場合、ホストがその識別子を更新し、解放または対応する移行まで定義元モジュールを保持します。Session の置き換えと再読み込みではこの識別子を管理します。

## ポリシーと Session 再利用

`FJitPolicy` は熱度、試行回数、キャッシュ判断、事前コンパイルを制御し、予算ゼロは無制限です。`Vm` はバックエンドを使わず、`Mixed` は熱度を待ち、`Jit` はポリシー内でコンパイルを優先します。実行管理とバックエンドの能力に応じて検証済みの制御付き JIT または VM を選びます。デバッグと未対応の経路では VM を使います。

`bReuseSessionCode` は既定で有効です。条件を満たす `Run`、`CallFunction`、`CallMethod`、保持 callable は同世代のキャッシュと累積試行予算を共有し、無制限予算でも再利用できます。工場の注入と非 VM モードが必要です。永続実行と安定したコールバックは再利用できます。ネイティブカタログは `IHostBindingCatalog::GetRevision()` で実行時の改訂を公開します。ポリシー、設定、Reload、Clear は状態を破棄し、失敗した Reload は元のプログラムを保ちます。

`FSessionJitCacheLimits` は既定で実行コード 8 MiB、保持ペイロード 32 MiB、scratch を含みます。ゼロは当該資源の保持禁止です。資源拒否では VM に戻れますが消費した試行回数は返しません。`FSessionJitCacheStats` は確定した世代の会計、`FJitStats` は直近の実行です。`NativeTypedLoopExecutions`、`DirectNativeCalls`、`CostModelVmFallbacks` は実際の経路を別々に数え、コストによる回退は実行エラーではありません。

## 並行処理と検証

コアはタスク、join、timeout、協調キャンセル、channel、mutex、semaphore、同期値を管理します。待機中の channel は close を観測します。worker は不変工場と所有プログラムを共有し、インスタンス、統計、scratch、エラーは独立です。終了時に言語 root を解放します。

契約、差分、寿命、デバッグ、並行回帰を検証します。モジュール有効、無効、ネイティブ生成のみ無効で構築し、バックエンドディレクトリのないソースで Core と C++ 消費者を構築します。性能基準を維持し、SDK の明示的注入を確認します。

<a id="construction-dispatch"></a>

構築中または破棄中の仮想呼び出し先は、Core の現在のライフサイクル段階で解決します。`bConstructionIncomplete` と `bDestroying` はこの確認を必要としますが、`GetTypeName` と `RuntimeTypeIndex` は引き続き実際の型とフィールド配置を表します。段階別のディスパッチを維持できない最適化は副作用の前にフォールバックし、通常の実型用仮想呼び出しキャッシュを再利用してはいけません。静的に束縛された非仮想呼び出しと、Core が対象を選択済みのネイティブコンストラクター入口は既存の経路を維持します。

## 実行診断

`PlanReason`/`PlannedKind` は snapshot 読取時の cache plan、`Fallbacks` は成功コンパイル前の warm-up を含む当該実行の過去の拒否です。詳細収集は公式 VM の計測 dispatch を使い、命令融合を減らして Core 境界を増やす場合があります。計時/境界数を無効時の性能とみなさず、集約入口数は詳細収集なしでも読めます。

`NativeLeafEntries`/`NativeCfgEntries` は生成 artifact、`NativeLeafExecutions`/`NativeCfgExecutions` はエラー終了も含む実際の入口です。controlled、typed loop、direct call、Linear/Bridge/VM trampoline は別カウンターです。`ScalarNativeFastPathCalls` は host scalar-native fast path のみを数えます。重複するイベントを独立した script call として合算しません。

実行前に `FJitPolicy::Diagnostics` を設定し、例として `MaxFunctions=128`、`MaxDepth=256`、必要なら `bMeasureTime` を選びます。実行後に `VeroScriptVm::GetJitDiagnostics()` または `VeroScriptSession::GetLastJitDiagnostics()` を読みます。世代内の関数 identity、module、実入口、結果、fallback histogram、任意の inclusive/self ナノ秒を記録します。Core call 境界だけが対象で inline 操作や直接 native 再帰の独立記録はありません。inclusive は重なり、self は記録された子だけを引きます。最外実行で reset し、再入/cleanup は同じ実行に属します。`ResetJitStats()` も消去します。

既定の `MaxFunctions=0` では記録 allocation・clock・logging はありません。上限は 4096 関数、深さ 32768、名前/理由ごと 4096 bytes、部分収集は `DroppedRecords` です。件数だけなら timing を切り、性能計測と診断 pass を分けます。

`GetFunctionReason()`/`GetFunctionFallbackReason()` は成功 VM plan も含むコンパイル決定と最新 runtime 拒否を示します。例外/ref/ownership 制約と cost-model は別で、未知の runtime 制約は `RuntimeGuard` です。`CostModelVmFallbacks` に必須 finally/ref fallback は含めません。finally は Core unwinding/有界 cleanup を維持します。有界 scalar 式は callback/governance なしで一つの単純な writable ref capture を更新できますが、alias/複数 capture/readonly/mutation-origin/probe 失敗は正規経路です。frame cache は関数・module・generation で保護し getter の順序/回数と binding 状態を維持します。

集約数は C Interop/C++/C#/Python、詳細 snapshot は Core/C++ に公開します。public layout/service 変更時は製品バージョンが同じでも Core/VM/JIT/Interop/native consumer を対応させて再ビルドします。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/jit-and-concurrency.md) · [fr-FR](../fr-FR/jit-and-concurrency.md) · [ja-JP](jit-and-concurrency.md) · [ko-KR](../ko-KR/jit-and-concurrency.md) · [zh-CN](../zh-CN/jit-and-concurrency.md) · [zh-TW](../zh-TW/jit-and-concurrency.md)
