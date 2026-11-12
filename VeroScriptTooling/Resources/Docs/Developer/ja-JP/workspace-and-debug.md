# ワークスペース、LSP、DAP

## ワークスペースの状態

`VeroScriptWorkspace` は project/module context の stateful boundary で、open overlay、source/import resolution、cross-file analysis、workspace symbol、preprocessor context、binding catalog を所有します。開いた文書のオーバーレイはディスクの内容より優先し、文書を閉じると失効します。option は path、stdlib、implicit import、define/macro、policy、ordered catalog。catalog は merge/reject duplicate で host symbol を記述し runtime behavior は register しません。

`TryGetDocument` は単一の開いた buffer の独立したコピーを返します。パスがなければ
`false` を返し、出力は変更しません。全 buffer が必要な場合は `SnapshotDocuments` を
使います。LSP の単一 document 読み取りでは無関係なソースをコピーしません。

## 文書のライフサイクルと失効

`textDocument/didOpen` で overlay create、`textDocument/didChange` で incremental update、`textDocument/didSave` の optional `text` で refresh、`textDocument/didClose` で remove。open/accepted change/save/configuration 後 diagnostic publish。URI version が old/equal は ignore、reopen は new lifetime。`$/cancelRequest` は pending request を cancel し `-32800`。

## 言語機能の処理の流れ

`VeroScriptAnalyzer`、`VeroScriptComplete`、`VeroScriptCallHelp`、`VeroScriptHover`、`VeroScriptNavigation`、`VeroScriptFindReferences`、`VeroScriptDocumentHighlight`、`VeroScriptOutline`、`VeroScriptWorkspaceSymbols`、`VeroScriptRename`、`VeroScriptFormat` が direct service です。LSP は completion/resolve、navigation と type hierarchy、document symbol、selection/folding range、document link、semantic token、signature/reference/highlight、rename、document/range/on-type format、`source.formatDocument`、`source.organizeImports`、`refactor.rewrite` の code action、workspace symbol/resolve を公開します。`source.organizeImports` は安全に削除できる完全一致の重複 import がある場合だけ返します。空の cursor 位置の `refactor.rewrite` は class/struct に不足している継承 method 実装の skeleton を提案します。range format は選択範囲を physical line に expand し、actual replacement range を返します。namespace は semantic で、`std::` と host-only `veroscript:host-bindings` を含みます。

completion では `FCompletionItem::MatchQuality` が primary textual rank で、
`SortPriority` は同順位だけを分けます。adapter は result order を保持してください。
LSP の `sortText` も同じ順序を表し、`preselect` は他より明確に良い exact、
case-insensitive-exact、prefix match にだけ付与します。primitive word は completion、hover、
semantic token で `Keyword` のまま、container word は `TypeAlias`/`type` です。両者は
localized display detail を持ちますが、source definition はなく rename target にはなりません。表示には catalog key
`tooling.builtin_type.detail` と `tooling.builtin_type.documentation` を使用し、
protocol field と type identifier は翻訳しません。

修飾 namespace の member では、`FCompletionItem::Label` と `InsertText` は末尾の名前と
tooling が生成した snippet を保持し、`LabelDescription` は修飾 namespace を保持します。
LSP に serialize するとき、snippet は client が
`textDocument.completion.completionItem.snippetSupport` を通知した場合だけ含めます。
それ以外は末尾の名前を通常の文字列として送ります。namespace は標準 LSP の
`labelDetails.description` に serialize してください。任意の `$valencePresentation` は
Valence の最終 label/declaration と UTF-16 semantic span を追加し、他の LSP client は無視
できます。filtering、sorting、挿入内容を変更してはいけません。

LSP semantic-token adapter は resolved semantic span と lexer baseline を merge します。
fallback は language/type word、directive、macro、literal/comment を各 valid physical line で
扱い、parse recovery 中も使用でき、LSP stream に encode する前に overlap を除去します。

## 診断とローカライズ

