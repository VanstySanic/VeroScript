# ホスト統合

VeroScript は C++ ホストに埋め込むことができます。ホストは通常、ワンショット チェックまたは実行には `VeroScriptEngine` を使用し、存続期間の長いモジュール、バインディング、ランタイム状態、およびホット リロードには `VeroScriptSession` を使用します。

## VM の明示的な選択

Core は暗黙のインタープリタを提供しません。以下の例では別途配布される静的バイトコード VM を使用します。両方の SDK パッケージを検索して両方のターゲットにリンクし、エンジン設定または Session ごとに明示的に設定してください。リンクだけではバックエンドは選択されません。

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED COMPONENTS Runtime)
find_package(VeroScriptBytecodeVm 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE
	VeroScript::VeroScriptCpp VeroScript::VeroScriptBytecodeVm)
```

動的に読み込む場合は、配置した VM のファイルまたはディレクトリを `FEngineConfig::VmBackendPath` または `Session::SetVmBackendPath` に指定します。ファクトリとパスは同時に指定できません。実行開始後は選択が固定され、`Clear()` でも解除されません。`ScriptCompileExtensions` に独自のファクトリまたはパスがない場合、コンパイラスクリプトは選択した VM を継承します。コンパイラスクリプトを実行しない通常のコンパイルには VM は不要です。

既存 Session では、VM ファクトリーもパスもない `FEngineConfig` は現在の選択を保持します。凍結前に明示的に解除するには `bClearVmBackend=true` を指定し、ファクトリー／パス／解除を併用しません。`Clear()` は読み込んだプログラムをリセットしますが、実行後の VM ドメインは凍結したままです。その後の VM 変更には新しい Session を作成します。

## 高レベルのエンジン エントリ ポイント

SDK によって提供されるパブリック `VeroScriptEngine.h` ヘッダーを含めます。 `VeroScriptEngine` は、ソース バッファおよびファイル エントリ ポイントを提供します。

- `CheckSource` / `CheckFile` は、語彙、構文、および意味のチェックを実行します。
- `RunSource` / `RunFile` スクリプトをロードして実行します。

これらのエントリ ポイントは、インポート パス、スクリプト引数、定義、標準ライブラリ検索パス、バイトコード キャッシュ ディレクトリ、実行バックエンド、およびバインディング カタログを受け取ることができます。

`FEngineConfig` は、構造化された事前定義マクロ、暗黙的なインポート パス、
インメモリソースオーバーレイ、最大呼び出し深さ、循環インポート、
プロジェクト全体のアクセス スイッチと入力ストリーム。埋め込み `RunFile` のデフォルト
バイトコードキャッシュをオフにします。 `bUseBytecodeCache` を使用して明示的に有効にし、
該当する場合、`BytecodeCacheDirectory`。実行バックエンドは `Vm` です。
`Mixed`、または `Jit`、デフォルトは `Mixed` です。

バイトコードキャッシュには Core の実装と公開 API の識別情報が含まれます。
製品とバイトコードのバージョンが変わらなくても、Core のコード変更で既存のキャッシュは無効になります。
同じビルドの繰り返し実行ではキャッシュを再利用します。

`ExecutionLimits.MaxInstructions` と `ExecutionLimits.TimeoutMilliseconds` は実行ごとの協調的な制限で、`0` は無制限です。共有の `FExecutionControl` を `ExecutionControl` に設定し、別スレッドから `RequestCancel()` を呼べます。Core はインタープリタと対応するネイティブ経路に同じ命令数、取消、権限チェックを適用します。制御付き `Mixed` は既定で VM を使用し、Session の `SetJitPolicy` で `FJitPolicy::bEnableControlledMixed=true` を設定すると動的ネイティブ実行を有効にできます。明示的な `Jit` と選択した静的 Core AOT はこのスイッチに依存しません。必要な制御機能がないコードはネイティブ実行に入る前に VM へ戻ります。実行中のネイティブコールバックを強制停止することはありません。制御トークンは自動リセットされないため、再利用前に `Reset()` を呼んでください。

`Capabilities` は標準ライブラリの副作用を制御します。ファイルの読み取り/書き込み/削除、環境の読み取り/書き込み、コンソール入出力、タスク生成を指定できます。デフォルトは全て許可で、制限は組み込み関数だけに適用されます。登録したコールバックの認可はホストが管理します。
`bAllowProcessDirectoryChange` は `chdir` によるホストプロセス全体の作業ディレクトリ変更を個別に禁止します。`chdir` には環境書き込み権限も必要です。

エントリ ポイントは `FRunResult` を返します。 `bSuccess` は正常を報告します
コンパイル/実行。 `int main` または `exit(n)` が成功した場合でも、
ゼロ以外の `ExitCode`。 `bExitRequested` は明示的な `exit(n)` によってのみ設定されます。
`exit(0)` を含む。通常の `int main` リターンでは設定されません。 `Error` を読む
障害の詳細については `Diagnostics` を参照してください。

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptEngine.h"

#include <iostream>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	config.ScriptArgs = {"alpha", "beta"};

	const VeroScript::FRunResult result = VeroScript::VeroScriptEngine::RunFile(
		"main.vs",
		host,
		config);

	if (!result.bSuccess)
	{
		return result.ExitCode;
	}
	return result.ExitCode;
}
```

