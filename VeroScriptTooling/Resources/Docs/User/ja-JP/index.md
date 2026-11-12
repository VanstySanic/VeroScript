# Tooling モジュール

`VeroScriptTooling` は VeroScript language core の公開 analysis layer です。
editor、language server、static checker、debugger の integration で使用できます。

## 公開 surface

| Surface | 目的 |
| --- | --- |
| Analyzer | source/file を解析し、structured diagnostic を返します。 |
| Formatter | 完全な source replacement を生成します。macro は展開しません。 |
| Completion / call help | context に応じた completion、grouped overload、signature、継承/ interface method 実装の生成、host metadata、preprocessor entry。 |
| Hover / navigation | 解決済み declaration、documentation comment、definition、namespace-aware range。 |
| Outline / semantic tokens | declaration に基づく document symbol と semantic highlighting。 |
| References / rename | file をまたぐ references、read/write、prepare と workspace text edit。 |
| Workspace | open buffer overlay、multi-file analysis、import 解決、workspace symbol。 |
| LSP / DAP | `VeroScriptLspStdio::Run` と debugger adapter。LSP は resolve、type navigation、range、link、token delta、formatting、format code action を提供します。 |

生成された method completion は `FCompletionItem::bOverrideMember` を設定し、
`bImplementsInterfaceMember` は生成 method が pure interface requirement を満たす場合に true です。
virtual default method の override は継承 member の override として分類されます。
`Documentation` は active Tooling locale に従います。`InsertText` は method skeleton を含み、
`bInsertTextSnippet` は snippet placeholder の有無を示します。同名 overload は1つの item にまとめ、
detail に signature を1行ずつ表示します。LSP は生成候補を明示的な text edit として返し、
snippet 非対応 client には通常の method text を送ります。

C++ クエリ API の位置は 1 始まりです。開いている文書はディスク上のファイルより優先されます。`FWorkspaceOptions` はインポートパス、標準ライブラリーの検索ルート、定義、事前定義マクロ、暗黙のインポート、プロジェクト全体へのアクセスとホストバインディングのメタデータを保持します。

ネイティブホストはクエリ・ワークスペースのオプション、`FLspStdioRunOptions`、`FDapStdioRunOptions`/`FDapLaunchArgs` に `AnnotationProcessing` を指定できます。コンパイルと同じ凍結済みプロセッサーレジストリーを使ってください。LSP のファクトリーと依存関係リーダーは、プロトコルと解析ワーカーからの同時呼び出しに対応する必要があります。再利用前に依存内容と存在を確認し、`Cacheable=false` の結果は通常どおり返しますがキャッシュしません。

`WorkspaceRoots` は closed `.vs` file の indexing root を定義し、`ImportPaths` とは
別に扱われます。`MaxWorkspaceFiles` と `MaxWorkspaceFileBytes` は scan の件数と
ファイルサイズを制限します。`ShouldCancel` は長い scan を協調的に停止する callback
で、`FWorkspaceSymbolsResult::bCancelled` の結果は新しい index として cache または publish してはいけません。
workspace symbol の収集は declaration identity の重複を除去するため、複数の import
から同じ declaration が見つかっても一度だけ返します。
`ImplementationDocument` は派生型と interface/virtual member の実装を探し、
`FormatRangeDocument` は選択範囲を完全な物理行へ広げて返します。
`FWorkspaceSymbolsOptions` を受け取る `ImplementationDocument` overload は同じ
workspace limit と cancellation callback を適用します。cancel された場合は
`FNavigationResult::bCancelled` が設定され、partial な definition を publish
してはいけません。
`FormatDocument` と `FormatRangeDocument` には library caller 向けの任意の
indentation width overload もあります。width が 0 の場合は workspace の default
を使い、既定では各レベルに空白を4個出力します。`bInsertSpaces=false` を指定すると各レベルに
tabを1つ出力します。workspace を使わない formatter も同じ形式設定を受け付けます。
ステートレスな `VeroScriptFormat::FormatRangeSource` は、open workspace がない場合にも
同じ完全な物理行を対象とする range 契約を提供します。

`VeroScriptWorkspace::ImplementationDocument` は、別途定義された関数・メソッドや注釈が生成した本体も検索します。宣言へのナビゲーションとは区別します。`SourceImplementation` は本体のファイル名と物理位置を保持し、ファイル名が空なら別の本体は関連付けられていません。自由関数や非仮想メソッドの直接検索ではワークスペース全体を走査しません。

フォーマッターはブロック本体に Allman の波括弧を使います。`else`、`else if`、`catch`、`finally`、
`do…while` 末尾の `while` は改行し、直前の終了波括弧と揃えます。
初期化式の構文、リテラルの内容、プリプロセッサ指令は保持され、マクロは展開されません。

範囲の境界がブロックコメント、生文字列、複数行のプリプロセッサ指令の内部にある場合、整形編集を返しません。
指令全体または後続の通常コードを選択すると整形できます。マクロの原文と行継続は保持されます。

