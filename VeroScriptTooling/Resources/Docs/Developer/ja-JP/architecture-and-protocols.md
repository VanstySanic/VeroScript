# Tooling アーキテクチャと protocol

`VeroScriptTooling` は command-line、LSP、DAP、その他 host の共有 language-service layer です。public language-core contract を consume し semantic query と protocol transport を分離します。

LSP と DAP は `Private/Internal/VeroScriptToolingJson.h` と `.cpp` の非公開 JSON 値、パーサー、シリアライザーを共有します。文字列のエスケープには `Shared/Private/VeroScriptJsonString.h` を使用します。フレーミング、要求の対応付け、スケジューリング、キャンセル、プロトコル固有のフィールドは各アダプターが担当します。共有実装を変更する際は、オブジェクトの決定的な順序、数値変換、既存の深さ制限とエラー文を維持します。

## 層ごとのデータの流れ

core は parsing、semantic analysis、module loading、runtime metadata、diagnostic、debugger semantics を所有します。`VeroScriptWorkspace` は project context、document overlay、source/import resolution、preprocessor context、binding catalog、cross-file state を提供します。public feature service は explicit source/workspace を query し stable result を返し、LSP/DAP adapter はその serialization/transport を所有します。feature は editor state を read/source file を write せず、adapter は public service の semantic result を protocol text から再構築しません。

## 公開機能の分類

`VeroScriptAnalyzer` は imported file を含む workspace-aware diagnostic を URI 単位で publish し、stale result は empty list で clear します。`VeroScriptOutline`、`VeroScriptNavigation`、`VeroScriptHover`、`VeroScriptCallHelp`、`VeroScriptComplete`、`VeroScriptFindReferences`、`VeroScriptDocumentHighlight`、`VeroScriptRename`、`VeroScriptFormat` (`FFormatOptions`)、`VeroScriptWorkspaceSymbols` は structure、lookup、completion、call、reference、edit、format、workspace search を提供します。`std::` を含む namespace は semantic に扱い、host-only location は virtual binding URI を使います。LSP adapter は、client が `textDocument.completion.completionItem.snippetSupport` を true にした場合だけ snippet 形式の挿入文字列を返し、そうでなければ通常の文字列を返します。call context で completion target の後ろに `(` がある場合（空白やコメントを挟む場合も含む）、重複する呼び出し構文を避けるため callable snippet を省略します。rename は re-resolve、standard library は rename 不可、format は edit を返すだけです。

`FCompletionItem::MatchQuality` は primary textual ordering key、`SortPriority` は同じ
quality の tie-breaker です。adapter はこの order を LSP `sortText` に保ち、他より明確に
良い exact、case-insensitive-exact、prefix match にだけ `preselect` を設定します。
primitive word は built-in `Keyword` entry、container word は `TypeAlias`/`type` entry です。
両者の display detail は localized、type identifier は stable、source definition はなく、rename は
拒否されます。

同じ表示名の callable overload は1つの completion item にまとめ、detail には各 source-facing
signature を1行ずつ表示します。call context では特定の overload の parameter を推測せず、空の
argument stop を使用します。overload のない callable または generic entry は、client が
`textDocument.completion.completionItem.snippetSupport` を通知した場合に snippet を含められます。
それ以外は通常のテキストを挿入します。

修飾された namespace 内の補完では、`label` と挿入テキスト（`insertText`、置換範囲が
ある場合は `textEdit.newText`）に末尾の名前を保持します。利用可能な callable または generic type snippet があり、client が
`textDocument.completion.completionItem.snippetSupport` を通知した場合だけ snippet を含めます。
それ以外は末尾の名前だけを挿入します。`labelDetails.description` に namespace を渡します。
任意の `$valencePresentation` は Valence 向けの表示文字列と UTF-16 semantic span です。
他の LSP client は無視でき、この表示情報は並び順や挿入内容を変更しません。