`ERunMode` はバックエンドから独立しています。`Eval` はインクリメンタル/REPL の
初期化パスを使用し、`EvalOnce` はソースを 1 回評価し、`Script` はロード後に
`main` を呼び出します。デバッグ、保持された初期化区間、有界クリーンアップは VM を使用し、永続セッションの通常関数は選択した JIT を使用できます。

## 組み込み VM デバッガー

`VeroScriptDebugger` は、`VeroScriptVm` を直接所有するホスト向けの同期
インプロセス デバッガーです。DAP とは別の API であり、ワイヤ接続を開いたり
JSON をエンコードしたりしません。`VeroScriptCompile.h`、
`VeroScriptDebugger.h`、`VeroScriptVm.h` をインクルードし、ソースまたはファイルを
`FBytecodeModule` にコンパイルして、所有権を移さないデバッガーを
`SetDebugger` で接続してからモジュールを実行します。

バイトコードインタープリターを使用する場合は `VeroScriptBytecodeVmAssembly.h` を
インクルードし、実行前に `VeroScript::BytecodeVm::Configure(vm)` を呼び出します。

`SetBreakpoint` のソース行は 1 始まりです。空のファイルラベルはすべてのファイルに
一致し、空でないラベルは完全なラベルまたはベース名に一致します。条件付きブレーク
ポイントの式は最上位フレームで評価されます。評価エラーまたは偽の条件では停止せず、
真の値、ゼロでない数値、その他の空でない結果は真として扱われます。

停止コールバックは VM が停止している間に同期的に呼び出されます。
`GetStackTrace`、`GetLocals`、`GetChildren` で状態を調べ、`Evaluate` で選択した
フレームの式を読み取り専用で評価できます。デバッガー変数を書き換える場合は
`SetLocal` または `SetChild` を使用します。コールバック内で `SetRunMode` を呼び、
`Continue`、`StepIn`、`StepOver`、`StepOut` のいずれかを選択してください。

`RequestPause()` はスレッドセーフな協調的要求です。VM のセーフポイントで検出され、
`EDebugStopReason::Pause` として通知されます。実行中のネイティブ コールバックを強制
終了するものではありません。前回の要求が残っている可能性があるデバッガーを再利用する
前に `ResetPauseRequest()` を呼び出してください。`SetDebugger` は所有権を移しません。
VM の実行中はデバッガーを有効に保ち、デバッガーを使うコールバックの間は VM も有効に
保ってください。接続中は実効バックエンドが `Vm` に固定され、JIT キャッシュが無効化
されます。

非同期のエディター統合には、[Tooling](../../../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/index.md) と
[デバッグ](../../../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/debugging.md) に記載されている `VeroScriptDebugSession` または
DAP アダプターを使用してください。

## 診断ローカリゼーション

エンジンのエントリ ポイントはコアのローカリゼーションを自動的に初期化します。
`VERO_LOCALIZATION_DIR` を優先し、[診断ローカリゼーション](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/localization.md) に
記載されたデプロイメントの検索順序を使用して、`VERO_LANG` またはシステム UI 言語から
カルチャを選択します。英語の診断を受け取るだけなら、ホストが追加の初期化呼び出しを
行う必要はありません。

明示的なカタログと言語制御の場合は、`VeroScript::Locale` を直接使用します。

```cpp
#include "VeroScriptLocale.h"

#include <iostream>
#include <string>

int main()
{
	std::string error;
	if (!VeroScript::Locale::LoadCatalogDirectory("localization", &error))
	{
		std::cerr << error << '\n';
		return 1;
	}

	if (!VeroScript::Locale::TrySetLanguageFromTag("zh-CN"))
	{
		return 2;
	}

	const std::string language = VeroScript::Locale::GetLanguageTag();
	std::cout << language << '\n';
	return 0;
}
```

`LoadCatalogDirectory` はプロセス全体のカタログ ツリーを置き換えます。ライブラリや
標準ライブラリのルートは `MergeCatalogDirectory` を使用して兄弟の
`localization/` ツリーを追加できます。既存のメッセージ ID は、上書きを要求しない限り
保持されます。コア言語とカタログの操作はスレッドセーフですが、`VeroScriptSession`
自体は内部的にスレッドセーフではありません。共有プロセスが 1 つのリクエストの途中で
表示言語を書き換えないよう、ホストは言語変更を許可する時点を定めてください。
メッセージ ID、プレースホルダー、英語へのフォールバック規則については
[診断ローカリゼーション](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/localization.md) を参照してください。

## 存続期間の長いセッション

`VeroScriptSession` は、複数の実行にわたるスクリプトを所有するエディター、ゲーム、またはホスト ツールを対象としています。セッションでは次のことができます。

- モジュールのロード、実行、再ロード、クリア。
- ネイティブ関数、ホストタイプ、フィールド、プロパティ、メソッド、演算子、および汎用ホストタイプテンプレートを登録します。
- 出力、診断、コールインターセプター、およびミューテーションコールバックをインストールします。
- VM、混合、または JIT 実行を選択し、バイトコード キャッシュを構成します。
- 協調的な命令/時間制限、キャンセル、標準ライブラリの機能を設定します。
- 状態を保持するホット リロードを明示的に選択します。

セッション呼び出しは内部的に同期されません。ホストは、`Load`、`Run`、`Reload`、および `Clear` に対して 1 つの制御スレッドを使用し、実行が非アクティブな間にリロードを要求する必要があります。

