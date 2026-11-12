# Interop ABI と lifetime

`VeroScriptInterop` は C11-compatible process ABI です。public header は session、value、callable、package、buffer、call context、interception、generic host type、external serialization の opaque handle を定義します。

## VM バックエンドの選択

`VS_INTEROP_CAP_VM_BACKEND_SELECTION` は `vs_interop_session_set_vm_backend` と `vs_interop_session_get_vm_backend_info` の対応を示します。標準 Interop 製品は静的バイトコード VM を明示的に組み込みます。独自ビルドでは `VEROSCRIPT_INTEROP_DEFAULT_VM=OFF` を設定し、Core と一致する 外部 VM を選択できます。選択は VM/Mixed/JIT ポリシーから独立しています。実行前、セッションがアイドルの 作成元スレッドで設定してください。空パスは選択を解除し、失敗時は元の選択を保持します。Core が実行を 確定すると JIT の場合も選択は固定され、`vs_interop_session_clear` でも解除されません。

出力の `struct_size` を初期化してください。問い合わせ失敗時は出力を保持します。`VS_INTEROP_VM_SELECTED` と `VS_INTEROP_VM_FROZEN` はドメインの状態、`VS_INTEROP_VM_CAP_*` は能力を示します。文字列ビューは選択の置換またはセッション破棄まで有効で、静的選択では `library_path` が空です。ApplyConfig は選択を保持し、スクリプトコンパイル拡張はセッションの VM を継承します。

## version と status

ABI version、runtime semantic version、capability bit、execution backend、run mode、package kind、value kind、status code は別 field です。ABI query function は個別に返します。binding が ABI value を product version のように表示または比較してはいけません。

現在の `VS_INTEROP_ABI_VERSION` は `0x00000101` (0.1.1) で、`(major << 16) | (minor << 8) | patch` としてエンコードします。メジャーに 16 ビット、マイナーとパッチに各 8 ビットを使用します。C# と CPython のアダプターは完全一致を要求します。能力ビットや `struct_size` に従う末尾フィールドの追加は、このバージョン検査を省略する根拠にはなりません。ABI 変更は製品ごとのパッケージバージョンとは独立して審査してください。

各 operation は該当時 ABI status を返します。last-error buffer は operation-level failure 用です。structured diagnostic は session diagnostic API で読み、presentation boundary でのみ format します。

## 所有権

opaque handle は function により explicit owner または borrowed reference です。string/byte view は non-owning です。ABI が返す buffer は対応する buffer function で destroy するか owner release 前に copy します。callback context と borrowed enumeration value は documented call 中だけ valid です。

C header は C と C++ の両方から compile/link されるため calling convention、struct layout、enum value、nullability、symbol name は ABI の一部です。additive function でも header、implementation、test、adapter の同期が必要です。

## セッションの JIT キャッシュ拡張

`VS_INTEROP_CAP_SESSION_JIT_CACHE` は session cache limit/statistic export を gate します。この extension は `vs_interop_jit_policy` に `reuse_session_code` を append します。original 32-byte layout 向け caller は valid のままで reuse enabled を default で受けます。current caller は full structure を送り byte を zero にできます。明示的 opt-out を許しつつ ABI 0.1.1 を保持します。

再利用は対応する通常の Run、EvalOnce、関数呼び出しに適用され、コンパイル状態を保持します。VM のグローバルやスクリプトオブジェクトは保持しません。永続セッションでも、互換バックエンドと安定したバインディングのリビジョンがあれば再利用できます。インターセプターや変更コールバックを変更すると保持コードは無効になります。実効バックエンドが VM の場合はこのキャッシュを使いません。制御付き実行は互換キャッシュと制御識別情報を保持し、累積コンパイル予算を共有できます。試行回数・キャッシュ項目予算がゼロなら計数は無制限ですが、保持メモリ上限は有効です。`vs_interop_session_set_jit_cache_limits` はコンテキストを無効化します。既定上限は 8 MiB/32 MiB、ゼロは該当リソースの保持を禁止します。制御付き Mixed は既定で VM を使い、測定後に `enable_controlled_mixed` で有効化します。明示的な JIT 選択は独立です。バックエンドの `VS_INTEROP_JIT_CAP_CONTROLLED_EXECUTION` と実行統計 `controlled_native_executions` を確認し、未対応または VM 優先の入口では制御付き VM 経路を維持します。

