# Python セッションと実行

セッションのクリアやホットリロードの無効化では、スクリプトのデストラクタや Python コールバックが実行される場合があります。コールバックの例外は直ちに `CallbackError` として報告され、すでに完了したクリーンアップや設定変更は有効なままです。

実行中の操作やコールバックから開いているセッションを閉じると `Error` が発生し、セッションは引き続き使用できます。外側の操作が戻ってから閉じてください。終了処理中は `closed` がすでに真であり、`close()` を再度呼んでも問題ありません。

## セッションの作成と結果

`Session(...)` はコンテキスト マネージャーとして使い、作成スレッドで
セッションを閉じてください。`load_source`、`load_file`、`load_package`、
`reload_package` は `RunResult` を返し、`run(RunMode.SCRIPT)` は `main` を
呼び出します。`RunMode.EVAL` はインクリメンタル評価を使用します。

結果には `success`、`exit_requested`、`exit_code`、`diagnostics` が含まれます。
ゼロ以外の終了コードでも実行成功になる場合があります。値を返す呼び出しが
失敗した場合は `VeroScriptError` が送出されるため、構造化された
`VeroScriptError.result` を確認してください。transport、型、thread、callback
のエラーにはモジュールの専用例外が使われます。

## 主な公開名

状態アクセスの正確な名前は `is_loaded`、`get_global(name)`、
`set_global(name, value)` です。`load_source`、`load_file`、`load_package`、
`reload_package` は `RunResult` を返し、`call`、`construct`、`call_method`、
`get_field`、`set_field` は失敗時に `VeroScriptError` を送出します。

`Session` はインポートパス、ホストバインディング、入力、診断、ライフサイクルを管理します。作成スレッドで使用し、`close()` またはコンテキストマネージャーで終了してください。

構成可能な主な項目は `backend`（`Backend.VM`、`Backend.MIXED`、`Backend.JIT`）、`import_paths`、`implicit_import_paths`、`script_args`、`defines`、`stdlib_search_paths`、`source_overlays`、`bytecode_cache_directory`、`use_bytecode_cache`、`max_call_depth`、`max_instructions`、`timeout_milliseconds`、`enable_execution_control`、`capability_policy_enabled`、`allowed_script_capabilities`、`allow_circular_imports`、`allow_project_wide_access`、`hot_reload_enabled`、`print_callback`、`diagnostic_callback` です。

`load_source`、`load_file`、`load_package` が内容をロードし、`reload` と `reload_package` が再ロードします。`run` はエントリーポイントを実行し、`call`、`construct`、`call_method`、`get_field`、`set_field`、`get_global`、`set_global` は構造化アクセスを行います。

`max_instructions` と `timeout_milliseconds` は実行ごとの協調的な制限です。`0` は制限なしを意味します。いずれかの制限、または `enable_execution_control=True` を指定すると、要求したバックエンドが `MIXED` や `JIT` でも VM のセーフポイント経路が使用されます。制限やキャンセル要求によって、実行中のホストコールバックが強制終了されることはありません。

`enable_execution_control=True` のときは、任意の Python スレッドから `request_cancellation()` を呼び出して現在の実行をキャンセル要求できます。要求が戻るまでセッションを有効に保ってください。キャンセル要求はそのセッションに残るため、次の実行を開始する前に新しい `Session` を作成してください。

`capability_policy_enabled=True` のとき、標準ライブラリの副作用は `allowed_script_capabilities` が許可した範囲に限定されます。対象はファイルの読み取り/書き込み/削除、環境の読み取り/書き込み、コンソール入出力、タスク生成、プロセスディレクトリ変更です。`chdir` には `ENVIRONMENT_WRITE` と `PROCESS_DIRECTORY_CHANGE` の両方が必要です。このポリシーは組み込み関数だけを対象とし、ホストコールバックの認可はホストが管理します。無効時はこれらの組み込み機能が既定で許可されます。