semantic kind は identifier の casing ではなく解析結果と host metadata から決まります。
user namespace と予約された `std` tree は completion、hover、navigation、reference、
rename、outline、semantic token で一貫した namespace symbol として扱われます。
completion item は引数や generic type の LSP snippet placeholder を含むことがあります。
LSP adapter が snippet 形式の挿入文字列を送るのは、client が
`textDocument.completion.completionItem.snippetSupport` を通知した場合だけです。
それ以外は通常の文字列を送ります。

namespace segment の item は member item と異なります。expression/statement site では、置換範囲の後ろに
separator がない場合だけ `InsertText` に `::` を追加します。既に `::` がある場合と `using namespace` の
補完では末尾の名前だけを挿入します。

修飾された namespace の member では、`FCompletionItem::Label` と `InsertText` は末尾の
名前および既存の snippet を保持し、`LabelDescription` が修飾 namespace を別に保持します。
LSP では標準の `labelDetails.description` に対応します。snippet 形式の挿入文字列は client が
`textDocument.completion.completionItem.snippetSupport` を通知した場合だけ送信し、それ以外は通常の
文字列を使います。server は Valence 用に、最終的な
label/declaration と UTF-16 semantic span を持つ `$valencePresentation` も送れます。
他の LSP client はこの extension を無視できます。これらの presentation field は filtering、
sorting、挿入 text を変更しません。

completion は context priority より先に textual match quality で並べます。順序は exact、
case-insensitive exact、prefix、word-boundary/CamelCase、contains、subsequence です。
`FCompletionItem::MatchQuality` が primary key、`SortPriority` が同順位の tie-breaker なので、
integration は返された順序を保持します。LSP の `sortText` も同じ完全な順序を持ちます。
LSP が先頭 item に `preselect` を付けるのは、exact/case-insensitive-exact/prefix の tier が
他の全 item より厳密に良い場合だけです。primitive と container の type word は builtin type
です。primitive spelling は completion、hover、semantic token で `keyword` のまま、container
spelling は `type`/type alias です。両者に definition の source location はなく、rename は
拒否します。説明用 hover text は active Tooling locale に従いますが、type identifier と
protocol field は変わりません。

LSP adapter は `completionItem/resolve`、type definition、implementation、selection
range、document link、folding range、full/minimal-delta semantic token、physical line
単位の range と on-type formatting、`source.formatDocument` と安全な duplicate-import
`source.organizeImports`、method 実装を生成する `refactor.rewrite` も提供します。cancel と watched-file notification によって
workspace を更新し、その間 closed-file workspace symbol index をキャッシュします。
`VeroScriptLspStdio::Run` は `FLspStdioRunOptions` を受け取り、client が
`workspaceFolders` または `rootUri` を提供しない場合の closed-file indexing 用に
`DefaultWorkspaceRoots` を指定できます。client が提供した root はこの default を置き換えます。

wire protocol と request、cancellation、設定の詳細は [Language Server](lsp.md) を、
debugger は [Debugging](debugging.md) を参照してください。

## デバッグアダプタープロトコル

CLI から stdio または TCP のデバッグアダプターを起動できます。

```powershell
veroscript --debug hello.vs
veroscript --debug --dap-port=4711 hello.vs
```

`--dap-port` には `--debug` と 1～65535 のポート番号が必要です。TCP の既定の待受アドレスは `127.0.0.1` です。`--dap-bind=<アドレス>` は TCP モードでのみ指定できます。プログラムと引数は DAP の `launch` または `attach` で渡します。デバッグでは VM を使用するため、その性能は mixed／JIT 実行の性能を示しません。

source line breakpoint は `setBreakpoints` で設定します。exception breakpoint と
function breakpoint は未対応です。空の `setExceptionBreakpoints` request は標準 client
launch handshake の clear 操作としてだけ成功し、non-empty filter とすべての
`setFunctionBreakpoints` request は失敗します。client はこれらの breakpoint category を
enabled と表示してはいけません。

`launch` または `attach` 前に設定した source breakpoint は pending/unverified です。
code の load 後、adapter は同じ DAP ID と `reason: "changed"` を持つ標準 `breakpoint`
event を送ります。verified になるのは、exactly one compiled source に属し、preprocessing 後も
active で executable location がある line だけで、matching basename への fallback はありません。
client は返された physical line を使用してください。function 内では next executable line まで
進むことがあります。完全な binding rule は [Debugging](debugging.md) を参照してください。

process 内 integration では `VeroScriptDapAdapter` を
`Initialize` → `Launch` または `Attach` → `SetBreakpoints` → `ConfigurationDone`
の順で使用します。process 内の `ConfigurationDone` は同期呼び出しなので、wire
host では execution worker から呼び出し、protocol thread が `pause`、`terminate`、
停止中の request を処理できるようにします。`Attach` は `Launch` と同じ loader で
program を読み込み、すでに実行中の VM への接続ではありません。`Pause` と `Terminate`
は thread-safe な cooperative control、`IsStopped()` と `IsRunning()` は実行状態を返します。