diagnostic は source file ごとで imported file に `textDocument/publishDiagnostics` を送り clean/stale には empty list。`initialize.locale` は BCP 47、unsupported は environment catalog、missing は `en-US` fallback。identifier/argument/protocol/URI/path/type/source は translate しません。

## デバッグ

`VeroScriptDebugSession` は Core の読み込み、実行、停止、スタック、スコープ、値、評価をツール向けデータに変換し、`VeroScriptDapAdapter` は DAP メッセージへ対応付けます。`VeroScriptDapStdio` と `VeroScriptDapTcp` は転送層であり、VM、ブレークポイント、寿命管理を再実装しません。`VeroScriptDebugSession::SetOutputHandler` には `VeroScriptDebugSession::FOutputHandler` を設定します。これは `Run()` 中にテキストとエラーフラグを受け取ります。`FDebugRunResult` は `Output`／`ErrorOutput` に全出力を保持し、DAP は終了前に `stdout`／`stderr` を送信します。[言語サーバーのリファレンス](../../User/ja-JP/lsp.md)も参照してください。

## デバッグセッションの契約

`LoadSource`/`LoadFile` は `FDebugLoadResult`、`Clear` は state invalidation、`SetBreakpoint` は session ID。`SetRunMode`、`SetExecutionBackend`、`SetMaxCallDepth`、`SetJitPolicy`、`GetLastJitStats` の contract を持ち attach は `Vm`/JIT invalidate。stopped で `EDebugStopReason`、`GetStackTrace`、`GetLocals`、`GetChildren`、`Evaluate`、`SetLocal`、`SetChild` が valid。`FDebugRunResult` の success/exit code は別です。

`BindBreakpoint` は adapter の strict path です。one-based の physical source line を、ただ一つの compiled source の preprocessor map で解決し、解決済みの preprocessed location だけを `SetExactBreakpoint` に登録します。inactive、missing、ambiguous、out-of-range の request、または eligible location がない non-executable request は登録せず、localized display text を返します。function 内では次の executable physical line まで進むことがありますが、module initialization は exact line でなければなりません。

## DAP アダプターのライフサイクル

`VeroScriptDapAdapter` は in-process mapper。`Initialize`、`Launch`/`Attach`、breakpoint、`ConfigurationDone` は `Run` を block し `Stopped`/output/terminal を emit。`Next`、`StepIn`、`StepOut`、`StackTrace`、`Scopes`、`Variables`、`Evaluate`、`SetVariable` は stopped callback 限定。`Attach` は load であり mid-run attach でなく、`Disconnect` は resume/cleanup。live `IHostBindingCatalog` は out-of-process に retain しません。

`setBreakpoints` は source が entries を置換しても stable な DAP ID を維持します。load 前は pending かつ unverified です。load 時に `BindBreakpoint` で rebind し、各 entry の `Breakpoint` event を emit します。wire adapter はこれを標準 DAP `breakpoint` event（`reason: "changed"`）として serialize します。返す DAP line は request line ではなく、実際に bind された physical line です。

## debug output と completion

stdout は line-buffered、complete line、`std::console::flush`、bounded buffer で flush、stderr は immediate。handler exception は isolate。successful int main の ExitCode は non-zero 可能で bSuccess と別。preprocessor line map は physical source を remap し conditional evaluation は active define/macro を使用。

## DAP トランスポートと起動の契約

`VeroScriptDapStdio::Run` は Content-Length、`FDapStdioRunOptions` は binding/catalog/args/stdlib/environment/cwd/input。live binding は `Run` 中 borrowed。`program`/`source`、`fileLabel`、`workspace`、`cwd`/`workingDirectory`、`stopOnEntry`、`args`、`input`、`env`/`environment`、`stdlibSearchPaths`/`stdlibPath`、`backend`、`maxCallDepth`。args は `main(string[] args)`、input は script stdin、environment は restore。4096/2048/1024 limit を enforce。

停止可能なエンドポイントでは `FDapTcpRunOptions::Control` に共有の `FDapTcpRunControl` を設定します。ホストの `RequestStop()` がソケット待機を解除し、ドメインの切断規則に従って停止中の呼び出しを再開します。ドメインの解放やモジュールのアンロード前に `Run` のスレッドを join してください。停止後の起動には新しい制御オブジェクトを使用します。