典型的な長時間セッションは次のとおりです。

```cpp
#include "VeroScriptBytecodeVmAssembly.h"
#include "VeroScriptSession.h"

#include <iostream>
#include <string>

int main()
{
	VeroScript::FStreamEngineHost host(std::cout, std::cerr);
	VeroScript::VeroScriptSession session(host);

	VeroScript::FEngineConfig config;
	VeroScript::BytecodeVm::Configure(config);
	config.ExecutionBackend = VeroScript::EExecutionBackend::Mixed;
	std::string configError;
	if (!session.ApplyConfig(config, &configError))
	{
		std::cerr << configError << '\n';
		return 1;
	}

	const VeroScript::FRunResult load = session.LoadFile("main.vs");
	if (!load.bSuccess)
	{
		return load.ExitCode;
	}

	const VeroScript::FRunResult run = session.Run();
	return run.ExitCode;
}
```

<a id="apply-config-result"></a>

`ApplyConfig` は設定の拒否時に `false` を返し、`OutError` で理由を受け取れます。ロードや実行の前に結果を確認してください。検証による拒否は設定の確定前に行われますが、メモリ確保やコールバックのコピーの例外は部分的な変更後に伝播する場合があり、ロールバックを保証しません。

`IEngineHost` はセッションを超えて存続し、`print` と診断ラインを受信する必要があります
コールバック。 `RegisterNative`、`RegisterHostType`、および
コンパイラが解決できるように、`Load` の前に `RegisterHostMember` を登録します。
ホスト宣言。レジストリは `Reload` および `Clear` 以降も存続します。それを最後まで読んでください
`GetBindingCatalog`、または `ExportBindingsToFile` を使用して IDE、LSP、および
DAP の消費者。

このセッションでは、`CompileFile`/`CommitCompiledFile` も公開されます。
`ReloadCompiledModule`、`LoadPackage`、および `ReloadPackage`。これらにより、編集者は、
パッケージ ローダーとホット アップデート システムは、コンパイルとコミットメントを分離します。
信頼境界を越える前に、パッケージの `CanonicalSignedBytes` を確認してください。

`BuildPackage`/`ReadPackage` と Session の `LoadPackage`/`ReloadPackage` は Core イメージ（`.vslib` または生のコンパイル済みアプリイメージ）を扱います。標準 ZIP `.vsapp` は外側のアプリコンテナーなので、Application サービスと `LoadApplication`/`ReloadApplication` を使用します。生の Core イメージを `.vsapp` という名前で保存しても標準コンテナーにはなりません。

## バインディング境界

スクリプトは、ホストが登録し、コンパイラが解決できるホスト関数またはメンバーのみを呼び出すことができます。バインディングの説明では、ホスト タイプ、コンテナ要素、またはキー/値タイプの情報とともに、パラメータと戻り情報を提供する必要があります。重複した名前、null コールバック、および組み込みとの競合は拒否されます。

ホスト構造体の値のコピーには、適切なクローン コールバックが必要です。ホスト オブジェクト ハンドルは、ホスト登録によって確立された有効期間と所有権のルールに従います。完全なバインディング API は、パブリック `VeroScriptCpp` ヘッダーによって定義されます。

パブリック コールおよび内線サーフェスには、次のものも含まれます。

- `CallFunction`、`CallMethod`、`CallMethodBypassingInterceptor`、`ConstructObject`、`GetField`/`SetField`、およびグローバル アクセス。
- 変更可能な `ref` 引数のライトバック。失敗または終了時にライトバックはありません。
- ネイティブ コールバックを超えてスクリプト関数値を保持するための `RetainCallable`/`InvokeCallable`。
- `SetInstanceCallInterceptor`、`SetMutationCallback`、および `ApplyClassDefaults`。
- `RegisterGenericHostTypeTemplate`/`InstantiateGenericHostType`、型エイリアス、外部シリアル化、およびオブジェクト参照列挙。
- `SetJitPolicy`, `SetJitCacheLimits`, `GetJitCacheStats`, `GetLastJitStats`: 再利用には選択済みファクトリー、VM 以外の有効ポリシー、`FJitPolicy::bReuseSessionCode` が必要です。対象の `Run`、`CallFunction`、`CallMethod`、保持した callable の呼び出しは世代キャッシュとコンパイル予算を共有します。`MaxCompileAttempts` または `MaxCachedEntries` のゼロは無制限を意味し、無効化ではありません。永続実行と安定したコールバックは再利用できます。ネイティブカタログは `IHostBindingCatalog::GetRevision()` で実行時の改訂を公開します。バックエンドが必要な実行制御機能を備えていれば、制御付きネイティブ実行でもコードを再利用でき、Session はその制御コンテキストを VM 間で保持します。構成、ポリシー、バックエンド、モジュール、バインディングの変更で保持コードを無効化し、リロード失敗時は以前のプログラムを保持します。既定値は実行コード 8 MiB、保持ペイロード 32 MiB です。`GetJitCacheStats` は確定した世代集計、`GetLastJitStats` は直近の呼び出しを示し、プロセス全体のメモリ計測ではありません。