`VeroScriptDapStdio::Run` は `FDapStdioRunOptions` を受け取り、
`DefaultProgram`、`DefaultScriptArgs`、`DefaultStdlibSearchPaths`、
`DefaultEnvironment`、`DefaultWorkingDirectory`、`DefaultInput` などを default として
使用します。embed host が `LiveBindings` を指定すると登録済み host function を実行できます。
所有権を持たない catalog は DAP run 全体より長く存続させてください。live binding がない
場合、`bindingCatalogPaths` は analysis と launch 用の metadata/stub binding を提供します。

`VeroScriptDapTcp::Run` は `FDapTcpRunOptions` を受け取ります。`MaxSessions=0` は host が
停止するまで client を受け付け、`Port=0` は ephemeral port を選んで `OnListening` で通知し、
`BindAddress` は通常 `127.0.0.1` にします。`bAnnounceListen=false` は error stream への
readiness 通知を無効にします。stdio host は clean session で `0`、protocol または I/O error
で `1`、client が host terminate を要求した場合に `2` を返します。

### プロトコルに依存しないデバッグ セッション

`VeroScriptDebugSession` は DAP アダプターの下で再利用できる C++ レイヤーです。ホストや
独自アダプターで DAP wire protocol を実装せずに、ロード、ブレークポイント、同期的な
停止コールバック、状態検査、実行結果を管理したい場合に使用します。`LoadSource` と
`LoadFile` は workspace レイヤーと同じ import path、define、predefined macro、source
overlay、任意の binding catalog、implicit import path、import policy、標準ライブラリ
ルートを受け取ります。

`BindBreakpoint` は adapter 用の strict な source-breakpoint path です。one-based physical
line を exactly one compiled source に解決し、bind 時は nonzero `Id` と actual physical
`Line`、bind できない場合は localized `Message` を含む `FBreakpointBinding` を返します。
function 内の line は next executable line まで進むことがあるため、caller は requested line を
仮定せず、返された line を使用してください。

`Run` の前に、スクリプト引数、環境、working directory、入力、最大 call depth、要求する
backend、JIT policy を設定できます。`FDebugRunResult` には `bSuccess`、構造化された
`Diagnostics`、分離された `Output` と `ErrorOutput`、`ExitCode` が含まれます。デバッガー
接続中の実効 backend は `Vm` に強制されますが、要求した backend と JIT policy は設定として
保持され、JIT 統計には主にその無効化が記録されます。

`SetStopHandler` は実行が停止している間に同期的に呼び出されます。ハンドラーでは
`GetStackTrace`、`GetLocals`、`GetChildren`、`Evaluate`、`SetLocal`、`SetChild` を使って
状態を扱い、`SetRunMode` で `Continue`、`StepIn`、`StepOver`、`StepOut` を選択できます。
非同期ホスト向けの `RequestPause()` と `RequestCancel()` はスレッドセーフな協調的制御です。
通常のセッション設定と `Run` は制御スレッドで実行し、要求した操作が戻るまでセッションを
有効に保ってください。

## 公開境界と localization

SDK が配布する public header を使用してください。implementation-only file は対応する
integration surface ではありません。Tooling は CLI と runtime の localization catalog
を共有します。diagnostic code と argument は言語間で安定し、変化するのは表示だけです。
[Diagnostic localization](../../../../../VeroScript/Resources/Docs/User/ja-JP/localization.md) も参照してください。

## 現在の構文認識 editor coverage

completion、outline、hover、signature help、semantic token は、同じ型の declaration group、
`do`/`while`、expression list を持つ classic `for` clause、signed 8-bit `char` literal を認識します。
initializer は自身の declaration が導入される前の scope で解決されます。shadowing name や nested
lambda local が後続 declaration の initializer や `for` step expression に漏れることはありません。
複数 physical line にわたる raw string には各 physical line ごとに string semantic token が付き、その
literal 内で comment marker に見える文字列が comment と分類されることはありません。

## ワークスペース解析の通知

VeroScriptWorkspace::AnalyzeAll(OnDocument) は各文書につき一度、借用したパスと解析結果で通知を同期呼び出しします。終了後に必要なデータはコピーしてください。走査中の通知から同じワークスペースの文書を開く、閉じる、変更することは禁止です。

## 埋め込み解析とデバッグの VM 選択

Tooling を直接使うホストはインタープリターを明示的に選びます。実行前に `VeroScriptDebugSession::SetVmBackendFactory`/`SetVmBackendPath` を呼ぶか、`FDebugLoadOptions`／`FDapStdioRunOptions` の `VmBackendFactory`／`VmBackendPath` を指定します。ファクトリーとパスは排他です。実行コミット時に選択が凍結され、クリア／再読み込みでもドメインを保持して交換を許可しません。設定失敗時は前の選択を保持します。対応 Runtime のランチャーは製品側のバックエンドを設定しますが、Tooling のリンクだけでは設定されません。

コンパイル拡張スクリプトを実行するクエリーでは、解析／ワークスペースオプションの `ScriptCompileExtensions` に `VmBackendFactory` または `VmBackendPath` を指定します。デバッグ読み込みはコンパイル拡張側で明示しなければ選択 VM を継承します。拡張を実行しない構文解析や通常解析は VM 不要です。処理中はバックエンドと所有リソースを保持してください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
