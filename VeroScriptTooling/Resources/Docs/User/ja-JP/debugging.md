# デバッグ

VeroScript デバッガーは、デバッグ アダプター プロトコル (DAP) を使用し、VM を通じてスクリプトを実行します。デバッグおよびホットリロード セッションでは、JIT 実行パスは使用されません。

## 標準入出力モード

```powershell
veroscript --debug script.vs
```

このモードは、標準入出力を介して DAP メッセージを転送します。これは、プロセスを起動および管理するエディターまたは別のデバッグ クライアントに適しています。

## TCPモード

```powershell
veroscript --debug --dap-port=4711 script.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 script.vs
```

`--dap-port` は、`1` から `65535` までのポートで TCP DAP サービスを開始します。 `--dap-bind` はバインド アドレスを選択し、デフォルトは `127.0.0.1` です。外部クライアントが本当に必要とする場合にのみ、非ループバック アドレスにバインドします。

`veroscript-dap` は、同じデバッグ ホストのシン エイリアスです。 IDE F6 の起動および接続セッションでも DAP が使用されます。セッションが切断されると、クライアントは `terminateDebuggee` でデバッグ対象を終了するかどうかを選択できます。

## Session lifecycle と launch/attach 引数

client は `launch` または `attach` の前に `initialize` を送信する必要があります。
adapter は `supportsTerminateRequest` などの capability と、`name`、`version`、
`platform`、`jitCapability` の metadata を返します。wire host は `initialize` response
の後に `initialized` event を送信します。

adapter が現在報告する DAP capability は次のとおりです。

| Capability | Value |
| --- | --- |
| `supportsConfigurationDoneRequest` | `true` |
| `supportsStepInRequest` | `true` |
| `supportsStepOutRequest` | `true` |
| `supportsStepBackRequest` | `false` |
| `supportsConditionalBreakpoints` | `true` |
| `supportsEvaluateRequest` | `true` |
| `supportsSetVariable` | `true` |
| `supportsTerminateRequest` | `true` |

`jitCapability` はコアの JIT サービス契約（現在は `backend-services`）を示し、
ネイティブ backend のインストールや選択状況を示すものではありません。

`attachMode: "domain"` と `domainId` は、host が事前準備した永続実行ドメインに接続し、
複数の script 呼び出しにわたってデバッグします。client は先に
`initialize.body.veroscriptSupportsDomainAttach` が厳密に `true` であることを確認してください。
それ以外は接続を拒否し、program の読み込みにフォールバックしてはいけません。
`configurationDone` は接続を有効にするだけで、script を読み込んだり実行したりしません。
対応する操作は attach response で確認します。既定の切断は host の方針に従って停止中の実行を
再開し、host を終了しません。実行中の JIT コードを途中から引き継ぐことはできません。
詳しくは[ドメイン契約](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/session-debugging.md)を参照してください。

以下の program 読み込みフローは `launch` と program モードの `attach`
（`attachMode: "program"` または省略）に適用されます。どちらも同じ debug loader で読み込みます。`attach` は
IDE が live binding を持つ embed または TCP host に接続するときに使用する request です。
すでに実行中の VM に attach するものではありません。breakpoint を設定した後、
`configurationDone` を送信すると実行が始まります。

`launch` と `attach` で使用できる引数は次のとおりです。

| Field | Meaning |
| --- | --- |
| `program` | script path。CLI debug command から default を指定できます。 |
| `source` | inline source text。空でなければ `program` より優先されます。 |
| `fileLabel` | inline `source` の表示 label と source identity。 |
| `workspace` | 相対 launch path の base directory。 |
| `cwd` | script の working directory と相対 path の解決先。`workingDirectory` も使用できます。省略時は利用可能なら `workspace` を使い、directory は存在している必要があります。 |
| `args` | `main(string[] args)` に渡す string array。script path は含みません。 |
| `input` | script の初期 standard input。DAP protocol stream とは独立しています。 |
| `env` | debug run 用の environment override。canonical 形式は object で、`environment` は alias です。`{ "name", "value" }` の array も受け付けます。 |
| `stdlibSearchPaths` | 順序付き standard-library search root。単一 root の互換 alias として `stdlibPath` も使用できます。 |
| `importPaths`、`implicitImportPaths` | 通常 import と implicit import の module search path。 |
| `defines`、`predefinedMacros` | preprocessor definition と host-style predefined macro。 |
| `sourceOverlays` | imported module の解決に使う memory source file。 |
| `bindingCatalogPaths` | metadata/stub binding として順番に読み込む catalog file。embed host の `LiveBindings` が指定されていれば、そちらが優先されます。 |
| `allowCircularImports`、`allowProjectWideAccess` | import と project visibility の policy。 |
| `stopOnEntry` | entry point で停止してから通常実行を続けるかどうか。 |
| `backend`、`maxCallDepth` | backend preference と call-depth limit。debug execution は VM path を使用します。 |

