# VeroScript 言語サーバー

`veroscript-lsp` は VeroScript の Language Server です。標準入出力上で
`Content-Length` による JSON-RPC framing を使用します。プロジェクトと同じ
VeroScript distribution に含まれる server を起動してください。

server は compiler と Tooling engine を直接使用するため、language rule、import、
preprocessor、host binding metadata、diagnostic catalog は CLI と共通です。

ホバーは構造化 Markdown を返します。シンボルの見出し、オーバーロードごとに改行した
VeroScript シグネチャのコードブロック、ドキュメント、定義位置を表示します。
ネイティブ Tooling と CLI のホバー要約は引き続きプレーンテキストです。

## Transport と lifecycle

通常の順序は `initialize`、`initialized`、document/workspace notification と
request、`shutdown`、`exit` です。`shutdown` の response を受け取ってから `exit`
を送信してください。shutdown なしの exit は 0 以外のプロセス結果になります。

`exit` 通知を受信すると待機中および実行中の意味解析をキャンセルし、解析期限を待たずにプロセスを終了します。

server は request 実行中も入力を読み続け、document update は受信順に適用します。
client は pending request に `$/cancelRequest` を送信でき、cancel された request は
JSON-RPC error code `-32800` を返します。request には bounded server deadline があり、
closed-file workspace scan は列挙と parse の途中でも cancellation を確認します。入力
queue は有界で backpressure を適用します。

client が `workspace/didChangeWatchedFiles` の dynamic registration を通知すると、
server は `initialized` の後に `client/registerCapability` を送り、`**/*.vs` と
`**/*.[vV][sS][lL][iI][bB]` の watcher を要求します。dynamic registration に対応しない client は
これらの pattern を自分で設定し、`workspace/didChangeWatchedFiles` notification を
明示的に送信できます。
`Content-Length` は UTF-8 byte 数です。framing または JSON の error は error stream に
出力されます。

file URI は標準の LSP file-URI 規則に従います。`file://server/share/module.vs` のような
UNC URI は network path として扱われ、host と share を失わずに round-trip します。

## セマンティック解析と予算

文書の更新は受信順に適用します。意味トークン解析は文書とバインディングのスナップショットを所有する別ワーカーで実行するため、色付け中もホバーや補完に応答できます。変更のない文書では有効な解析済みスナップショットと完全なトークン列を再利用します。文書/依存の変更は影響する結果を無効にし、解析設定、監視ファイル、バインディングの変更は再解析を開始します。要求予算、トレース、結果上限だけの変更では意味スナップショットを維持し、ワークスペース探索上限は解析済みプログラムを消さずシンボル検索を更新します。再利用前にディスク依存の更新時刻とサイズを確認します。置き換えられた解析は `-32801` を返し、古いトークンを公開しません。

同じ URI を閉じて再度開くと、新しい文書ライフサイクルが始まります。バージョン番号が再開しても古いセマンティックトークン結果 ID は破棄します。古い ID の delta リクエストには完全なトークンデータを返すため、以降は新しい `resultId` を使用してください。

budget は protocol worker の dispatch 時から計測します。analysis queue の待ち時間は含み、input queue の待ち時間は含みません。cancel は `-32800`、timeout は `-32001` を返します。queue 内の期限切れ request は実行中の解析を待たずに解放します。互換性のため `lspRequestTimeoutMs` だけを指定すると analysis budget も設定します。分離するには `lspAnalysisTimeoutMs` を明示してください。trace は `lex`、`preprocess`、`parse`、import path resolution（`imports`）、symbol、query、encode、queue を記録します。`load` は内部 stage を含むため単純に合計できません。

クライアントのタイムアウトにはプロトコルキュー待ちと文書更新/診断の処理も考慮してください。これらの協調的な解析予算は、要求送信から応答までの最大時間を保証しません。

## Initialize と設定

`initialize.locale` は document を open する前に指定する BCP 47 preference です。
設定は `initializationOptions.veroscript` に置き、後から
`workspace/didChangeConfiguration` の同じ object で更新できます。