namespace segment の completion は namespace member の completion とは異なります。
expression/statement site では、namespace item は末尾の名前を `label` と `filterText` に保持し、
置換範囲の直後に separator がない場合だけ挿入文字列に `::` を追加します。
`using namespace` の補完では名前だけを挿入します。client が snippet に対応する場合、call context では
`print` などの variadic host formatter は統合された overload 表示から placeholder を推測せず、
`print(${0})` のような空の引数 tab stop を使います。

semantic token では LSP adapter が resolved span と language/type word、directive、macro、
literal/comment 用の lexer baseline を combine します。この baseline は incomplete input でも
利用でき、LSP stream を encode する前に overlap を除去します。

## LSP プロトコルの契約

`VeroScriptLspStdio` は `Content-Length` framing、JSON-RPC correlation、dispatch、cancellation、diagnostic publication、lifecycle を所有します。公開 request は `textDocument/completion`、`completionItem/resolve`、`textDocument/hover`、`textDocument/definition`、`textDocument/declaration`、`textDocument/typeDefinition`、`textDocument/implementation`、`textDocument/prepareTypeHierarchy`、`typeHierarchy/supertypes`、`typeHierarchy/subtypes`、`textDocument/documentSymbol`、`textDocument/selectionRange`、`textDocument/documentLink`、`textDocument/foldingRange`、`textDocument/semanticTokens/full`、`textDocument/semanticTokens/full/delta`、`textDocument/signatureHelp`、`textDocument/references`、`textDocument/documentHighlight`、`textDocument/prepareRename`、`textDocument/rename`、`textDocument/formatting`、`textDocument/rangeFormatting`、`textDocument/onTypeFormatting`、`textDocument/codeAction`（`source.formatDocument`、`source.organizeImports`、`refactor.rewrite`）、`workspace/symbol`、`workspaceSymbol/resolve` です。

notification は `initialize`、`initialized`、`textDocument/didOpen`、`textDocument/didChange`、`textDocument/didSave`、`textDocument/didClose`、`shutdown`、`exit`、`workspace/didChangeConfiguration`、`$/cancelRequest` で、cancel は `-32800` を返します。completion trigger は `.`/`:`/`@`、signature trigger は `(`/`,`。LSP は zero-based UTF-16、C++ は one-based byte position で adapter が一度だけ変換し、old document version は ignore します。

## configuration と localization

`initializationOptions` と `workspace/didChangeConfiguration.settings` の `veroscript` setting は import/implicit-import path、`stdlibPath`、define/macro、policy、`bindingCatalogPaths` を持ちます。catalog は ordered merge、duplicate reject、次の non-lifecycle request で reload/reanalyze し、runtime callback は register しません。`initialize.locale` は BCP 47 preference、unsupported/missing は environment catalog/`en-US` fallback。diagnostic/binding/type/protocol/URI/path/source identifier は translate しません。

## invariants

source position/range、document identity/version、cancellation/correlation/wire order、stable diagnostic ID/argument、empty result と protocol failure の区別、tooling が edit を apply しない rule を保持します。[VeroScript 言語サーバーのリファレンス](../../User/ja-JP/lsp.md) も参照します。

## Debug と DAP boundary

`VeroScriptDebugSession` は compilation、breakpoint、step、VM inspection、evaluation、variable write、output、JIT/backend を所有し、`VeroScriptDapAdapter` は DAP mapping のみで second engine/transport を持ちません。`FDapLaunchArgs` は source/path/overlay/binding/policy/stop/call depth/backend/args/roots/environment/cwd/stdin を運び、debug attach は `Mixed`/`Jit` 要求でも `Vm`。`Initialize` の後の `ConfigurationDone` は synchronous run、stopped callback が debug call の有効地点、`Disconnect` は resume/clear、`initialized` は response 後に transport が emit し `VeroScriptDapAdapter::Initialize()` は emit しません。