`program`、import、standard library、implicit import、overlay、binding catalog の相対
path は、`cwd` があればそこから、なければ `workspace` から解決されます。`workspace`
だけを指定した場合は、それが script の working directory にもなります。environment
override は debug session 中だけ有効で、終了後に復元されます。live binding を使う
`attach` session では environment override と working-directory override を同時に指定
できません。

CLI は script path の後ろに script argument を受け付け、DAP request の default にします。

```powershell
veroscript --debug hello.vs red blue
veroscript --debug --dap-port=4711 hello.vs red blue
```

明示した DAP `args` array はこの default を置き換えます。1 回の launch で script
argument は最大 4096 個、3 つの module-path array はそれぞれ最大 2048 個、environment
entry は最大 1024 個です。environment name は空、`=`、NUL を含めることができず、value
も NUL を含めることができません。

## 非同期実行と control

stdio と TCP の wire host では、`configurationDone` は VM の終了を待たずに acknowledge
され、VM は execution worker で実行されます。そのため protocol reader は control と
stopped-state request を処理し続けます。`stopped` event の `reason` は `entry`、
`breakpoint`、`step`、`pause` のいずれかです。実行中の VM を cooperative pause で停止
した場合は `pause` になります。

`continue`、`next`、`stepIn`、`stepOut` は `stopped` の後だけ有効で、実行を再開します。
`stackTrace`、`scopes`、`variables`、`evaluate`、`setVariable` も停止中だけ有効です。
breakpoint は `configurationDone` 前または停止中に設定してください。実行中の変更は
拒否されます。`pause` は VM 実行中だけ有効な cooperative stop request です。`terminate`
は cooperative cancellation を要求します。client は request response だけで終了と判断
せず、その後の `exited` と `terminated` event を待ってください。

`threads` request は id `1`、name `main` の論理 VM thread を 1 つ返します。これは
debugger の実行 view であり、wire host の protocol thread と execution worker thread は
script thread として公開されません。

`disconnect` の `terminateDebuggee` default は `launch` では `true`、`attach` では
`false` です。multi-session TCP host では `terminateDebuggee=false` は現在の client
session だけを終了して listener を残し、`true` は host listener を停止します。`terminate`
request は常に debug run を cancel し、host の停止を要求します。

## TCP host と embedding

CLI の TCP host は default で 1 session を処理します。SDK host は `VeroScriptDapTcp::Run`
と `FDapTcpRunOptions` を使用できます。`MaxSessions=0` は host が停止するまで client
を受け付け、`Port=0` は ephemeral port を選んで `OnListening` で通知し、
`BindAddress` は通常 `127.0.0.1` のままにします。embed host が
`FDapStdioRunOptions::LiveBindings` を設定すると、`attach` session は catalog stub では
なく登録済み host function を実行します。live catalog は DAP run より長く存続する必要があります。
embed host が readiness を自分で通知する場合は `bAnnounceListen=false` を設定できます。
default の listener 通知は host の error stream に書き込まれます。`VeroScriptDapStdio::Run`
は clean session で `0`、protocol または I/O error で `1`、client が host terminate を要求
した場合に `2` を返します。
`VeroScriptDapTcp::Run` では、client が host termination を要求した場合は正常終了として
`0` を返し、listen、accept、その他の異常な session failure では 0 以外を返します。

## ブレークポイントの対応範囲