```json
{
	"locale": "ja-JP",
	"initializationOptions": {
		"veroscript": {
			"importPaths": ["src", "shared"],
			"stdlibPath": "stdlib",
			"defines": ["FEATURE_SCORE=1"],
			"implicitImportPaths": [],
			"allowCircularImports": true,
			"allowProjectWideAccess": false,
			"lspRequestTimeoutMs": 2000,
			"lspAnalysisTimeoutMs": 5000,
			"lspTraceAnalysis": false,
			"maxCompletionItems": 512,
			"maxWorkspaceSymbolResults": 2000,
			"maxReferenceResults": 5000,
			"maxWorkspaceFiles": 10000,
			"maxWorkspaceFileBytes": 2097152,
			"bindingCatalogPaths": ["bindings/core.json", "bindings/game.json"],
			"predefinedMacros": [
				{
					"name": "FEATURE",
					"parameters": ["value"],
					"variadic": false,
					"body": "value",
					"sourceName": "host"
				}
			]
		}
	}
}
```

| 設定 | 型 | 内容 |
| --- | --- | --- |
| `importPaths` | string array | 追加の module search root。 |
| `stdlibPath` | string | standard library の search root。空文字列で custom root を解除します。 |
| `defines` | string array | `FEATURE_SCORE=1` などの object-like preprocessor 定義。 |
| `implicitImportPaths` | string array | 通常の visibility rule に従って全 source から利用する file。 |
| `allowCircularImports` | boolean | circular module dependency を許可するか。 |
| `allowProjectWideAccess` | boolean | non-exported declaration の project-wide sharing を許可するか。 |
| `lspRequestTimeoutMs` | integer | 対話 request の deadline（milliseconds、50–30000）、既定値 2000。 |
| `lspAnalysisTimeoutMs` | integer | semantic full/delta の budget（milliseconds、50–30000）、既定値 5000。 |
| `lspTraceAnalysis` | boolean | `window/logMessage` で解析時間を出力。既定値 false。 |
| `maxCompletionItems` | integer | 1 回の completion list の最大項目数（32–5000）。超過時は incomplete と表示されます。 |
| `maxWorkspaceSymbolResults` | integer | 1 回の `workspace/symbol` で返す最大 symbol 数（64–20000）。 |
| `maxReferenceResults` | integer | 1 回の reference request で返す最大 location 数（64–50000）。 |
| `maxWorkspaceFiles` | integer | LSP workspace root 配下で closed-file index に含める `.vs` の最大数（1–100000）。 |
| `maxWorkspaceFileBytes` | integer | closed-file index の 1 ファイルあたりの最大サイズ（1024–67108864 bytes）。 |
| `bindingCatalogPaths` | string array | 順番に merge する host catalog。重複定義は reject されます。 |
| `predefinedMacros` | object array | `name`、`parameters`、`variadic`、`body`、任意の `sourceName` を持つ host macro。 |

設定は同じ project の CLI と揃えてください。catalog file の変更は検出時に再読込
され、open document が再解析されます。catalog は metadata であり runtime callback を
登録するものではありません。

`initialize` response には `serverInfo` も含まれます。ここには server name、生成された
product version、host platform、JIT capability が入ります。これらは実行中の
distribution の metadata なので表示には使えますが、特定 version を hard-code したり、
platform や JIT の値から language semantics を推測したりしないでください。

## 通知する機能

| Capability | Contract |
| --- | --- |
| Text synchronization | open/close、incremental change、text を含む save。 |
| Completion | `.`, `:`, `@` が trigger です。補完項目は取得可能な documentation を含み、resolve はそれを保持します。documentation がない場合に限り `detail` を Markdown として追加します。 |
| Hover | 解決済みのシンボル detail と、連続したソースまたは host documentation。ソースの `@param` と `@return(s)` はローカライズされたラベルで表示されます。 |
| Navigation | definition、declaration、type definition、implementation。 |
| Symbols and ranges | nested document symbols、token・line・document の selection range。 |
| Document links | quoted `import` と `#include` target への link。 |
| Folding ranges | block、連続 comment、import group の lexer-safe な folding range。 |
| Semantic tokens | full と full delta。range request は advertise されません。 |
| Signature and references | signature help、references、document highlight。 |
| Rename and formatting | prepare rename、rename、document formatting、physical line 単位に広げる range formatting、on-type formatting。 |
| Code action | `source.formatDocument`、重複 import を安全に削除する `source.organizeImports`、メソッド実装を生成する `refactor.rewrite`、`codeAction/resolve`。 |
| Workspace symbols | source、host、soft symbol を対象とした case-insensitive search。 |

