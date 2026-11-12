# C# トランスパイラー

`VeroScriptCSharpTranspiler` は、厳密に静的解析できる C# 12 サブセットを
VeroScript ソースへ変換します。`VeroScriptCSharpFrontend` と Roslyn の
意味情報を利用し、テキストのトークン置換は行いません。C# runtime SDK、
BindGen、VeroScript から C# への AOT backend とは別のソース変換ツールです。

## 単一ファイルの変換

```powershell
veroscript-csharp-transpiler Program.cs -o Program.vs `
	--source-map Program.vs.map.json `
	--veroscript veroscript
```

`--veroscript` は、生成ソースの検証に使用する VeroScript executable または
SDK root を指定します。検証は既定で有効です。VeroScript ファイルと source
map は、解析、lowering、検証、出力 staging がすべて成功した後にだけ書き込ま
れます。変換に失敗した場合、既存の出力は置き換えられません。

公開 library API は `CSharpTranspiler.TranspileSource` と
`CSharpTranspiler.TranspileFile` を提供します。結果には生成ソース、source-map
JSON、`CSharpDiagnostic` の一覧が含まれます。`CSharpTranspileOptions` では検証、
validator、生成先、timeout、C# parse options を指定できます。

## プロジェクトの変換

```powershell
veroscript-csharp-transpiler Models/User.cs Services/UserService.cs `
	--project-root . `
	--output-dir generated/veroscript `
	--veroscript veroscript
