# Tooling workflow: editor と debugger を接続する

この tutorial では、VeroScript project を Language Server と debug adapter に
接続します。CLI を確認し、LSP を設定し、editor assistance を試し、最後に DAP
session を開始します。

## Step 1: CLI を確認する

同じ Runtime リリースの CLI と VM、対応する言語サーバー、LSP と DAP に対応したクライアントを使います。以下の 2 ファイルを、書き込み可能な同じ作業ディレクトリに保存します。そのディレクトリでコマンドを実行し、エディターでもワークスペースとして開きます。

### hello.vs

```VeroScript
import "lesson-utils.vs";

namespace Lesson
{
	int twice(int value)
	{
		return value * 2;
	}
}

int main(string[] args)
{
	std::chrono::now();
	int value = Lesson::twice(lessonSeed());
	print("value={}, argc={}", value, len(args));
	return 0;
}
```

### lesson-utils.vs

```VeroScript
export int lessonSeed()
{
	return 21;
}
```

```powershell
veroscript --version
veroscript --check hello.vs
veroscript --vm-backend=vs-bytecode --backend=vm hello.vs red blue
```

チェックはスクリプトを実行せずに成功します。実行時は `value=42, argc=2` が表示され、終了コードは 0 です。

ここで失敗する場合は editor を設定する前に修正してください。CLI と LSP の結果が
違う場合、module path、macro、catalog の設定が異なることが多いです。

## Step 2: Language Server を起動する

editor から version が一致する `veroscript-lsp` を標準入出力で起動します。
editor は `initialize` の response を待ってから `initialized` を送ります。project
root を workspace root として渡してください。

以下のフィールドを、ワークスペースやクライアントの機能情報と一緒に `initialize` リクエストの `params` に追加します。 `.` が作業ディレクトリを指すように、言語サーバーをそのディレクトリで起動します。エディターが別の場所からサーバーを起動する場合は、Windows の `C:/work/project` のように絶対パスを使います。別のプロジェクトで標準ライブラリーのディレクトリを明示する必要がある場合は `stdlibPath` を設定します。

```json
{
	"locale": "ja-JP",
	"initializationOptions": {
		"veroscript": {
			"importPaths": [
				"."
			]
		}
	}
}
```

client が watched file の dynamic registration に対応している場合、server は
`initialized` の後に `**/*.vs` と `**/*.vslib` の watcher を要求します。対応して
いない場合は client 側でこれらの pattern を設定するか、
`workspace/didChangeWatchedFiles` notification を明示的に送信してください。

`.vs` file を open すると buffer は memory overlay として解析されます。保存前でも
import を解決できます。古い version の incremental `didChange` は無視されます。

## Step 3: editor assistance を試す

ステップ 1 で保存した `hello.vs` を開きます。

1. syntax または type error を一時的に作って diagnostic を確認し、その error を元に戻します。
2. `std::chrono::now();` を `std` に置き換え、直後に caret を置いて completion を実行し、namespace を確定します。
   `std::` が挿入されます。
   `ch` を入力して `chrono` を確定すると `chrono::` が挿入され、member を確認できます。
   最後に行を `std::chrono::now();` にして、sample を有効なコードに戻します。置換範囲の後ろに
   `::` がない場合だけ、completion によって追加されます。
3. `Lesson::twice(lessonSeed())` を一時的に `Lesson::tw` に置き換えて補完を実行します。クライアントが `textDocument.completion.completionItem.snippetSupport` を通知している場合は、`twice` の引数のプレースホルダーを確認します。次に進む前に元の呼び出しに戻します。
4. namespace、function、type を hover します。
5. `hello.vs` の呼び出しから `lessonSeed` の定義に移動し、`lesson-utils.vs` が開くことを確認します。ドキュメントシンボルで `Lesson` と `main` を見つけ、`workspace/symbol` で `Lesson::twice` を検索して `workspaceSymbol/resolve` で結果を解決します。
6. 補完項目を解決して、利用可能なら Markdown の説明を確認します。`import "lesson-utils.vs";` 行のリンクを開きます。
7. semantic token を取得し、previous result id を使って semantic-token delta を
   取得します。
8. `Lesson` で名前変更を準備し、`Practice` に変更します。宣言と修飾名を使った呼び出しが一緒に変わることを確認します。
9. ファイル全体を整形し、選択範囲の整形では対象の行全体が更新されることを確認します。`source.formatDocument` を試します。`source.organizeImports` を試すには import 行を 1 つ複製し、重複が削除されることを確認します。両方のファイルを保存してから `veroscript --check hello.vs` を再実行します。

generic type と callable completion は、該当する context で LSP snippet を使えます。
server は client が `textDocument.completion.completionItem.snippetSupport` を通知した場合だけ
snippet を送信し、それ以外は通常の文字列を挿入します。standard-library namespace は
navigation できますが rename できません。code action は完全な format と、
完全に重複した import だけを削除する保守的な organize imports を提供します。