## プロトコルのメソッド一覧

server は次の lifecycle / synchronization message を受け付けます。

- `initialize`、`initialized`、`shutdown`、`exit`;
- `textDocument/didOpen`、`textDocument/didChange`、`textDocument/didSave`、
  `textDocument/didClose`;
- `workspace/didChangeConfiguration`、`workspace/didChangeWorkspaceFolders`、
  `workspace/didChangeWatchedFiles`; そして
- pending request を cancel する `$/cancelRequest`。

feature request method は次のとおりです。

- `textDocument/completion` と `completionItem/resolve`;
- `textDocument/hover`、`textDocument/definition`、
  `textDocument/declaration`、`textDocument/typeDefinition`、
  `textDocument/implementation`;
- `textDocument/prepareTypeHierarchy`、`typeHierarchy/supertypes`、
  `typeHierarchy/subtypes`;
- `textDocument/documentSymbol`、`textDocument/selectionRange`、
  `textDocument/documentLink`、`textDocument/foldingRange`;
- `textDocument/semanticTokens/full` と
  `textDocument/semanticTokens/full/delta`;
- `textDocument/signatureHelp`、`textDocument/references`、
  `textDocument/documentHighlight`;
- `textDocument/prepareRename` と `textDocument/rename`;
- `textDocument/formatting`、`textDocument/rangeFormatting`、
  `textDocument/onTypeFormatting`、`textDocument/codeAction`、
  `codeAction/resolve`; そして
- `workspace/symbol`、`workspaceSymbol/resolve`。

## 文書の同期

text sync は incremental（`textDocumentSync.change = 2`）です。client から届いた順に
memory overlay へ適用し、full-text change と ranged change の両方を受け付けます。
LSP の line は zero-based、character は UTF-16 code unit です。

URI の各 open lifecycle 内で document version は monotonic です。最後に受理した version 以下の
`didChange` は無視されます。`didSave` は complete text を含めることができ、diagnostic
の前に overlay を更新します。`didClose` は overlay を削除し、その URI の diagnostic
を空にします。open buffer は import 解決時に disk file より優先されます。

`workspace/didChangeConfiguration` は設定を適用して open document を再解析します。
`workspace/didChangeWorkspaceFolders` は追加・削除された workspace root を適用します。
`workspace/didChangeWatchedFiles` は workspace symbol index を無効化し、disk の変更を
open document に反映するため再解析します。

`initialize.workspaceFolders`（または `rootUri`）が closed-file indexing の明示的な
root を定義します。open buffer は常に優先され、`maxWorkspaceFiles` と
`maxWorkspaceFileBytes` はその root 配下の closed file に適用されます。

## Tooling の意味論

- completion は同じ表示名の callable overload を1つの item にまとめ、detail に
  各 signature を1行ずつ表示します。call context で overload をまとめた item は、
  特定の overload の parameter を推測せず、`Select(${0})` のような空の argument stop を使います。
  overload のない callable は `Helper(${1:count}, ${2:name})` のような LSP snippet を含められ、generic type には
  `HostPair<${1}, ${2}>` のような snippet を含められます。generic completion の置換範囲の後ろに
  `<` がある場合（スペースまたは tab を挟む場合を含む）、generic snippet を省略して型名だけを挿入し、
  `<` の二重挿入を避けます。completion target の後ろに `(` がある場合、callable completion は
  snippet を省略して呼び出し構文の重複を避けます。
  client が `textDocument.completion.completionItem.snippetSupport` を true にした場合に限り、
  snippet 形式の挿入文字列を返します。未対応の場合は通常の文字列を返します。client が snippet に対応する場合、
  call context で variadic な host formatter は統合された overload 表示から placeholder を推測せず、
  `print(${0})` のような空の引数 tab stop を使います。未対応の場合は関数名だけを挿入します。