`VeroScriptDebugSession::BindBreakpoint` は physical-line/preprocessor-map resolution を所有し、exact な compiled source label だけを登録します。invalid request、または eligible location がない non-executable request は localized display text を持つ unregistered result になります。adapter は external DAP ID を維持し、actual bound line を返し、pre-launch breakpoint の rebind 後に `Breakpoint`/wire `breakpoint` の `reason: "changed"` event を publish します。

### DAP トランスポートの契約

`VeroScriptDapStdio::Run` は caller stream の Content-Length DAP、`FDapStdioRunOptions` は live binding/catalog/args/stdlib/environment/cwd/input を提供し、live binding は `Run` 中 borrowed/priority です。`program`/`source`、`fileLabel`、`workspace`、`cwd`/`workingDirectory`、`stopOnEntry`、`args`、`input`、`env`/`environment`、`stdlibSearchPaths`/`stdlibPath`、`backend`、`maxCallDepth` と policy を受けます。args は `main(string[]
args)`、input は script stdin。上限 4096 args、2048 path、1024 environment で invalid input は local diagnostic です。

Core デバッガーの `bReadOnly` は `FDebugVariable`、`FDapVariable` を経て、DAP の `presentationHint.attributes = ["readOnly"]` に渡されます。入れ子の variant の不変ペイロードも、ウォッチ結果を含めこの状態を保持します。不変値の編集拒否は Core が担当します。

`VeroScriptDapTcp::Run` は IPv4 sequential。`FDapTcpRunOptions::BindAddress` default は `127.0.0.1`、empty/`0.0.0.0` は all interface、`Port == 0` は `OnListening`、`MaxSessions == 1`/`0`/negative は one/unlimited/one の rule です。

## LSP workspace と request contract

`VeroScriptLspStdio::Run` は worker analysis/cancellation を使います。root priority は `workspaceFolders`、`rootUri`、FLspStdioRunOptions::DefaultWorkspaceRoots。root は index 用で `.vs` だけ scan、open overlay が優先。safe default は 2000 ms interactive / 5000 ms semantic、512 completion、2000 symbol、5000 reference、10000 file、2 MiB file；lspRequestTimeoutMs、maxCompletionItems、maxWorkspaceSymbolResults、maxReferenceResults、maxWorkspaceFiles、maxWorkspaceFileBytes は safe range に clamp します。

`VeroScriptFormat` は字句ベースの formatter です。既定では各インデントレベルに4個の空白を出力し、`bInsertSpaces=false` の場合は各レベルにタブを1つ出力します。LSP は `options.insertSpaces` と `options.tabSize` に従い、`insertSpaces=false` ならタブを、そうでなければ `tabSize` 個の空白を使います（最大16、未指定または無効の場合は4）。範囲フォーマットは物理行全体に拡張し、`FRangeFormatResult` に実際の置換範囲を返します。失敗時の LSP 編集結果は空です。`VeroScriptWorkspace::WorkspaceSymbols` は open/overlay/closed を統合します。`FWorkspaceSymbolsOptions::ShouldCancel` は単一クエリの間だけ借用され、保持されません。キャンセルされた結果はキャッシュにも公開にも使われず、既定キャッシュは document/config の変更時に無効化されます。

## ソース文書コメント

ソース定義されたプリプロセッサーマクロの補完項目には、直前に連続して置かれた `//`/`///` または `/* ... */` コメントを含められます。host 補完文書は binding metadata から取得します。`completionItem/resolve` は既存文書を保持し、文書がない場合に限り `detail` を Markdown として使用します。ホバーはソース宣言前の連続コメントを抽出し、`@param 名前 説明`、`@return 説明`、`@returns 説明` をアクティブな言語カタログのラベルで整形します。未知のタグは本文に残ります。これは Tooling の表示規約であり、コンパイラーのコメント処理は変わりません。

## セマンティック着色のスケジューリング