`RunResult` は成功、終了要求、終了コード、構造化診断を含みます。`set_jit_policy` では hot threshold、コンパイル試行数、キャッシュ、先行コンパイル予算を調整し、`jit_stats` でヒット、フォールバック、コンパイルを確認できます。`jit_stats` には `direct_native_calls` も含まれ、世代単位の JIT 呼び出しのうち VM の値ブリッジを通らず別のネイティブ エントリに到達した回数を示します。

`jit_stats` は直近の呼び出しの独立したスナップショットです。実際に実行された入口を
`native_leaf_executions`、`native_cfg_executions`、`bridge_executions`、
`linear_executions`、`vm_trampoline_executions`、`controlled_native_executions`、
`aot_executions` で数え、エラーや制御移動で終わった入口も含みます。コンパイル数と
キャッシュ内の入口数は別です。`direct_native_calls`、スカラー高速経路、型付きループは
入れ子のイベントであり、独立した関数入口として加算しません。VM 実行ではこれらの
実行入口カウンターはゼロです。コンパイル済みコードを再利用しても呼び出しごとに
リセットされ、`clear()` はセッションのスナップショットをリセットします。
`jit_cache_stats` は再利用コンテキストのコンパイルとリソースの累計値を保持します。

<a id="session-jit-cache"></a>

`JitCacheLimits` は条件を満たす通常の `Session.call`／ネイティブ `CallFunction` 間で保持するコードを管理し、バイトコードキャッシュとは別です。`JitPolicy.reuse_session_code` の既定値は `True` です。`max_compile_attempts` または `max_cached_entries` のゼロは、保持コンテキストでも回数無制限を意味します。再利用には互換 JIT ファクトリーと JIT を許可する有効な実行ポリシーが必要です。永続セッションや安定したコールバックではコードを再利用できます。ネイティブバインディングカタログには実行時のリビジョンが必要です。制御要件が一致する場合、制限付きネイティブ実行もコンテキストを再利用できます。

`max_native_bytes` は既定で 8 MiB、`max_retained_payload_bytes` は 32 MiB で、ゼロは該当リソースを保持しません。`session.set_jit_cache_limits(...)`、`session.jit_cache_limits`、`session.jit_cache_stats` で設定・確認します。統計はコンテキストの確定した累積値であり、ライブ値やプロセス全体のメモリ計測ではありません。ポリシー、上限、設定、モジュール、バックエンド、可観測なホスト境界の変更はコンテキストを無効化し、リソース上限では安全にフォールバックします。

制限付き `Backend.MIXED` は既定で VM を使います。`session.set_jit_policy(JitPolicy(enable_controlled_mixed=True))` で互換性のある制御付きネイティブ実行を選びます。明示的な `Backend.JIT` はこの設定と独立です。非対応のエントリは実行前にフォールバックし、デバッグ、状態を保持する初期化、上限付きの後処理には VM を使います。

`set_input` は入力を設定し、`set_hot_reload_enabled` はホットリロードを制御します。コンテナーはスナップショットであり、Python 側のリストや辞書の変更はスクリプトへ書き戻されません。可変セッションをスレッド間で共有しないでください。


任意の関数やメソッドを呼び出す前に、`has_function(name, argument_count)` と `has_method(instance, name, argument_count)` で存在を確認できます。グローバル変数の読み書きには、Core の実行契約に従って `hot_reload_enabled=True` による状態保持が必要です。

作成元スレッドで `session.reload_status` を読む前に、`CAPABILITIES & RuntimeCapabilities.RELOAD_STATUS` で対応を確認します。`ReloadStatus` スナップショットは `operation_id` でリロードを識別し、`current_generation` と `committed_generation` を区別します。`PENDING` は候補プログラムが初期化待ちであることを示します。`REJECTED` は以前のプログラムを保持し、`ROLLED_BACK` は復元の完了を示します。`APPLIED_WITH_ERRORS` はコミット後に実行または後処理が失敗したことを示します。`RESET_REQUIRED` は後続の実行を止めます。`clear()` でリセットできない場合はセッションを作成し直します。外部値をホストが保持している間は、以前の状態を保ったまま候補の準備を拒否することがあります。