- 修飾 namespace からの completion は `label` に末尾の名前を保持します。callable または generic type の
  snippet が使え、client が snippet 対応を通知した場合は `insertText`（存在する場合は `textEdit.newText`）に使います。
  それ以外は末尾の名前だけを挿入します。container は標準の
  `labelDetails.description` に入ります。任意の `$valencePresentation` metadata は
  Valence に最終的な label/declaration と UTF-16 semantic span を提供します。他の LSP
  client は無視でき、filtering、sort order、挿入 text は変更しません。
- namespace segment の completion は末尾の名前を `label` と `filterText` に保持します。
  expression/statement site で namespace を確定すると、置換範囲の直後に `::` がない場合は挿入文字列に追加します。
  既に `::` がある場合と `using namespace` の補完では名前だけを挿入します。
- completion は text match quality を contextual priority より先に並べます。順序は
  exact、case-insensitive exact、prefix、word-boundary/camel-case、contained、
  subsequence です。`sortText` はこの完全な順序を持ちます。先頭 item の `preselect` は、
  exact/case-insensitive-exact/prefix tier が他の全 item より厳密に良い場合だけ設定されます。
- primitive/container word は built-in type です。primitive spelling は completion、hover、
  semantic token で `keyword` のままですが、container spelling は `type`/type alias です。
  両者とも localized hover detail を返し、source definition はなく、rename はできません。
- `completionItem/resolve` は元の item と既存 documentation を保ちます。documentation がなく `detail` がある場合に限り、detail を Markdown として追加します。
- Hover はソース宣言の直前にある連続コメントを読み取ります。ソース定義されたプリプロセッサーマクロの補完項目には直前のコメントを含められます。host 補完文書は binding metadata から取得します。Hover は `@param`、`@return`、`@returns` をローカライズされたラベルで整形します。
- user namespace と予約された `std::` tree は hover、navigation、references、
  rename、outline、semantic token で namespace symbol として扱われます。standard
  library namespace は rename できません。
- `textDocument/declaration` は definition と同じ location です。type definition は
  class、struct、interface、enum、type alias の declaration を探し、不完全な buffer
  では outline と declaration-head recovery を使用します。implementation は派生 type と
  override member の implementation location を返します。対象になるのは、parameter と
  `const` qualifier が一致する、`final` ではない virtual、`override`、abstract member
  だけです。type name が曖昧な場合は除外され、definition の代替にはなりません。
- selection range は token、line、whole document の順に parent を持ちます。
  document link は quoted `import` と `#include`（任意の `export` prefix を含む）を
  現在の document から解決します。
  コアの module resolver を使い、現在のディレクトリ、設定した検索パス、標準ライブラリと
  未保存の開いている document を参照します。未解決の target にはローカルの link を残します。
- semantic-token delta は previous result が不明または古い場合は全 data を返し、
  一致する場合は最小の連続差分 edit と新しい result id を返します。
- semantic token legend の全種類は `namespace`、`type`、`class`、`enum`、
  `interface`、`struct`、`variable`、`property`、`enumMember`、`function`、
  `method`、`macro`、`keyword`、`decorator`、`decoratorArgument`、`constant`、
  `label`、`string`、`number`、`comment`、`preprocessor` です。lexical baseline は
  language/type word、directive head、macro name も分類します。これは重複する LSP span を
  作らず有効なすべての physical line を対象にし、不完全な入力から declaration analysis が
  復旧している間も利用できます。
- range formatting は安全に完全な physical line まで広げ、外側 block の indent を保ちます。
  document 全体・範囲・入力中の各フォーマット要求は `insertSpaces` に従います。`false` では
  インデントレベルごとに tab を1つ出力し、`true` では `tabSize` 個の空白を出力します
  （最大16、未指定または無効の場合は4）。