semantic token は独立 analysis worker を使います。順序付き protocol worker が document update、request state、output を所有し、job は buffer/options/binding のコピーを所有します。version/dependency 検証後に immutable parsed/token snapshot を公開します。同一 version の full/delta、hover、definition、completion は有効な snapshot を再利用します。着色用 recovered AST は point query の原本として使いません。cancel/deadline probe は lex、preprocess、parse、import、symbol traversal、encode に到達します。queue 内 cancel は protocol worker を起こし、期限切れ job は active analysis を待たずに終了します。既定 budget は interactive 2000 ms、semantic 5000 ms。`lspAnalysisTimeoutMs` で分離し、`lspTraceAnalysis` で計測します。[予算](../../User/ja-JP/lsp.md)を参照してください。

### コールバック、キャッシュ、ライフサイクルの契約

`FSemanticTokensOptions::ShouldCancel` と `RecordStage` は同期クエリの呼び出し中だけ借用され、その解析スレッドで実行されます。キャプチャは呼び出しが戻るまで有効である必要があります。キャンセル時はトークンなしで `bCancelled` を返し、不完全な結果を公開またはキャッシュしてはいけません。計測コールバックの例外は無視します。`FSemanticTokensResult::Snapshot` はプログラムとソースの記憶領域を所有しますが、不透明な Tooling 内部状態であり、外部向けのキャッシュ・再利用 API ではありません。

再利用には文書内容／バージョン、設定、依存関係の一致が必要です。ディスク記録はインポートソース／パッケージの読み込み前に取得し、公開や再利用前に再確認します。無効な記録は再利用できません。オーバーレイの変更も影響するインポートを無効化します。同じ URI を閉じて開き直すと、バージョンが小さい値から再開しても解析／トークン状態と古い結果 ID を破棄します。設定変更は実行中のタスクも無効化します。同一のソースラベルは重複するファイルシステム正規化を省き、異なるラベルは引き続き正規パスで比較します。

`VeroScriptLspResponsivenessTest.py` は実際の stdio プロセスで full/delta 再利用、解析中の hover、キャンセル、期限、依存関係編集、設定変更、閉じて再度開く動作を検証します。lexer と workspace テストはキャンセル後の再試行と読み込みからスナップショット構築までのディスク変更を検証します。これらの回帰テストは担当する Tooling／コアのテスト領域に保持します。

## リクエスト内の問い合わせ再利用

参照収集と名前変更は `Internal/VeroScriptSourceQuerySession` で現在のファイルの正確な解析済みプログラムを点問い合わせに再利用します。状態はリクエスト内だけに保持し、ディスク依存を確認し、回復解析やプロセッサーで生成したプログラムは再利用しません。宣言が現在のソースにあるブロック/関数ローカル変数では、同じ名前を含む他の開いたモジュールを走査しません。リクエスト内の集合でトークン位置を重複排除し、既存パスの各表記は走査中に一度だけ解決します。

プロトコル専用の設定（予算、トレース、結果上限）は意味解析スナップショットを保持します。解析設定の変更はスナップショットと実行中ジョブを無効にします。ワークスペース探索の上限変更はシンボル索引だけを無効にし、解析済みプログラムを維持します。

Darwin では `Private/Internal/VeroScriptLspThread.h` がプロトコルと意味解析ワーカーを 8 MiB の pthread スタックで作成し、他のプラットフォームは `std::thread` を使います。join と所有権を維持し、スタック拡大をパーサー／解析上限の拡大として扱わないでください。JSON の浮動小数点出力は [Core の十進変換](../../../../../VeroScript/Resources/Docs/Developer/ja-JP/architecture-and-pipeline.md#floating-formatting-support)にある非公開ヘルパーを使います。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/architecture-and-protocols.md) · [fr-FR](../fr-FR/architecture-and-protocols.md) · [ja-JP](architecture-and-protocols.md) · [ko-KR](../ko-KR/architecture-and-protocols.md) · [zh-CN](../zh-CN/architecture-and-protocols.md) · [zh-TW](../zh-TW/architecture-and-protocols.md)