`VeroScriptSession` は内部でスレッド同期を行いません。通常の操作と最終的な破棄は、セッションを作成したスレッドで行ってください。ホストのコールバックは VM のワーカースレッドで実行される場合があるため、外部状態へのアクセスを同期する必要があります。`RetainCallable` の既定値は `bOwnerThreadOnly = true` です。ホストがアクセスを直列化しても、別スレッドから保持した関数値を呼び出すと失敗します。別スレッドでの呼び出しを許可するには、`RetainCallable` の第 2 引数に明示的に `false` を渡し、実行、設定、リロード、破棄を含むすべてのセッション操作をホスト側で直列化してください。

スクリプトのクリーンアップも実行中の Session 操作です。プログラムの消去や置換、ホットリロードの無効化、保持した callable の解放、Session の破棄は、スクリプトのデストラクターやホストコールバックを呼び出す場合があります。これらのコールバックから同じ Session の実行や変更を行う API に再入してはいけません。クリーンアップ中の `RetainCallable` も新しいルートを拒否します。後続の操作はクリーンアップ終了後に行ってください。通常の実行コールバックから callable を保持することは引き続き可能です。

## ネイティブ JIT の可観測性

`FJitStats::CostModelVmFallbacks` は、有効なコンパイル済み経路がより遅いと
予測され、意図的に VM に残された `Fallbacks` の部分集合です。コンパイル失敗
でも意味論の変更でもありません。`FJitStats::NativeTypedLoopExecutions` は
native backend の typed-loop kernel invocation を数え、source-visible runtime
failure を報告する invocation も含みます。success counter ではありません。どちらも
直近の実行の観測値であり、ゼロでも target または capability の不在は証明しません。

<a id="threads-and-hot-reload"></a>

## スレッドとホットリロード

ホストは、VM の実行、セッション制御、ネイティブ コールバックの間のスレッド境界を調整する責任を負います。ホットリロードはオプトインです。関数の識別子とキャプチャの名前・型に互換性があれば、キャプチャした `this` を含むクロージャを移行できます。キャプチャの追加・削除・型変更、関数の再マッピング不能、アクティブなチャネル、互換性のない型変更は拒否されます。ホストが保持する callable ハンドルは、登録時の世代が正常に置き換えられると無効になりますが、有効なグローバル変数や値ルートと共有するグラフはクリーンアップで保持されます。ネイティブの副作用は引き続きスクリプト状態のトランザクションの対象外です。

状態保持ホット リロードが有効になっている場合、互換性のあるグローバル、オブジェクト、
コンテナの ID を移行でき、追加されたフィールドには言語のデフォルトが適用されます。
リロードはコミットと初期化の前にコンパイル、検証、移行を行い、失敗した場合は最後に
正常だったモジュールが引き続き使用できます。ホット リロードを無効にするか `Clear` を
呼び出すと、参照サイクルを含む保持された値が通常どおり解放されます。ホット リロードは
バイトコード キャッシュをスキップするため、`Reload` は現在のソースを再コンパイルします。

安定した多言語 C ABI については [Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md) を、ブラウザーと
WebAssembly のメモリ内エントリポイントについては [WebAssembly/WASM SDK](../../../../../../Platform/WebAssembly/Resources/Docs/User/ja-JP/index.md) を参照してください。

## 既存 Session のデバッグ

[既存 Session のデバッグ](session-debugging.md). `SetDebugBridge`, `ISessionDebugBridge`, `SetDebugBridge(nullptr)`.

## 保持バイト割当量

`MaxNativeBytes` または `MaxRetainedPayloadBytes` のゼロは該当領域の保持を許可しません。ポリシー予算のゼロ（無制限）とは異なります。

## JIT バックエンドの明示的な選択

インストール済みモジュール環境を渡さない場合、Core と C++ SDK は具体的な JIT 実装を自動選択しません。
`VeroScriptJitBackendLoader.h` の `LoadJitBackend` でファイルまたはディレクトリを
読み込み、共有ファクトリを `FEngineConfig::JitBackendFactory` に設定します。
Session は `SetJitBackendFactory` または `SetJitBackendPath` を使用できます。
空のパスはバックエンドを解除し、読み込み失敗時は現在のバックエンドとキャッシュを保持します。

ストリーム版 `RunSource` / `RunFile` のオーバーロードには、JIT ファクトリの後に VM ファクトリを渡せます。短いオーバーロードはインタープリターを提供しません。パス指定やモジュール環境の既定値には `FStreamEngineHost` と `FEngineConfig` を使い、[サービスバックエンドの選択](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection)に従います。`Vm` と `Mixed` の解釈実行部分には利用可能な VM ファクトリ、明示的な `Jit` には JIT ファクトリも必要です。設定済みホストは環境から解決するか、明示したファクトリを使用できます。デバッグと状態保持付きホットリロードには適切な VM が必要です。静的 Core AOT は動的 JIT ファクトリなしで `Mixed` を使用できますが、解釈実行する初期化やフォールバックには VM が必要です。

デバッグブリッジの設定で Session の保持済み JIT コードを解放します。ブリッジが
その後デバッガを返さなければ、選択済みファクトリで後続呼び出しを再コンパイルできます。
切断はスクリプトの取消や Session の破棄を行いません。Core と C++ の公開レイアウトが
変わったため、対応する SDK でネイティブ利用側を再ビルドしてください。

## 協調的なキャンセル