`vs_interop_session_get_jit_cache_stats` は current context の cumulative compile attempt、settled retained byte、resource-limit latch を返します。`vs_interop_session_get_last_jit_stats` の last-run delta とは別で process peak-memory limit ではありません。policy、limit、backend、module、host observability の変更は context/statistic を reset できます。

## 新しい context と channel 契約

channel operation は value handle contract を使い、value を borrowed callback object に変えません。blocking/timed/non-blocking は別 symbol/result semantics を持ち、`close` は waiter の synchronization point です。host は無関係な `VeroScriptStatus` から timeout/closed を推測してはいけません。

generic host-type、call、interception、external serialization context pointer は callback 中だけ borrowed です。accessor、mutator、error setter、continuation/invocation function は callback active 中のみ valid です。return 後に context pointer、argument/field/byte view を retain できません。callback exception/host failure は return 前に context error channel へ変換します。

callable handle には explicit retain、validity、invoke、destroy operation があります。owner-thread-only invocation と weak-`this` capture は別 option で session を一般 thread-safe にしません。callback 後に必要な external serializer byte/reference visitor value は copy します。この rule は C、C++、managed adapter 全てに適用されます。


<a id="localization-abi"></a>

## ローカライズ ABI

`vs_interop_locale_translate` は通常の初期化とプロセス言語に従い、モジュール所有のバッファーを確保します。不明なバイトと NUL を保持し、無効入力では出力をクリアします。すべての経路でバッファーを解放してください。C ヘッダーと `.def` エクスポートを揃え、ネイティブ検証と六言語の置換を確認します。メモリ確保失敗時は英語フォールバックが残る場合があります。

## 公開前の検証

単独および完全リリースの Interop パッケージ作成では、ステージング済み SDK を使って
C11 ヘッダーテストをコンパイル・実行し、すべての公開関数をリンクします。エクスポートや
インポートライブラリの項目が欠けるとパッケージ作成は失敗します。外部サンプルでは
`VEROSCRIPT_SDK_ABI_TEST_SOURCE` でこの検証を有効にします。Linux 共有ライブラリの
テストは、モジュール未ロード時のリフレクションエラー経路にメモリ確保失敗を注入し、
例外が C ABI を越えず `OUT_OF_MEMORY` が返ることも確認します。通常の未ロード状態では
`ENGINE_ERROR` を返します。

境界テストは最初の version query の割り当て失敗（空の view、再試行の成功）、
serializer の置換失敗（以前のバイトを保持）、割り当てなしの multiset 計数、
memory guard の元の失敗 status も検証します。拡張テストは session の最終破棄時に
script destructor から native、interception、mutation callback を実行します。
host の callback data は破棄完了まで生存させてください。

UTF-16 view の割り当て失敗は出力引数を変更せず `OUT_OF_MEMORY` を返します。
再試行ではすべてのコード単位を保持し、以後の読み取りはキャッシュを再利用します。

Console callback は C ABI version を変更せず session-options の末尾に追加されます。
完全な末尾フィールドだけを読み、旧 options も有効です。query/control は端末操作を
記述し、任意の write callback は改行を追加せず元の UTF-8 バイトを受け取ります。
既存の callback lifetime と worker thread の規則を適用します。

マクロの回帰テストは公開 C ABI で、オブジェクト形式・関数形式・可変引数マクロを
再エクスポートするライブラリを構築、解析、インポートして実行します。デバッグ情報の
保持と削除の両方で、構築時に確定する条件付きエクスポート、呼び出し側での展開、
非公開マクロの分離、インポートエラーの物理位置、現在の `VSBC` リーダーによる
`VRS\0` / `VRS\1` / `VRS\2` / `VRS\3` / `VRS\4` の拒否を確認します。

