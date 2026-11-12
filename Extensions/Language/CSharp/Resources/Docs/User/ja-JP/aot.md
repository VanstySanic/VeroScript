# C# AOT

`veroscript aot --target=csharp` は、検査済み VeroScript を C# ソースへ
変換します。生成アプリケーションは通常の .NET ビルドまたは NativeAOT
publish が可能で、実行時に `VeroScriptInterop` を必要としません。これは
組み込み用の `VeroScriptCSharp` SDK とは別の境界です。

## 単一ファイル

```powershell
veroscript aot --target=csharp app.vs -o Program.cs
```

生成物は実行ファイルではありません。`net8.0` または `net9.0` を対象とする
SDK-style project に追加して `dotnet` でビルドしてください。必要なら
`-I`/`--import-path` でアプリケーション module の検索先を追加します。

## ソース bundle

```powershell
veroscript aot --target=csharp app.vs --output-dir generated --bundle-name App
dotnet publish generated/App.csproj -c Release -r win-x64 --self-contained true
```

bundle には `App.runtime.g.cs`、`App.program.g.cs`、`App.entry.g.cs`、
`App.csproj`、`App.manifest.json` が含まれ、manifest がファイル一覧と順序
を固定します。

<a id="aot-project-inputs"></a>

### 生成 project の host 入力

生成 `.csproj` は `EnableDefaultCompileItems=false` なので、明示した `Compile` 項目だけをコンパイルします。元の managed host 宣言と AOT bridge を追加入力として指定します。ファイルを隣に置くだけでは含まれません。bridge の partial class 実装は生成プログラムと同じ assembly でコンパイルします。manifest の全生成単位を維持してください。VM 登録ソースは別の SDK 統合成果物です。

bundle 再生成は project file を置換します。追加 `Compile` 項目を再適用するか、application が所有する project から生成単位と host 入力を参照します。具体的なパスと操作順序は[バインディングチュートリアル](../../../../../../../Resources/Docs/Tutorials/ja-JP/csharp/01-csharp-source-tools.md)を参照してください。

## 対応範囲と制限

scalar、string、`pair`、`auto`、各種 container、free function、制約のない
open generic、default、variadic、overload、`ref`、namespace、`using namespace`、
enum、global、import、制御構文、`switch`、jump、`throw`、
`try`/`catch`/`finally`、`algorithm`/`numeric`/`random` をサポートします。
さらに function value と indirect call、expression/block lambda と capture、
script class/struct、constructor、instance/static/virtual method、single
class inheritance、reference return、local reference alias に対応します。

signed 8-bit `char` literal、same-type local declaration group、`do`/`while`、
classic `for` の initializer/step expression list も含まれます。通常の string と
raw string は `\xNN` で導入された isolated byte を含め、VeroScript の byte sequence
を正確に保持します。Unicode newline scalar は escape として出力され、generated C# の
literal や source-location comment を無効にしません。

generic type parameter と、lexically shadowing する local、field、loop/catch binding、
lambda parameter には hygienic な C# 名が付与されます。initializer は新しい binding が
visible になる前の VeroScript environment に対して出力され、C# では自己参照として bind
されたり nesting が拒否されたりする場合でも VeroScript の意味を保ちます。

open generic 型の constructor field initializer list では、C# ターゲットが
サポートするのは `T` から `T` への直接初期化だけです。制約のない型パラメーターを
0 個または複数の引数で構築する形式は AOT 生成時に拒否されます。具体的なフィールド型を
使うか、事前に構築した `T` 値を渡してください。

lambda がインスタンスメンバーを使用する場合、キャプチャーされた class の
`this` はレシーバーを保持して後のフィールド変更を観測します。一方、struct の
`this` は値のスナップショットです。ネストした lambda はすでにキャプチャーされた
レシーバーを引き継ぎます。キャプチャーなしの lambda は `this` を使用できません。

Binding Catalog v1 の互換な class、struct、enum、member、container は
`--binding-catalog` と生成された AOT bridge で直接 managed call に lowering
できます。runtime reflection は使用しません。

## 明示的な除外

interface と interface implementation、複数の concrete class base、non-public
または struct inheritance、deterministic destructor、friend/using-member、
constraint/default generic parameter、const または reference direction だけが
異なる overload、precompiled body、map destructuring、reference `foreach`、
fixed-size native array、host operator/indexer、generic host member、host
property increment/decrement、element conversion が必要な host container は
拒否されます。意味を変更する代わりに診断を返します。