`ApplyConfig` または `SetExecutionControl` で共有 `FExecutionControl` を設定します。
プロトコルスレッドは `Session::RequestCancel()` を呼び出せます。所有者が実行していない間に
制御オブジェクトを交換する場合も安全です。呼び出しは現在の制御オブジェクトの共有スナップショットを
キャンセルします。null は何もせず、別の制御オブジェクトにはキャンセルを転送しません。
`GetExecutionControl()` は所有者スレッド専用の借用参照を返します。
キャンセルは協調的で、ネイティブコールバックを中断できません。再利用する前にトークンをリセットしてください。
Session を破棄する前に、キャンセル呼び出しと実行の終了を待ってください。デバッガーの切断は独立した操作です。

同じ `FExecutionControl` トークンを共有する Session はキャンセル状態も共有します。`Reset()` と再利用の前に以前のキャンセル呼び出しの完了を待ち、古い要求がそのトークンを再利用する次の実行をキャンセルしないようにします。

## C++ ホストでのメモリ管理

Core の `Memory::*` API には `VeroScriptMemory.h` を使用します。
`FEngineConfig::MemoryLimits` は設定付きの `RunSource` と `RunFile` の両方に適用されます。
`MaxBufferBytes` は所有バッファー単体の容量と外部領域の長さ、`MaxRetainedBytes` は所有領域と借用領域の共有バイト予算を制限します。どちらもゼロは無制限です。

Session は各実行 VM で同じメモリコンテキストを使用します。所有者スレッドで
`GetMemoryContext()`、`SetMemoryLimits()`、`GetMemoryStats()` を使用してください。
制限の変更は実行が停止している間に行います。引き下げが拒否されると以前の制限が維持され、
`ApplyConfig` のメモリ制限が拒否されると他の実行設定も維持されます。
ワーカーは予算を共有し、それぞれのスレッドドメインを使用します。ネイティブコールバックでは、借用した
`FNativeCallContext::MemoryServices` から現在のコンテキスト、作業チェック、バインディングカタログ、引数のアドレスを取得します。コールバック終了後に保持しないでください。
`Capabilities.bAllowMemoryAddress` はアドレスアクセスを制御します。生のアドレスはバイトの寿命を延長しません。独自のネイティブコールバックでアドレスを公開する前に `MemoryAddressesAllowed()` を確認してください。Core 関数を直接呼び出す場合の権限制御はホストの責任です。

`Clear` と再読み込みはコンテキストを維持します。互換性のあるホットリロードでは領域の同一性が維持され、初期化失敗時は以前のプログラムが復元されます。
Session の破棄ではスクリプトをクリーンアップしてからアクセスドメインを閉じます。
借用コンテキストを手動で閉じたり、Session 破棄後にその参照を保持したりしないでください。
取得したリースは、Session の破棄後も `Memory::ReleaseLease` までバイトを保持します。
リース解放はスレッドセーフですが、ホストは解放前にネイティブアクセスを終了または同期する必要があります。
外部領域の所有者と解放コールバックのデータは、すべての別名とリースより長く存続させ、解放コールバックから例外を送出しないでください。

SDK の `CppMinimal/memory.cpp` と Core の
[メモリリファレンス](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md) を参照してください。
独立 C++ AOT 出力では実行時メモリ操作と領域型を使用できず、生成時にエラーになります。未使用の `memory.vs` または `core.vs` のインポートは使用できます。

`RetainCallable` に `false` を渡しても、Session のメモリドメインは移転されません。
アクセスを直列化しても、領域操作には Session の所有者スレッドが必要です。他のスレッドで領域を操作する callable は `diag.memory_wrong_thread` で失敗します。スクリプトワーカーはそれぞれのメモリドメインを使用します。

## モジュールの公開マクロ

`Defines` と `PredefinedMacros` は各モジュールを個別に初期化します。Engine と Session はコアのモジュールローダーを使用します。明示的な import は `#define_export` マクロを取り込み、`export import` はそれらを再エクスポートします。通常の `#define` は非公開です。`ImplicitImportPaths` は宣言のみを取り込み、`SourceOverlays` も同じ規則に従います。[プリプロセッサーのリファレンス](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/preprocessor.md)を参照してください。

マクロの依存モジュールを変更したら、ファイル Session で `Reload()` を呼び、再実行して初期化を完了するか、`LoadSource()` で更新したソースを読み込みます。ホットリロードが有効な場合、`Reload()` は互換性のあるグローバル状態を移行します。`LoadSource()` はモジュールを置き換え、その状態をリセットします。公開マクロのインターフェイスによってバイトコードの構造が変わるため、配布する `.vslib`／`.vsapp` を現在のコアで再ビルドし、ネイティブ側も対応する Core と C++ SDK で再コンパイルしてください。

## コンパイル時のアノテーション処理

Engine と Session は `FEngineConfig::AnnotationProcessing` で設定します。Session は `SetAnnotationProcessing`／`GetAnnotationProcessing` も提供します。レジストリで `Freeze()` を呼び、その後は変更しません。ファクトリは宣言準備ごとに独立したプロセッサーを作成します。ネイティブライブラリとコールバックが参照するデータの寿命を保持してください。C++ AOT は `FCodegenOptions::AnnotationProcessing` で `FCppCodegenBackend` を設定します。

プロセッサーはコンパイルを行うスレッドで同期実行されます。準備コールバックから同じ Session を実行または変更する再入操作は拒否されます。キャンセルはコアの `FAnalysisCancelled` 例外を伝播します。キャンセルやコンパイル失敗では現在のプログラムを保持します。`FRunResult::Diagnostics` は Warning／Information／Hint を保持し、エラーストリームにも出力します。非致命的な診断によって成功が失敗になることはありません。`RunSource`／`RunFile` の結果にはコンパイルと実行の診断が含まれます。