## Step 4: host catalog を追加する

ホストバインディングを使うプロジェクトの場合は、この手順を行います。練習用の 2 ファイルだけを使う場合はステップ 5 に進めます。

project が使用する catalog を `initializationOptions.veroscript.bindingCatalogPaths` に同じ順序で追加します。
function、type、member、annotation、generic template と documentation が analysis、
completion、hover、symbol に反映されます。

server 実行中に catalog を編集すると、次の request で reload され、open document が
再解析されます。catalog は metadata だけで、runtime implementation は登録しません。

## Step 5: diagnostic language を変更する

file を open する前に `initialize.locale` に BCP 47 tag を指定します。

```json
{
	"locale": "zh-CN"
}
```

新しい `locale` で言語サーバーを再起動します。ステップ 3 と同じ一時的なエラーを作り、診断コードと翻訳されたメッセージを比較します。コードは変わらず、翻訳がない場合は `en-US` が使われます。デバッグする前にエラーを修正してファイルを保存します。

## Step 6: debugger を起動する

DAP クライアントで接続方式を選びます。クライアントがアダプターを起動する場合は、作業ディレクトリで次のコマンドを使い、標準入出力で接続します。

```powershell
veroscript --debug hello.vs
```

クライアントを別に起動する場合は、作業ディレクトリで TCP アダプターを起動し、クライアントを `127.0.0.1:4711` に接続します。

```powershell
veroscript --debug --dap-port=4711 hello.vs
```

標準入出力の方式でスクリプトの既定の引数を指定するには、最初のコマンドを次のものに置き換えます。

```powershell
veroscript --debug hello.vs red blue
```

クライアントで作業ディレクトリを `cwd` に指定し、`hello.vs` を起動します。`print` の行にブレークポイントを設定し、起動時の `args` に `["red", "blue"]` を指定します。CLI で既定値を指定した場合は `args` を省略できます。ブレークポイントで停止したら再開し、デバッグ出力に `value=42, argc=2` が表示されることを確認します。リクエストの順序と設定は [デバッグリファレンス](../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/debugging.md)を参照してください。

DAP `launch` または `attach` では、`program` または inline `source` を指定し、
`args` を `main(string[] args)` に渡します。script の standard input には `input`、
environment override には `env`、working directory には `cwd`、standard-library root
には `stdlibSearchPaths` を使います。script input は DAP protocol stream とは独立して
います。相対 path は `cwd`、それがなければ `workspace` から解決されます。debugging、保持された初期化区間、有界クリーンアップは VM path を使います。
永続 Session の通常関数は選択した Mixed/JIT を使用できます。性能測定は通常実行で行ってください。

DAP の `initialize` argument には、diagnostic と error message の表示言語を指定する
BCP 47 `locale` を設定できます。client の分岐は翻訳 text ではなく、DAP の success と
error field を使用してください。

source line breakpoint は `setBreakpoints` で設定します。exception breakpoint と
function breakpoint には対応していません。これらの request は明示的に失敗するため、
client はその種類を active と表示しないでください。

`configurationDone` の後、wire host は request を acknowledge して VM を execution
worker で実行します。実行中の VM には `pause` で cooperative pause を要求でき、その後
の `stopped` event の reason は `pause` になります。`continue`、`next`、`stepIn`、
`stepOut` で再開し、`terminate` で cancellation を要求します。実行中も protocol は
responsive です。`disconnect` の default は launch では debuggee を terminate し、
attach では terminate しません。multi-session TCP host では `terminateDebuggee=false`
にすると listener が残ります。

実行中、`std::console::write` と `std::console::error` は改行なしで出力します。
`std::console::flush()` を呼び出すと進捗の断片をすぐに表示できます。アダプターは
プロセス終了前に標準出力とエラー出力を別々の DAP output event として転送します。

LSP session は `shutdown` の後に `exit` で終了します。

## 演習

1. CLI と LSP が同じ import を解決するようにします。
2. 新しい version の後に古い `didChange` を送り、新しい buffer が残ることを確認します。
3. catalog を追加し、host function が completion と hover に現れることを確認します。
4. language を変え、diagnostic code が変わらないことを確認します。
5. disk 上の imported module を変更し、`workspace/didChangeWatchedFiles` を送って
   open document の diagnostic が更新されることを確認します。

詳細は [Language Server reference](../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/lsp.md) と
[Tooling reference](../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/index.md) を参照してください。

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/toolchain/17-tooling-workflow.md) · [fr-FR](../../fr-FR/toolchain/17-tooling-workflow.md) · [ja-JP](17-tooling-workflow.md) · [ko-KR](../../ko-KR/toolchain/17-tooling-workflow.md) · [zh-CN](../../zh-CN/toolchain/17-tooling-workflow.md) · [zh-TW](../../zh-TW/toolchain/17-tooling-workflow.md)