`VeroScriptDapTcp::Run` は IPv4、`127.0.0.1` default、empty/`0.0.0.0`、`Port == 0` と `OnListening`、`MaxSessions == 1`/`0`/negative の session rule。

## DAP ロケールと接続終了の規則

DAP locale は initialize.arguments.locale BCP 47、unsupported は environment catalog を保持。Launch/Attach load option は同じで Attach は mid-run VM attach ではありません。Attach 後 Disconnect は terminateDebuggee false、Launch は true default。false は session release、true は host termination request；TCP は MaxSessions policy を維持します。

## 既存 Session のデバッグ

ホストは `IVeroScriptLiveDebugDomain` を提供し寿命を保持します。`Info()` は同期済みスナップショットです。プロトコルスレッドの要求は実行安全点へ VM 処理を投入します。一接続のアクティブコンテキストは一つ（thread 1）なのでデバッグ呼び出しを直列化します。`Connect()` はコールバックを発火せずコードを開始／再ロードしません。失敗時はコールバックを保持しません。`Activate()` が構成を完了します。`SetBreakpoints` は一ソースを不可分に置換し、指定 ID を保ち入力順に結果を返します。同じ更新の重複イベントは出しません。`Disconnect()` はブレークポイントとコールバックを解除・排出しホストポリシーで停止を解放します。取消は別のホスト操作です。

コールバックと停止 VM の検査、評価、変更、再開は Session の制御スレッドで行います。停止コールバックはそのスレッドで要求を処理します。スレッドを越えて直接使える制御は `RequestPause()` と共有 `FExecutionControl::RequestCancel()` だけです。借用プログラム、バインディング、ソース overlay、エントリー名はコールバック終了で失効します。必要なメタデータをコピーしてください。overlay は現在の構成であり固定ソースではありません。ソース／バインディング版は別途公開します。