生成に影響する外部入力は `ReadDependency` で読み取ります。キャッシュがヒットしても宣言準備を先に行い、プロセッサーの識別情報、設定、生成内容、依存入力をキーに含めます。再現できない処理には `Cacheable=false` を指定します。生成宣言を含むライブラリのインポートにも独自の予算が適用されます。Core の `VeroScriptAnnotationProcessing.h`、`VeroScriptProgramPreparation.h` と、ビルド可能な SDK サンプル `CppMinimal/main.cpp` を参照してください。

## ホストの端末接続

`IEngineHost` は `VeroScriptConsole.h` の型で `QueryConsole`／`ExecuteConsole` を実装できます。既定では未対応です。`HasConsoleTextOutput`／`WriteConsoleText` は改行を加えない出力、`FlushConsoleText` はフラッシュを提供します。`FStreamEngineHost` は接続先ストリームへ転送します。ホストはワーカー呼び出しを同期し、終了処理まで生存させます。VM は終了時にスタイルとカーソル表示を復元します。端末状態は共有されるため、タスク間で操作列を調整してください。

端末インターフェースには仮想メソッドが追加されています。更新時には同じビルドの Core／C++ SDK を使用してホストと関連するネイティブ拡張を再コンパイルしてください。JIT を使用する場合は対応するバックエンドも配備してください。

## 汎用コンパイル処理

Engine と Session は `FEngineConfig::CompileProcessing` を渡します。Session は `SetCompileProcessing`/`GetCompileProcessing` も提供します。C++ AOT は `FCodegenOptions::CompileProcessing` を使用します。コンパイル前に `FCompileProcessorRegistry` を凍結してください。各コンパイルは `RawSource`、`ActiveSource`、`Types`、`Members`、`Bodies` を通じて同じプロセッサーインスタンスを保持します。準備済みプログラムの AOT 出力は受理済み生成結果を再利用します。コールバックはコンパイルスレッドで同期実行され、同じ Session に再入できません。

生成入力には `ReadDependency` を使用し、非決定的なプロセッサーには `Cacheable=false` を設定してください。生成内容、依存関係、注釈定義、コメント保持方針、予算はキャッシュ識別に含まれます。キャッシュ読み込み前にも処理を実行します。置換の失敗やキャンセル時は使用中のプログラムを保持します。`CompileProcessing` と旧 `AnnotationProcessing` レジストリは Core のトランザクションと予算を共有します。旧注釈 API のみを使う場合、旧操作数・ソース予算の引き上げは未変更の汎用既定値にも反映されます。明示した汎用レジストリまたは予算が優先します。

注釈名は既定で大文字と小文字を区別します。ASCII の表記違いを許可するには、`RegisterAnnotation` の最後の引数を `false` にするか、`CompileProcessing.AnnotationDefinitions` の `FAnnotationDefinition::CaseSensitive=false` を設定します。Session は自身のバインディングレジストリ、不変モジュール環境は公開カタログを使用し、C++ AOT は `FCodegenOptions::Bindings` でカタログを受け取ります。この規則はプロセッサの購読と実行時リフレクションに適用され、`annotation_info.matches_name()` と `annotation_definition_info.case_sensitive()` で確認できます。ソースの表記は保持され、引数名とメタデータキーは完全一致のままです。`matches_name()` と `find_annotations()` は `@` を区別し、`has_annotation()` は省略も許可します。規則が重複する登録は拒否され、規則変更時はバイトコードキャッシュが無効になります。これらのリフレクション検索には Core ランタイム（VM、Mixed、またはホスト上の JIT）が必要で、単独の C++ AOT は明示的に拒否します。

`CommentRetention` はソースコメントの保持を制御します。VM は実行時の注釈・コメント検索に対応します。今回のソースメタデータ形式変更後は、対応する Core/C++ ライブラリでネイティブ利用側とバイナリスクリプトパッケージを再ビルドしてください。`VeroScriptCompileProcessing.h`、`VeroScriptSourceMetadata.h`、SDK の `CppMinimal/main.cpp` を参照してください。

Core/C++ SDK の更新時は新しいビルドディレクトリを使用するか、すべてのネイティブオブジェクトを再ビルドしてください。Windows のローカライズされた MSVC と Ninja では、CMake の構成とビルドの前に開発者コマンドプロンプトで `chcp 65001` を実行し、検出されるインクルード接頭辞とコンパイラー出力の文字コードを一致させてください。不一致の場合、Ninja がヘッダーファイルの変更を追跡できないことがあります。

## スクリプトによるコンパイル拡張

`FEngineConfig::ScriptCompileExtensions` でコンパイルスクリプトを設定します。Session は `SetScriptCompileExtensions`/`GetScriptCompileExtensions` を提供します。ロード前に `Roots`、任意の `RegistrationEntry`、`Configuration`、ソースオーバーレイ、標準ライブラリパスを設定してください。`import compiler "builder.vs";` は既定で有効です。`bAllowSourceImports=false` でこの入口を無効にできます。SDK と一致する `compiler.vs` を配布してください。