インストール済みネイティブ注釈 SDK のテストは凍結レジストリー、独立した retain/release、
ApplyConfig での保持、作成スレッド制限、プロセッサーと解放コールバックからの設定再入を
確認します。メソッドを生成して呼び出し、4 種類の診断重大度と主となるエラー、サービス
消去後の生成無効化を検証します。ネイティブ作成関数には一致する Core C++ SDK が必要です。
追加の 5 個の C エクスポートは ABI 0.1.1 と既存の診断レイアウトを保ちます。

テストは ApplyConfig 中のサービス関数オブジェクトのコピーと破棄も検証します。
設定の再入は拒否され、コピー時のメモリ確保失敗は `OUT_OF_MEMORY` を返し、
インストール済みサービスを保持します。再試行の成功で古いエラーを消去します。

回帰テストはコピー・破棄コールバックからの通常の関数呼び出し、実行、
ソースの置き換えを拒否し、ロード済みプログラムを保持することも検証します。
設定記述子はサービス関数オブジェクトのコピー前に検証するため、ライフサイクル
コールバックが元の検証エラーを上書きしません。ネイティブコールバックの
コンテキスト経由の呼び出しは、対応する入れ子のスクリプト実行に引き続き使えます。

追加の回帰テストは古い成功・終了フラグで結果を初期化し、再入の拒否後に
新しい失敗結果とセッション診断を確認します。NULL の結果出力も対象です。
期限切れ callable は戻り値を変更せず失敗します。同期破棄では最終コールバックが
完了するまでハンドルを保持し、セッションを先に閉じるため、API アクセスと再度の
破棄を安全に拒否します。destroy が戻った後のハンドルは無効です。実行中の呼び出しが
破棄を遅延させる場合、コールバックデータはその呼び出しが戻るまで保持してください。

`vs_interop_session_retain_callback_user_data` は成功時のみホストプロトコルの
データ所有権を移します。最後のレジストリ／値の所有参照が解放された後、一度だけ
解放コールバックを呼びます。セッション終了後や別スレッドの場合もあります。
同じデータを使う後続の通常・ジェネリックプロトコルも所有者を共有します。
保持した値のコピー、別スレッドでの最終解放、メモリ確保失敗後の登録再試行を検証します。

## 汎用処理とスクリプトによるコンパイル処理

`VS_INTEROP_COMPILE_CAP_GENERAL_PROCESSING` は `VeroScriptInteropCompileProcessing.h` のネイティブサービスを示します。凍結済み `FCompileProcessorRegistry` と `CreateCompileProcessingService` で作成し、retain/release は独立した opaque handle を扱います。`vs_interop_session_set_compile_processing_service` で設定、NULL で解除し、ApplyConfig は保持します。対応する Core/Interop ネイティブ SDK を使用し、最終解放までコードとデータを保持してください。設定は作成スレッドの非実行時のみ可能で、処理・サービスのコピー／解放中の再入は拒否します。

`VS_INTEROP_COMPILE_CAP_SCRIPT_EXTENSIONS` は `vs_interop_session_set_script_compile_options` を示します。現在の完全な `struct_size` を渡し、ルート、任意の登録入口、設定、許可ディレクトリはコピーされます。パス／入口名の NUL、重複設定キー、不正なポインター／件数、予約バイト、bool は受理済み設定を変更しません。上限はルート64件、設定／許可ディレクトリ各4096件です。空の入口名は `register_extension` を選択します。