## VM の選択

標準 Python パッケージには静的なバイトコード VM が含まれるため、通常のセッションで外部 VM ライブラリは不要です。`RuntimeCapabilities.VM_BACKEND_SELECTION` は選択 API の対応を示します。初回実行前に、セッションを作成したスレッドからアイドル時に `set_vm_backend(path)` を呼び、互換性のある VM ファイルまたはディレクトリを選択できます。`None` または空文字列は選択を解除します。置換に失敗しても以前の VM は維持されます。実行開始後は選択が固定され、`clear()` でも解除されません。変更には新しいセッションを作成してください。この選択は `Backend.VM`、`MIXED`、`JIT` の実行方針とは独立しています。

`get_vm_backend_info()` は `id`、`version`、`target`、`library_path`、`capabilities`、`selected`、`frozen` を持つ独立した `VmBackendInfo` スナップショットを返します。静的 VM の `library_path` は空であり、`selected` で VM 未選択と区別できます。置換後やセッション終了後もスナップショットは有効です。

## JIT の明示選択

実行ポリシーと backend 選択は別です。インストール済みモジュール環境を渡さない場合、Runtime の既定モジュールはありません。未選択 mixed は VM、強制 jit は実行前失敗です。所有スレッドで Session が待機中に `Session.set_jit_backend(path)` を使い、別途導入した互換ファイル/ディレクトリを選択します。`set_jit_backend(None)` / `set_jit_backend("")` は選択解除です。変更失敗で前の選択を保持します。`get_jit_backend()` / `get_jit_backend_info()` は実際の識別/ネイティブ能力を示し、`backend-services` は Core サービス契約のみです。永続セッションで条件を満たす呼び出しは、選択したバックエンドを使用できます。検索と ABI は JIT ガイドを参照してください。

[JIT](../../../../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/User/ja-JP/index.md)

```python
with Session(backend=Backend.MIXED) as session:
	session.set_jit_backend("/absolute/path/to/Jit")
	session.load_source("int main(){ return 0; }")
	result = session.run()
```

## 一回評価とバックエンド機能

通常のセッションでは、`run(RunMode.EVAL_ONCE)` は呼び出しごとにモジュールの初期化を一度実行し、`main` を呼び出しません。選択した JIT バックエンドによる初期化コードのコンパイルを許可します。`RunMode.EVAL` は増分評価を維持し、`RunMode.SCRIPT` は `main` も実行します。`RuntimeCapabilities.EVAL_ONCE` がこのモードの対応を示します。ホットリロードセッションは初期化済みのグローバル状態を保持し、状態を保持する初期化には VM を使用します。

`get_jit_backend_info()` は所有された `JitBackendInfo` スナップショットを返します。`capabilities` は `JitBackendCapabilities` の `NATIVE_CODE`、`PORTABLE_EXECUTION`、`NATIVE_TYPED_LOOPS`、`DIRECT_SCALAR_CALLS` で選択したモジュールを示します。`CAPABILITIES` は Core サービスを示します。スナップショットはバックエンドの置換や Session の終了後も有効です。

## コンパイルサービスと診断の重大度

`COMPILE_CAPABILITIES` と `CompileCapabilities.ANNOTATION_PROCESSING` は、`CAPABILITIES` および選択した JIT バックエンドとは独立したコンパイルサービスを示します。`Diagnostic.severity` は `DiagnosticSeverity` の `ERROR`、`WARNING`、`INFORMATION`、`HINT` です。操作の成功は `RunResult.success` で判断してください。診断一覧が空でなくても、非致命的なメッセージだけの場合があります。

`session.set_annotation_processing_service(handle)` は、セッションが待機中にネイティブ SDK 拡張が作成した有効な不透明ハンドルを接続します。セッションは設定をコピーするため、作成側は接続後に自分のハンドルを解放できます。サービスを解除するかセッションを閉じるまで、プロセッサのネイティブモジュールとコールバックデータを保持してください。`None` または `0` で解除します。任意の整数アドレスは有効なサービスハンドルではありません。`register_annotation(...)` は注釈の語彙を宣言し、コンパイル時プロセッサは登録しません。