拡張はコンパイルスレッド上の独立した VM で動作し、各段階で状態を維持します。生成された業務コードは選択したバックエンドで動作します。業務バインディングは自動では公開されません。コンパイル専用の `Bindings` を明示し、コンパイル中は有効に保ってください。コンパイルコールバックは同じ Session を実行・変更できません。ネイティブコールバックの権限はホストが管理します。

拡張の `ExecutionControl` が空なら Engine/Session の実行トークンを継承します。Session では `SetExecutionControl` に追従します。明示したトークンは、最初に実行トークンと同一でも独立して保持されます。設定は次のコンパイルや再ロードに適用されます。拡張の `MaxInstructions`、`TimeoutMilliseconds`、パスと転送の制限は業務実行の制限とは別です。協調的キャンセルはネイティブコールバックを中断できません。

拡張の識別情報、設定、ソース、生成結果、宣言した依存関係でキャッシュを検証します。生成入力はコンパイラの依存サービスで読み取ってください。追加ネイティブバインディングは、決定性と安定した `BindingConfiguration` を明示した場合にのみ再利用を許可します。拡張のコンパイルや再ロードが失敗しても Session の実行中プログラムは維持されます。更新後は一致する Core/C++ SDK でネイティブ利用側とバイナリスクリプトパッケージを完全に再ビルドしてください。`CppMinimal` は Session と AOT の拡張経路を検証します。

`GetScriptCompileExtensions` は設定した内容を返し、継承する制御は空のまま保持します。設定を読み取り、他の項目を変更して再設定しても継承は維持されます。

## モジュール環境とネイティブ拡張

検査・ロード・実行前に、`FModuleEnvironment::Create` が返す不変スナップショットを `FEngineConfig::ModuleEnvironment` に設定します。Engine と Session はそのモジュール対応、依存範囲、リソース、コンパイル方針とネイティブ契約を使います。メモリモジュールは正規の `veroscript-module:/...` パスで `CheckFile`/`RunFile`/`LoadFile` に渡せます。import はスナップショットで許可され、インストールしても宣言は自動で import されません。

環境に `Bindings` がある場合、`GetBindingCatalog`、エクスポートと Session のデバッグビューはそのカタログを使います。公開前に同じ一時レジストリ（または `FNativeExtensionRegistry` の基底レジストリ）で binding を統合し、型 ID を維持してください。競合するローカル binding と後続の登録は拒否され、公開環境がカタログを所有する間 `GetBindingRegistry` は `std::logic_error` を送出します。カタログのない環境は Session のローカル登録を使えます。