NULL は Core の既定値（設定ルートなし、ソース import 許可、合計1,000,000命令、2,000 ms）を復元します。明示した制限0は無制限です。ソース import を無効にしても設定ルートは使用できます。overlay、import、標準ライブラリパスは通常の設定に従い、標準ソースを import する拡張には `compiler.vs` を含む対応ディレクトリが必要です。拡張は隔離 VM で動作し、セッションのキャンセルを継承、ApplyConfig は設定を保持します。業務 binding は暗黙に公開されません。生成パッケージは生成元拡張を実行せず、JIT は生成された業務コードを実行します。

C 回帰はフェーズ間状態、入力コピー、生成／診断、不正入力時の維持、予算・キャンセル失敗と復旧、import 方針、パッケージ実行を検証します。インストール済みネイティブ利用者は寿命、設定保持、処理／コピー／解放時の再入を検証します。

## 解決済みモジュール環境

`VS_INTEROP_CAP_MODULE_ENVIRONMENT` は環境ブリッジを示します。対応するネイティブ Core/Interop SDK で不変の `FModuleEnvironment` を作成し、`VeroScriptInteropModuleEnvironment.h` の `CreateModuleEnvironment` でラップします。C 側は不透明ハンドルのみを渡します。`vs_interop_module_environment_retain` は独立して所有する別のハンドルを作成し、各ハンドルを一度解放します。設定済みセッションはスナップショット、リソース、ネイティブコード所有者、依存契約を独立して保持します。

`vs_interop_session_apply_config_with_environment(session, config, environment)` は完全な設定を適用してスナップショットを選択し、NULL は選択を解除します。通常の `vs_interop_session_apply_config` は現在の環境を保持します。どちらも作成スレッドとアイドル状態が必要です。環境変更前にロード済みプログラムを Clear してください。ローカルバインディングとの競合は設定やキャンセル制御を変更する前に `ENGINE_ERROR` を返します。環境所有のカタログへのローカル変更も `ENGINE_ERROR` で拒否されるため、ネイティブ側で結合済みの不変カタログを準備します。カタログのない環境では通常の C 登録を使用できます。

ロード、実行、パッケージ、外部 JIT は、メモリ/ファイルのモジュールマッピング、コンパイルリソース、ローカライズ、厳密な外部依存チェックについて Core の環境契約に従います。このブリッジは解決済み環境を受け取り、パッケージのインストールやネイティブ拡張の探索は行いません。公開後のコールバックは不変にし、対応するネイティブ SDK バイナリを使用してください。設定時やセッションの最終破棄時に所有者が解放される場合があります。デストラクタは例外を送出せず、設定や実行への再入は拒否されます。Core の設定適用中に割り当て失敗または内部エラーが起きた場合は、設定を利用する前に完全な設定を再適用してください。

バックエンドの解決が拒否されると、設定やキャンセル状態の確定前に `ENGINE_ERROR` を返します。有効な環境で再試行してください。ホットリロード設定が拒否された場合も `ENGINE_ERROR` を返します。上限付きロールバックが `RESET_REQUIRED` のままなら、次の実行前にセッションを破棄して再作成してください。

<a id="interop-regression-targets"></a>

CMake の検証対象には `VeroScriptInteropTests`、`VeroScriptInteropExtendedTests`、`VeroScriptInteropTypeTests`、`VeroScriptInteropMemoryTests`、C11 の `VeroScriptInteropCHeaderTest` があります。有効時の `VeroScriptInteropNativeSdkTests` はインストール済みネイティブ利用者を検証します。実エクスポートを公開 ABI マニフェストに合わせ、別途取得した C11 例を展開済み SDK に対して確認します。一致する Core/VM でコンパイラサービス寿命、異スレッド拒否、コールバックと保持値を検証し、実結果とスキップを記録します。テストソースの存在だけではリリース受け入れになりません。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/abi-and-lifetime.md) · [fr-FR](../fr-FR/abi-and-lifetime.md) · [ja-JP](abi-and-lifetime.md) · [ko-KR](../ko-KR/abi-and-lifetime.md) · [zh-CN](../zh-CN/abi-and-lifetime.md) · [zh-TW](../zh-TW/abi-and-lifetime.md)