`concurrent.vs`、`channel<T>`、`task<T>`、`task_group<T>` は C# AOT では未対応
です。runtime concurrency、host binding、reflection、interception、JIT が
必要な場合は [C# SDK](index.md) と native VM を使用してください。

対応する container には通常の map、ordered map、set、queue、stack、deque に
加えて `multi_map`、`ordered_multi_map`、`multi_set`、`ordered_multi_set`、
`priority_queue` も含まれます。

C# AOT の浮動小数点数から整数への変換はゼロ方向に切り捨て、キャスト前に二進数の値が対象範囲内であることを確認します。表現可能な 64 ビット整数値を十進数への丸めなしに保持します。符号なしの変換先では負の小数を含む負の入力を拒否し、NaN、無限大、範囲外の値も失敗します。

<a id="aot-floating-conversions"></a>

C# AOT は整数から float への変換でコアと同じ double 経由の変換を維持し、混合演算と複合代入にも適用します。浮動小数点数を含む比較は double のオペランドで行い、浮動小数点数の剰余はコアと同じく整数のみで定義されるというエラーを返します。

## ランタイムのメモリ機能

独立 AOT は現在、`std::memory` の照会、バッファー、ビュー、および関連する型に対応していません。実際の呼び出しやこれらの型の使用には明示的な生成エラーを返します。未使用の `memory.vs` または `core.vs` の import は引き続き可能です。

[Runtime SDK](index.md)

## 宣言の準備とキャプチャの制限

C# 生成は Core の宣言準備を使用し、クラス外の定義と生成されたメソッドも処理します。ネイティブホストは `FCSharpCodegenBackend` の `FCodegenOptions.AnnotationProcessing` でアノテーションプロセッサーを指定できます。プロセッサーのコードは生成中に有効である必要があります。

値キャプチャしたバインディングは既定で読み取り専用です。`mutable` ラムダは所有するキャプチャへの変更を次回以降の呼び出しにも保持します。関数値のコピーは同じ環境を共有し、別々の評価で作成したクロージャは独立した環境を持ちます。値の初期化キャプチャは外側のスコープで左から右へ一度だけ評価され、通常の名前による const バインディングのキャプチャは読み取り専用のままです。スカラーのローカル `static` は初回使用時に初期化され、同じラムダ式から作成したクロージャ間も含め宣言ごとに共有されます。ジェネリックの各インスタンスと生成エントリーポイントの各呼び出しは状態を分離します。初期化失敗後は再試行でき、再帰的な初期化はエラーになります。参照の初期化キャプチャ、非スカラーのローカル static、入れ子のラムダから外側の関数のローカル static へのアクセスは Core ランタイム経路が必要で、独立した C# 生成では明示的に拒否されます。

独立 C# AOT はコンソール能力の問い合わせ、寸法、色、カーソル、画面消去をサポートします。出力をリダイレクトすると制御能力はなく、エスケープシーケンスも出力しません。エントリの実行境界で Windows コンソールの元の属性とカーソル状態を復元します。他の端末ではスタイルや表示状態を変更した後にスタイルをリセットしてカーソルを表示し、スクリプト失敗時も同じクリーンアップを行います。

C# AOT は関数シグネチャ内の等価な基本型エイリアスに同じ生成デリゲート型を使用します。参照パラメーター、参照戻り値、ネストした値コンテナーも対象です。`long`/`int64` と `unsigned long`/`uint64` は等価ですが、符号、幅、オブジェクト型の識別、`ref`/`const ref` 修飾は区別されます。

## ユーザー定義 pair テンプレート

ユーザー定義の `pair<T, U>` は通常のジェネリックフィールドとメソッドを使用します。この2引数テンプレートがソフト組み込み型を置き換える場合、単体 AOT はマップの `entries()` とキー・値ペアの反復を明示的に拒否します。これらの操作には VM バックエンドを使用してください。ユーザー定義 pair 型をマップの値として反復することは引き続き可能です。

オプションを受け取るファイル生成入口（`FCSharpCodegenBackend.EmitFromFile`、`EmitCSharpSourceBundleFromFile`）は、Core のモジュールローダーでエントリーとインポートを読み込みます。不変のモジュール環境と `SourceOverlays` は仮想エントリーパスにも適用されます。対象ランタイムのアダプターが必要なネイティブパッケージ契約は、独立した出力では明示的に拒否されます。

オプションを受け取る `EmitCSharpSourceBundleFromProgram` オーバーロードも、モジュール環境の公開バインディングカタログを使用し、競合する明示的なカタログを拒否します。

生成されるエントリは、`main` より前の実行エラー処理境界内で、コンパイラが記録した順序に従ってスクリプトの静的データメンバーとグローバル変数を初期化します。初期化中の例外と `exit` はエントリ実行と同じ規則で処理します。初期化前の `int` 以外のストレージの読み取りや参照には Core ランタイムが必要です。その未加工のゼロ値を、型付き C# フィールドで忠実に表現できないためです。起動時の検査は、静的に特定できる呼び出しと直接の呼び出し可能値の束縛を追跡します。このようなストレージが未初期化の間は、対象を特定できない起動時の間接呼び出しを拒否します。遅延実行する呼び出し可能値の作成だけであれば引き続き対応します。直線的な処理での書き込み後の読み取りと、通常の `int` のゼロ値の前方読み取りにも対応します。

Core によるコンパイルが成功した後、起動時の関数呼び出しと型初期化のグラフが深すぎる場合は、Core ランタイムを必要とする
診断を返して生成を拒否します。これにより生成器のネイティブ検査スタックを保護します。
通常の再帰と逐次的な起動時呼び出しは引き続き対応します。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