- code action は `context.only` が除外しない限り `source.formatDocument` を返します。
  完全一致する重複 import の行を安全に特定して削除できる場合に限り、該当行だけを削除する
  `source.organizeImports` も返します。
- class または struct の本文で有効な空の cursor 位置では、completion はアクセス可能で、
  まだ final ではなく未実装の継承 virtual member と、実装が必要な pure interface member、
  または override 可能な virtual default member を提案します。
  generic signature は派生型に合わせて具体化されます。候補を選ぶと編集可能な `throw 0;` body
  を持つ method を挿入し、pure interface の実装には `override` を付けません。prefix を入力中の
  completion も対応します。空の cursor 位置では同じ候補が `refactor.rewrite` code action としても
  表示されます。`context.only` は `refactor` または `refactor.rewrite` で action を絞り込めます。
  snippet 非対応 client には通常の method text を返します。
- workspace symbol の query は大小文字を区別せず、空白で分割します。symbol name、
  qualified name、container、detail、file label を検索し、`::` と旧来の `.` の区切りを
  同じものとして扱います。完全一致・prefix 一致が fuzzy subsequence 一致より先に並び、
  各 item には安定した `data.veroscriptSymbolId` が含まれます。`workspaceSymbol/resolve`
  は detail と container を更新し、ID が不明なら元の item をそのまま返します。
- document symbol と workspace symbol は namespace container を公開します。同じ declaration
  identity が複数の import 経路から見つかっても、結果には 1 回だけ含まれます。source file
  のない host symbol は `veroscript:host-bindings` URI と zero range を使用します。

open、受理された change、save、configuration change の後に
`textDocument/publishDiagnostics` を送ります。import file を含めて file ごとにまとめ、
空の配列で古い marker を消去します。各 diagnostic には LSP severity、message、
利用可能な場合は stable code、および `source: "veroscript"` が含まれます。source
file を持たない host symbol は `veroscript:host-bindings` URI と zero range を使用します。

該当する結果がない場合、server はその LSP method に対応する null または empty result
を返します。未対応 method は JSON-RPC `-32601`、document または position を要求する
method の不正な parameter は `-32602` を返します。

## ローカライズ

server は CLI と runtime と同じ localization policy を使用します。`initialize.locale`
は表示だけを変更し、script semantics や symbol resolution は変更しません。client は
翻訳済み text ではなく message ID/code を logic の境界にしてください。不足した file
または key は `en-US` に fallback します。[Diagnostic localization](../../../../../VeroScript/Resources/Docs/User/ja-JP/localization.md)
を参照してください。

## 関連リファレンス

[Tooling モジュール](index.md)、[CLI リファレンス](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md)、
[Host bindings](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/bindings.md)、[Debugging](debugging.md) も参照してください。

## ホストが提供するモジュールソース

ホストは解決済みのモジュール環境とコンパイルプロセッサを提供できます。標準の Runtime サーバーは CLI のパッケージ選択やアプリケーションのネイティブコールバックを自動では引き継ぎません。編集中のプログラムに合わせてインポート、バインディングカタログ、ホストサービスを設定してください。カタログは説明を提供するもので、実行可能なプロセッサではありません。

`capabilities.experimental.veroscriptModuleEnvironment` が `moduleSourceMethod` を通知した場合、対応クライアントはそのメソッド（`veroscript/moduleSource`）を使って `veroscript-module:` の参照先を読み取り専用で開けます。これは編集可能なワークスペースファイルではなくスナップショットです。サーバーの再起動やコンパイルフィンガープリントの変更後は、元のソースから再度移動してください。存在しない、またはアクセスできないモジュールは `null` を返します。通常のディスク上のソースには引き続きファイル URI を使います。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/lsp.md) · [fr-FR](../fr-FR/lsp.md) · [ja-JP](lsp.md) · [ko-KR](../ko-KR/lsp.md) · [zh-CN](../zh-CN/lsp.md) · [zh-TW](../zh-TW/lsp.md)