```

プロジェクト mode はすべての入力を 1 つの C# compilation として解析し、
ファイル間の symbol を解決し、`--project-root` からの相対パスを保ったまま
明示的な import を生成します。入力ごとに VeroScript source と `.map.json`
source map を 1 つずつ出力します。既定の manifest は
`transpile.manifest.json` で、`--manifest` で変更できます。生成ファイルと
manifest は 1 つの出力セットとして commit され、staging に失敗すると rollback
されます。

library API のプロジェクト入口は `CSharpTranspiler.TranspileProject` です。
`CSharpTranspileProjectOptions` で設定し、結果から生成ファイル、deterministic
manifest JSON、diagnostics を取得できます。各入力は project root の内側にあり、
同じパスを重複して指定できません。

出力される相対名が衝突する場合、後の名前には決定的な path-hash suffix が付加されます。

## コマンドラインオプション

| Option | 範囲と意味 |
| --- | --- |
| `-o`, `--output` | 単一ファイルの出力先。単一ファイル mode では必須で、プロジェクト mode では使用できません。 |
| `-h`, `--help` | usage を表示して終了します。 |
| `--version` | transpiler の version を表示して終了します。 |
| `--source-map` | 単一ファイルの source-map 出力先。既定は `<output>.map.json` で、プロジェクト mode では使用できません。 |
| `--output-dir` | プロジェクト mode を有効にし、生成先 directory を指定します。 |
| `--manifest` | プロジェクト manifest の出力先。既定は output directory 内の `transpile.manifest.json` です。 |
| `--project-root` | project-relative な出力名を計算する root。すべての入力はその内側に必要です。 |
| `--reference` | metadata assembly を追加します。複数指定できます。 |
| `--language-version` | parse に使用する C# language version を選びます。 |
| `--veroscript` | validator として使う VeroScript executable または SDK root を指定します。 |
| `--timeout` | 生成ソースの検証 timeout（秒）。既定値は 120 です。 |
| `--no-validate` | コンパイラー検証を省略します。別の明示的な工程が生成ソースを検証する場合に限り使用します。 |

入力が複数ある場合、または `--output-dir` / `--manifest` がある場合は
プロジェクト mode になります。プロジェクト mode では必ず `--output-dir` を
指定します。

## 出力メタデータ

source map には source path、generated path、mapping 配列が含まれます。各
mapping は generated line と対応する source path、line、column を示します。

```json
{
	"version": 1,
	"source": "Program.cs",
	"generated": "Program.vs",
	"mappings": [
		{
			"generatedLine": 1,
			"sourcePath": "Program.cs",
			"sourceLine": 1,
			"sourceColumn": 1
		}
	]
}
```

プロジェクト manifest の安定したフィールドは次のとおりです。

```json
{
	"version": 1,
	"kind": "veroscript.csharp-transpiled-project",
	"projectWideAccess": true,
	"files": [
		{
			"input": "Models/User.cs",
			"output": "Models/User.vs",
			"sourceMap": "Models/User.vs.map.json"
		}
	]
}
```

manifest 内の path は project-relative で、区切り文字は `/` です。同じ変換で
生成した manifest、source、source map を一緒に扱ってください。

## 対応する C# の範囲

次の構文を含むサブセットを対応します。

- namespace、enum、class、struct、および namespace レベルの宣言として使う
  static class;
- public、protected、private の field、対称な auto-property、constructor、
  static/instance member、method;
- 制約のない generic type と free function、primitive type、VeroScript 標準
  container type;
- local inference、call、object/collection creation、index、cast、算術、代入、
  条件、`do`/`while` と複数の宣言または反復式を持つ classic `for` を含む loop、
  `foreach`、`switch`;
- 通常の `throw`、`try`、`catch`、`finally`;
- type alias、constant local、`params`、delegate/function type、expression/block
  lambda;
- reference-returning method、reference local、label、`goto`。対応する
  VeroScript の aliasing と control-flow 形式へ lowering されます。

`Console.Write` と `Console.WriteLine` の改行動作の違いは保持されます。対応する
lambda では C# の by-reference closure の動作も保持されます。
string literal は NUL と対応するほかの control character を保持します。unpaired
UTF-16 surrogate は、VeroScript に lossless な UTF-8 表現がないため安全に拒否されます。

明示的な generic member invocation は、表現可能な場合に具体的な型引数を保持します。
たとえば `Helpers.Pick<long>(1)` は `Helpers::Pick<int64>(1)` になります。
nested type declaration は曖昧な VeroScript を生成しないよう diagnostic で拒否されます。
transpile する場合は namespace scope に移してください。

## 明示的な制限

意味を忠実に表現できない機能は、暗黙に近似せず `VSCT2001` diagnostic で拒否されます。
現在の制限には次が含まれます。

- `async`/`await`、task、iterator、LINQ query syntax;
- delegate declaration と event;
- nullable reference semantics、unsafe/pointer code、reflection、`dynamic`;
- generic constraint と interface;
- `catch` filter;
- asymmetric または computed property;
- constructor chaining;
- 複数ファイルに分割された partial type。

`--reference` はメタデータアセンブリの追加に繰り返し使用でき、`--language-version` は C# の構文バージョンを選択します。どちらも対応する VeroScript の意味を拡張しません。`--timeout` は生成ソースの検証時間を制限します。`--no-validate` は、別の明示的な工程がコンパイラー検証を行う場合に限り使用してください。ソース生成の成功だけでは出力を実行できると判断できません。

## 整数の変換と数値の対応範囲

Transpiler は整数の加算、減算、乗算、符号反転、増減、複合代入、整数キャストについて、解決された幅で回り込む C# の既定 unchecked 動作を保持します。シフト数は31または63でマスクします。複合代入は右辺より先に対象とその以前の値を一度だけ評価し、自動プロパティはフィールドとして出力します。整数の除算と剰余は、除数ゼロまたは符号付き最小値/-1 のオーバーフローで捕捉可能なスクリプト例外を送出します。整数値の `double` 定数も浮動小数点型を保持します。

<a id="numeric-and-console-boundaries"></a>

明示的な `checked`/`unchecked`、非定数の浮動小数点除算・剰余、非定数の浮動小数点から整数への変換、有限でない浮動小数点定数は、検証を無効にしても `VSCT2001` で失敗します。有限のコンパイル時数値定数は Roslyn で評価します。解析は既定の unchecked 設定を使用し、利用側プロジェクトのオーバーフロー検査オプションは取り込みません。

enum の整数キャストは宣言された基礎型の幅で回り込みます。符号なし64ビット enum、enum の算術・ビット演算による変更、非定数の `double` から `float` への縮小、非定数の64ビット整数から `float` への変換は `VSCT2001` で失敗します。Console は文字列・整数・bool の単一引数と引数なしの `Console.WriteLine()` に対応します。`Console.Write` は改行しない `std::console::write`、`Console.WriteLine` は改行する `std::print` を使用します。bool の文字列表現は連結と `+=` でも `True`/`False` です。標準関数を修飾することで、同名のソース関数による出力や整数文字列変換の横取りを防ぎます。書式付き・複数引数の Console オーバーロード、および浮動小数点・enum・オブジェクト・配列の書式変換は CLR の文字列規則を変更せず、明示的に拒否します。

C# トランスパイラーは整数と浮動小数点数の混合比較前に Roslyn のオペランド昇格を適用し、コアの比較前に C# の float 丸めを維持します。

数値の条件分岐には Roslyn が選択した暗黙の変換を適用し、整数の幅と float の丸めを維持します。選択された分岐のみを評価します。条件分岐内でも、定数でない 64 ビット整数から float への変換は拒否します。

## リンクを含む出力パス

Windows の `\\?\`、`\\.\` およびボリューム GUID のパス別名は、最終的なファイルシステム上の位置で比較します。この別名で検証を回避することはできません。別々の出力パスは引き続き使用できます。

BindGen と Transpiler CLI は出力を準備する前に既存のディレクトリとファイルのリンクを解決します。symlink や Windows junction を経由して入力ソースを上書きしたり、同じ出力を二重に指定したりすることはできません。リンクされたディレクトリ内の別々の出力は使用できます。

`--timeout` と `ValidationTimeout` は呼び出しごとに、validator の終了と標準出力・標準エラーの読み取りを制限します。ランチャーが終了しても子プロセスがリダイレクトしたパイプを保持する場合、期限が切れると `VSCT2004` で検証に失敗します。検証失敗時は CLI の出力ファイルを置換しません。

静的フィールドと自動プロパティの初期化子は、省略またはコンパイル時定数に限られます。非定数の静的初期化子と静的コンストラクターは、検証を無効にしても `VSCT2001` で拒否されます。VeroScript のグローバル初期化では、CLR の型初期化の時点、副作用、依存関係の順序を維持できないためです。動的な静的状態は `Main` またはアプリケーションが制御するメソッドで明示的に初期化してください。

文字列、配列、対応するコレクションのフィールドと自動プロパティには、明示的な非 null 初期化子が必要です。対応するコレクションは `List<T>`、`Dictionary<TKey, TValue>`、`SortedDictionary<TKey, TValue>`、`HashSet<T>`、`Queue<T>`、`Stack<T>` です。初期化の省略とこれらの型への null 代入は、検証を無効にしても `VSCT2001` で拒否されます。CLR の null 参照を空の文字列やコンテナーに置き換えると、等価比較と例外の動作が変わるためです。対応する非 null 配列およびコレクションと null の等価・不等価比較は引き続き利用できますが、文字列と null の比較は対応範囲外です。

## 出力ファイル名の衝突

Transpiler のプロジェクト出力名は、すべてのホストで大文字と小文字を区別せず、Unicode NFC 正規化後に比較されます。割り当てはパス順です。衝突時は決定的なパスハッシュを付加し、その名前も使用済みなら数値の接尾辞を追加します。実際のファイル名は推測せず、返されたレコードやマニフェストで確認してください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/transpiler.md) · [fr-FR](../fr-FR/transpiler.md) · [ja-JP](transpiler.md) · [ko-KR](../ko-KR/transpiler.md) · [zh-CN](../zh-CN/transpiler.md) · [zh-TW](../zh-TW/transpiler.md)