[既存 Session のデバッグ](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/session-debugging.md#debug-disconnect-lifetime) を参照してください。

## 実装の責任範囲

[C++ embedding と session — 実装の責任範囲](../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ja-JP/session.md#session-debug-implementation) を参照してください。

## メモリの検査

`FMemoryInspection` をデバッガー変数へ伝播します。許可された格納アドレスは十進文字列 `memoryReference`、`veroMemory` は利用可能な `objectId` / `objectAddress` / `dataAddress` / `byteLength` を十進文字列で返します。uint64 の精度と未提供時の省略を保ちます。メタデータの子に書き込み先や偽の格納アドレスを作りません。`variablesReference` は ID と独立した不透明 DAP ハンドルです。任意のメモリ読み書き能力を宣言しないでください。

## コンパイル処理とソースメタデータ

ネイティブホストはクエリ、`FWorkspaceOptions`、`FLspStdioRunOptions`、`FDapStdioRunOptions`、`FDapLaunchArgs` の `CompileProcessing` へ `FCompileProcessingOptions` を渡します。一般プロセッサーはモジュール読み込み前に設定され、`AnnotationProcessing` とコアの準備を共有します。解析、補完、移動、索引、デバッグで同じ構成を使ってください。レジストリは凍結し、各コンパイルで専用インスタンスを作成します。ネイティブコールバックはサービス開始時に提供し、LSP/DAP の JSON では送れません。

一般依存リーダーは旧注釈リーダーに優先します。キャッシュは実際に読んだ依存を再検証し、非キャッシュ対象のプロセッサー結果を再利用しません。構成変更後は `SetOptions` を呼びます。コアの起源区間が `ActiveSource` の編集を元座標へ対応付け、移動、トークン、診断、デバッグで共有されます。生成宣言は独立した読み取り専用ソースです。ホバーと補完はコメント保持方針に従うコアの説明を使い、メタデータのリフレクションもコアと同じシグネチャです。

## スクリプトコンパイル拡張

同じオプションの `ScriptCompileExtensions` が拡張ルート、構成、実行方針を指定します。`import compiler "extension.vs";` はコアの冒頭規則に従い、補完、パス移動、文書リンクはプロセッサーを実行せず認識します。

拡張ソースの編集には `GetScriptCompileServiceCatalog()` を `Bindings` に渡すか、書き出したカタログを `bindingCatalogPaths` で指定します。`std::compiler` のコンパイル専用シグネチャを提供し、通常コードへ実行権限を与えません。

意味解析スナップショットは、最初は存在しなかったものも含め、スクリプトが読んだリソースを検証します。オーバーレイの版や拡張コードの変更も失効要因です。独自リーダーでは、終了したタスクの統合コード/リソースリーダーを再保持できないため再解析します。full/delta は公開済みトークン列を比較し、構成変更時は `SetOptions` が必要です。

## プロトコル編集とソース座標

範囲・入力時整形は複数行コメントや生文字列の断片を保持します。周辺を整える場合はリテラル全体または文書を整形してください。

LSP は動的監視があればバインディングカタログも監視し、サイズ・時刻が同じでもイベントで再読込します。イベントがなければ両属性をクエリ前に確認します。不正カタログは直前の正常スナップショットを保持し状態を通知します。構成変更で監視を置換します。LSP/DAP はモジュール規則でパスを正規化・重複排除し、Windows の大小文字別名も同じファイルです。DAP はカタログと起動設定の検証後に置換し、検証拒否ではプログラムとネイティブ索引を維持します。

デバッグ再読込の取消・例外は古いバイトコード、バインディング、ソース、ブレークポイントを消去してから伝播します。次の読込は可能です。DAP は失敗応答を返し接続を維持します。

DAP はスレッド 1 のみです。`stackTrace` の `startFrame` / `levels` に従い、0 は残り全フレーム、`totalFrames` は総数で、停止中のフレーム識別はページをまたいで保持します。`FDebugStackFrame.Column` は物理 UTF-8 バイト列、`FDapStackFrame.Column` は 1 始まり UTF-16 列で、トランスポートがクライアントの 0/1 始まりへ変換します。補正はインポート/生成ソースもメタデータ削除前に保存し、後のディスク編集では変わりません。live domain は DAP UTF-16 列を返します。

## 解決済みモジュール環境

解析、クエリ、workspace、debug と `FLspStdioRunOptions` / `FDapStdioRunOptions` の `ModuleEnvironment` は、コアと同じ不変の `FModuleEnvironment` です。完全な環境を渡し、明示 live カタログとの競合は診断になります。環境変更で索引は失効し、編集なしでもプロセッサーの依存を検証します。

共通リゾルバーが imports、overlays、所有権、可視性、処理構成、リソースを扱います。ホスト候補は起源と公開シグネチャの可視型に従います。解析がスコープを復元できないとき、補完の最終代替は全ホストカタログを列挙しません。標準カタログは `std::i18n` を含み、getter のみのプロパティは `property` / `readonly` を維持します。

LSP は `capabilities.experimental.veroscriptModuleEnvironment` にコンパイル/実行の指紋と `moduleSourceMethod: "veroscript/moduleSource"` を通知します。`{ "uri": "veroscript-module:/..." }` は `uri`、`languageId`、`text`、`readOnly`、コンパイル指紋、または不許可/不在なら `null` を返します。フレーム上限内に収め、クライアントはこの URI スキームの内容プロバイダーを用意します。通常のファイル URI は維持します。合法な再エクスポートは対象だけを読み取り可能にし、全推移モジュールは公開しません。rename もこれらの読み取り専用文書を編集しません。

デバッグは環境を保持し外部依存を検証します。`TryGetSource` と DAP `sourceReference` はコンパイル時点の主バッファ、メモリモジュール、生成ソースを返します。物理ラベルがあっても後のディスク内容で置換しません。reload/clear で旧参照は失効し、live domain は独自ソースコールバックを使います。同一路径は最後の overlay が優先し、エディターバッファがすべての問い合わせで環境のソースを置換します。

アクセス判定は修飾型の同一性を保持します。別名前空間の同じ末尾名だけでは private/protected/friend 権限は得られません。短い名前は同じ一意の宣言を指す必要があり、グローバル宣言および `::Same` が優先します。

import 整理は連続ブロック内の完全に重複したグローバル import だけを、現在の前処理とバッファで確認して削除します。マクロで書き換えた行、非アクティブ分岐、不明な依存、リテラル、追加文は保持します。指令、コメント、他宣言、スコープ境界はブロックを区切ります。継続行や字句エラーでは編集せず、元の改行を保持します。

## ホスト提供ドメインへの接続

前述の読込型 `Attach` は `program` または `attachMode` 省略時です。持続型では `IVeroScriptLiveDebugDomain` を `VeroScriptDapStdio::Run` / `VeroScriptDapTcp::Run` の対応オーバーロードへ渡し、`attachMode: "domain"` と `domainId` を送ります。送信前に `initialize.body.veroscriptSupportsDomainAttach == true` を必須確認し、不在/false では launch/program に切り替えないでください。不正フィールドでは読込せず失敗します。attach 応答の識別、版、能力は当該ドメイン固有で、`configurationDone` はプログラムを開始せずブレークポイントを有効にします。接続は後のホスト呼び出しでも維持します。ドメインの寿命を保ち、デバッグ対象コンテキストをスレッド 1 に直列化します。終了要求は `veroscriptCanTerminate: true` の場合だけ可能で、それ以外は接続を維持したまま失敗します。

切断前に送信済み pause/cancel の復帰を待ちます。終了中の新制御は失敗し、古い制御は次の接続を対象にしません。制御処理は切断や、制御を排出している同じスレッドへの作業を待ってはいけません。ホスト切断とコールバック排出は制御 mutex の外で行い、ホストは attach、再開、切断、停止状態へのアクセスを引き続き直列化します。

生成メソッド補完は `FCompletionItem::bOverrideMember`、`bImplementsInterfaceMember`、`Documentation`、`InsertText`、`bInsertTextSnippet` を持ちます。純粋インターフェイスの実装と、既定本体を持つ仮想メソッドなどの継承 override は区別します。説明は現在のロケールで、同名オーバーロードはグループ化します。LSP は明示的なテキスト編集を送り、snippet 非対応では通常テキストを使います。

## プロトコルの JIT メタデータ

`Initialize` はプロトコルのメタデータと機能フラグを返します。`jitCapability` は Core の JIT サービス契約（`backend-services`）を示し、選択されたバックエンドやネイティブコードの機能は示しません。ホストは `IJitBackendFactory::GetDescription()` でそれらを取得します。ホストはファクトリを直接渡すか、ライブラリまたはディレクトリへの明示的な UTF-8 パスで `SetJitBackendPath` を呼び出します。ローダーは ABI を検査し、ファクトリや成果物が保持される間ライブラリを維持します。`PATH` や暗黙の環境設定からは探索しません。Tooling 自体は JIT モジュールをロードせず、DAP の `backend` は実行方針だけを保存します。デバッガー接続中の実行には VM を使用します。

## アダプターの VM 設定

`FDebugLoadOptions::VmBackendFactory` / `VmBackendPath` を読み込み処理へ伝播させます。`FDapLaunchArgs` と stdio/TCP オプションはホストが指定した選択を保持します。DAP リクエストでネイティブファクトリーは渡せません。ファクトリーとパスは排他的です。デバッグは VM 上で動作し、`backend` フィールド自体は VM をロードしません。実行確定後の選択は不変で、保持するドメインとの互換性が必要です。コンパイル拡張には `ScriptCompileExtensions` で独自の選択を指定でき、未指定ならデバッグ読み込みの選択を引き継ぎます。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/workspace-and-debug.md) · [fr-FR](../fr-FR/workspace-and-debug.md) · [ja-JP](workspace-and-debug.md) · [ko-KR](../ko-KR/workspace-and-debug.md) · [zh-CN](../zh-CN/workspace-and-debug.md) · [zh-TW](../zh-TW/workspace-and-debug.md)