adapter が対応するのは `setBreakpoints` による source line breakpoint です。
exception breakpoint と function breakpoint は未対応です。filter を含まない
`setExceptionBreakpoints` は標準 client の起動 handshake における clear 操作としてのみ成功し、
exception filter を含む request とすべての `setFunctionBreakpoints` request は failed DAP response
を返します。client はこれらを有効な breakpoint と表示しないでください。

`setBreakpoints` は 1 始まりの physical source line を使います。`launch` または `attach`
が debuggee を load する前は、返される breakpoint は `verified: false` の pending 状態です。
code の load 後、adapter はそれを bind し、同じ DAP breakpoint ID と
`reason: "changed"` を持つ標準の `breakpoint` event を emit します。verification には、
path が一意の compiled source に解決すること、要求行が preprocessing 後も active であること、
そして executable location があることが必要です。basename だけで file を match することは
ありません。bind できない場合は unverified のままで、localized `message` を返します。

function 内の non-executable physical line では、adapter がその function 内で次に executable な
physical line を bound line として返すことがあります。client は返された `line` を使ってください。
module initializer の breakpoint には exact physical line が必要です。preprocessing で除かれた行、
range 外の行、存在しないまたは曖昧な source は bind されません。

その他の未対応 DAP command も、localization 済みの error message を含む failed response
を返します。表示言語を選ぶには、DAP `initialize` arguments の `locale` に BCP 47 tag
を指定してください。client は翻訳済み message text ではなく、DAP の success/error
field で分岐してください。

## デバッグ中の出力

`std::console::write` と `std::console::error` は改行を追加せずに出力します。
`std::console::flush()` を呼び出すと、保留中の標準出力が直ちに表示されます。
DAP アダプターは、実行中の標準出力とエラー出力を別々の `output` イベントとして転送し、
最終結果でも 2 つのストリームを分けて保持します。

## 実行制約

デバッガーは、ブレークポイント、ステップ実行、変数検査に完全な VM デバッグ パスを使用します。 `--backend=mixed` または `--backend=jit` を要求しても、現在のデバッグ実行は JIT に切り替わりません。通常の実行では、デバッグ終了後に要求されたバックエンドを復元できます。

より詳細なデバッガとホスト バインディングの統合については、[ホスト統合](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/embedding.md) を参照してください。

## メモリの検査

停止中にオブジェクトや領域を展開すると、`[object_id]`、`[byte_length]` などの読み取り専用情報を確認できます。アドレスは許可時のみ表示します。ID は生存オブジェクトを識別し、アドレスは一時的な観察値です。検査情報へ代入はできません。DAP の `variablesReference` はプロトコルハンドルで、任意の `readMemory` / `writeMemory` は非対応です。

## デバッグフレームのソーススナップショット

スタックフレームが正の `sourceReference` を返した場合、クライアントは DAP の `source` でテキストを取得します。返されるのはインライン、生成、メモリ内コードのコンパイル済みスナップショットであり、ディスク上のファイルが古い、または存在しないソースオーバーレイも含みます。読み取り専用で表示してください。同名のディスクファイルを変更しても実行中のプログラムは変わりません。再開後も取得済みソースは読めますが、フレームと変数のハンドルは失効します。プログラムの再読み込みやクリアで古いソース参照も無効になるため、現在のフレームから取得し直してください。

## ネイティブデバッグホストのライフサイクル

所有スレッドでライフサイクル操作を直列化します。Run 実行中はコールバック内も含め LoadSource/LoadFile と入れ子の Run を拒否し、実行中のプログラムを維持します。Clear は何もしません。Run の復帰または例外処理後に Clear してください。引数は Run 開始時に固定され、コールバックからの変更は次回に適用されます。SetStopHandler や DAP イベントハンドラーの置換・解除は後続通知に適用され、現在の呼び出しとキャプチャーは復帰まで保持されます。並行アクセスは認められません。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/debugging.md) · [fr-FR](../fr-FR/debugging.md) · [ja-JP](debugging.md) · [ko-KR](../ko-KR/debugging.md) · [zh-CN](../zh-CN/debugging.md) · [zh-TW](../zh-TW/debugging.md)