## ホストの端末接続

`Session(console_query=..., console_control=..., console_write=...)` で端末コールバックを設定できます。照会は `ConsoleInfo`（寸法 0 は不明）を返します。制御は `ConsoleOperation` と二つの整数を受け取り、ANSI 色 0..15／既定 -1、行モード after/before/all=0/1/2、0 始まりの列／行、表示 0/1 を表します。同期成功なら `True`。テキストは改行を加えず `bool` を返します。省略時は行出力のバッファを維持します。終了処理までコールバックを保持し、ワーカーを同期してください。例外は `CallbackError`。端末操作列をタスク間で調整してください。

## 解決済みモジュール環境と汎用コンパイル処理

`Session(module_environment=handle)` は、対応するネイティブ SDK の `VeroScript::Interop::CreateModuleEnvironment` で作成した不透明なスナップショットハンドルを受け取ります。セッションは不変の環境を保持するため、構築後に生成側のハンドルを解放できます。環境はソースの所有範囲、正確なインポート、リソース、コンパイルサービス、実行依存関係を提供します。標準ライブラリが必要な場合は、その検索ルートと所有範囲も環境で指定してください。このモードでは Python は同梱の標準ライブラリルートを追加しません。スナップショットの変更には新しいセッションを作成します。環境がバインディングカタログを所有する場合、ローカル登録は拒否されるため、公開前にバインディングを統合してください。`None` または `0` は通常の設定を使用します。任意の整数アドレスは無効です。対応状況は `RuntimeCapabilities.MODULE_ENVIRONMENT` で確認できます。

`set_compile_processing_service(handle)` はネイティブ SDK の汎用処理サービスを接続します。注釈サービスと同じ待機状態、作成スレッド、寿命の規則に従います。`None` または `0` で解除します。機能ビットは `CompileCapabilities.GENERAL_PROCESSING` です。スクリプトのコンパイル拡張は `set_script_compile_extensions(...)` で設定します。

## クロージャーとローカル静的変数

スクリプトの関数値は初期化キャプチャーと `mutable` をサポートします。
`Value.clone()` と `retain_callable()` は既存のクロージャー環境を共有し、
Lambda 式を再評価すると独立した環境が作られます。参照キャプチャーの記憶域は
作成元の関数が戻った後も保持されます。セッションを閉じると保持した callable は無効になります。

ローカル `static` 変数は実行状態に属します。通常の `call()` は新しい状態を作成し、
`hot_reload_enabled=True` は呼び出し間で状態を保持します。セッションの終了時に
セッションが保持する静的状態が解放されます。独立した [Python AOT](aot.md) は
ソース Lambda とローカル `static` 宣言を拒否します。

## Session のバックエンド選択引数

`Session.set_vm_backend` / `set_jit_backend`

これらの Session セッターは、対応するファイル／ディレクトリ、または指定環境にリゾルバーがある場合はインストール済みバックエンド ID を受け取ります。`auto` は環境候補を要求し、VM では保持した製品の代替も使用できます。`none` またはネイティブの空文字列は選択を無効にし、`None` / `""` もその空要求になります。新規セッションでバックエンド指定を省略すると環境による選択が可能になり、明示的な解除とは異なります。既存セッションの再設定で VM 選択を省略した場合は、その実行ドメインを保持します。明示選択に失敗した場合は以前のバックエンドを保持します。作成スレッドの待機中に設定し、VM は実行前に変更してください。Clear/reload でも実行ドメインの固定は解除されません。JIT の選択と VM/Mixed/JIT 実行方針は独立です。

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/session.md) · [fr-FR](../fr-FR/session.md) · [ja-JP](session.md) · [ko-KR](../ko-KR/session.md) · [zh-CN](../zh-CN/session.md) · [zh-TW](../zh-TW/session.md)