プログラムをロードしたまま環境は置換できません。実行を終了し `Clear` を呼び、新しい設定を適用して再ロードします。検証で設定を拒否した場合は以前のプログラムと設定を保持し、コンパイル失敗時は使用中のプログラムを保持します。`ApplyConfig` の例外は[設定結果の契約](#apply-config-result)に従います。callback と外部へ返したネイティブオブジェクトは拡張コードを保持するため、ホストのリソース解放前にタスク、呼び出し可能ハンドルとオブジェクトを解放します。ネイティブライブラリは `VeroScriptNativeExtensionLoader.h` の明示 API でロードし、利用側とモジュールは一致する Core/C++ SDK と共有 CRT で再ビルドしてください。JIT のロードプロトコルは別のままです。

環境付きコンパイルは Core の準備処理を直接使い、Session/Engine の永続 bytecode キャッシュを使いません。純粋なスクリプトの AOT は対応しますが、実行時ネイティブパッケージ契約を必要とするプログラムは対象 runtime アダプターが提供されるまで拒否されます。

`GetModuleEnvironment` で現在のスナップショットを保持できます。デバッグビューの借用 `ModuleEnvironment` はメモリソースと provider 契約を公開し、明示 overlay が優先されます。これはコンパイル入力で、ロード済みメインソースの凍結コピーではありません。ホストは実際の `LoadSource` リビジョンを公開します。

## 厳密数値 API

SessionのSetNumericLimits/GetNumericLimitsはFNumericLimitsを設定します。VeroScriptNumerics.hの不変FBigInteger/FRationalと検査付きNumerics::Try*はFNumericOperationContext(Limits/Work)を使います。0は有限既定値で、work/取消/error/出力保持を明示します。独立sourceAOTは現状このCore厳密数値操作を拒否します。

## 中断後の上限付き終了処理

<a id="bounded-interruption-cleanup"></a>

命令数制限、タイムアウト、キャンセルにより通常の実行を終了します。Core は入っていたスコープの `finally` と必要なデストラクターを、別枠の **論理命令 16,384 個かつ 50 ms** の共有予算で処理します。同期の入れ子呼び出しと一時的な終了処理 VM はこの予算を共有します。元の上限やキャンセルトークンは終了処理を直ちに拒否せず、既存の権限は維持します。タスク作成、コンソール入力待機、`exit` による元エラーの置き換えは許可しません。

最初の中断原因を主エラーとして保持します。追加の失敗は `FRunResult.Diagnostics` と `VeroScriptVm::GetCleanupDiagnostics()` で取得できます。終了処理完了後は、ホストが制限を解除するかキャンセルトークンをリセットして再呼び出しできます。業務上の変更はロールバックしません。

未完了、捕捉されない終了処理エラー、予算枯渇では `VeroScriptVm::RequiresReset()` と `VeroScriptSession::RequiresReset()` が true になります。すでに実行中の finally やデストラクタ内で中断した場合、未完了の処理は再実行せず、リセットを要求します。その後の実行と保持状態操作は `diag.execution_state_reset_required` で失敗します。`Session.Clear()` 後、新しいプログラムを読み込んでください。Clear は利用不能状態の未完了スクリプト終了処理を打ち切り、ネイティブ資源を解放しますが、ホストのキャンセルトークンはリセットしません。単独 VM と保持した Core 状態も破棄して再作成してください。期限は協調方式であり、ブロック中のホストコールバックを強制停止できません。VM、Mixed、JIT、対応する AOT は同じ Core 契約を使用します。 失敗したリロードの復元が未完了、または外部ルートが `Clear()` を妨げる場合、Clear 後に `RequiresReset()` と `IsLoaded()` を確認してください。リセットできなければ Session を再作成し、利用不能なインスタンスで実行を再開しません。

## Session のバックエンド選択引数

`VeroScriptSession::SetVmBackendPath` / `SetJitBackendPath`

これらの Session セッターは、対応するファイル／ディレクトリ、または指定環境にリゾルバーがある場合はインストール済みバックエンド ID を受け取ります。`auto` は環境候補を要求し、VM では保持した製品の代替も使用できます。`none` またはネイティブの空文字列は選択を無効にし、`""` もその空要求になります。新規セッションでバックエンド指定を省略すると環境による選択が可能になり、明示的な解除とは異なります。既存セッションの再設定で VM 選択を省略した場合は、その実行ドメインを保持します。明示選択に失敗した場合は以前のバックエンドを保持します。作成スレッドの待機中に設定し、VM は実行前に変更してください。Clear/reload でも実行ドメインの固定は解除されません。JIT の選択と VM/Mixed/JIT 実行方針は独立です。

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

`BytecodeVm::Configure(FEngineConfig&)` は `FallbackVmBackendFactory` を設定し、明示的な `VmBackendFactory`／`VmBackendPath` が優先されます。Session はこのフォールバックを保持するため、実行前に `SetVmBackendPath("auto")` で復元できます。

## 永続実行とリロードの確定

永続 Session の通常関数は選択した Mixed/JIT バックエンドを使用できます。保持された初期化区間、デバッグ、有界クリーンアップは VM を使用します。未対応のネイティブ操作は実行前にフォールバックし、命令数、時間、取消、権限、呼び出し深度の規則は維持されます。

対応するインターセプト済みメソッド本体もバックエンド能力に基づき実行します。`Proceed` 内のネスト呼び出し、参照書き戻し、実行制御を保持します。ホストが位置を指定しないインターセプターエラーは呼び出し箇所を示し、明示位置と本体の元エラーは保持します。

`Proceed` の元の結果を直接渡す場合、構造体、ネイティブ値型、固定長配列の値戻りのコピー境界を維持し、コピーコンストラクターを重複実行しません。置き換えた結果や借用した結果には通常の型検査と値コピーを行います。参照を返すと宣言されたメソッドは参照セマンティクスを維持します。

安定したネイティブバインディングとコールバックは世代コードを再利用できます。`IHostBindingCatalog::GetRevision()` は実行時の改訂を公開します。ゼロを返すカスタムカタログにネイティブコールバックがある場合、再利用しません。`FHostBindingRegistry` は改訂を自動更新します。外部変更がコードや所有権の寿命に影響するときは `InvalidateRuntimeBindings()` を呼びます。コールバックが読む通常の可変データには改訂が不要です。実行中はカタログを変更せず、Session の設定 API でコールバックを変更してください。

`RetainMigratableValue(value, root, error)` はホストの値グラフをグローバル状態と同じ移行グラフに明示的に登録します。オブジェクト、コンテナ、別名、循環、クロージャーの互換規則を共有します。置換初期化の確定待ちは `root.Read()` が null を返し、コミットまたは完了したロールバック後に読めます。待機中はコピーした生ハンドルへアクセスしないでください。生ハンドルは自動再配置されません。通常のルートは置換を阻止し、世代ルートは失効します。Load/Clear の前には移行ルートを解放します。 ソースのリロードには `SetHotReloadEnabled(true)` が必要です。トランザクション型のパッケージ／アプリのリロードもこの移行サービスを使います。

所有スレッドで `GetReloadStatus()` を読みます。`OperationId` は `PreviousGeneration`、`CandidateGeneration`、`CurrentGeneration`、`CommittedGeneration` と独立しています。ロールバック後は候補世代番号を再利用できます。`Rejected` は未適用、`Pending` はステージ済み、`RolledBack` は復元完了、`Applied` は初期化コミット済み、`AppliedWithErrors` はその後の業務実行または退役値のクリーンアップ失敗です。`ResetRequired` は `RequiresReset()` と一致し、以降の実行を禁止します。復元や清掃を完了できない場合は Session を作り直します。直近の受理済みリロード要求の状態を返し、ネイティブホストの副作用はロールバックしません。

Core、C++ 埋め込み、Application は `PathUtil::FromUtf8` / `ToUtf8` により C++17/C++20 の UTF-8 パスを扱います。`char8_t` を無効化する必要はありません。製品バージョンが同じでも、バックエンドとネイティブモジュールは対応する Core 実装で再ビルドします。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/embedding.md) · [fr-FR](../fr-FR/embedding.md) · [ja-JP](embedding.md) · [ko-KR](../ko-KR/embedding.md) · [zh-CN](../zh-CN/embedding.md) · [zh-TW](../zh-TW/embedding.md)
