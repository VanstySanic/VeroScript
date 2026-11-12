# WebAssembly runtime と binding

単一行の表記も正確に保持します：`void main(string[] args)` と `int main(string[] args)`。
## ランタイムファクトリー

`createVeroScriptRuntime()` は interactive input 用 Asyncify-capable factory です。`createFastVeroScriptRuntime()` は whole-program Asyncify stack instrumentation を省き、run 前に全 input がある場合の preferred path です。capability flag が許す範囲で両 factory は同じ language/result contract を公開します。

`runSource` は module creation 後 synchronous です。`runSourceInteractive` は input provider が logical line を待つ間 VM/script state を suspend します。一つの Wasm module で active にできる interactive run は一つです。provider exception は runtime が安全に input wait を離れた後 promise を reject します。

[WebAssembly/WASM SDK](../../User/ja-JP/index.md#runtime-factory-configuration) を参照してください。

## input と result

run は file label、optional virtual file、standard input、script argument、selected `vm`/`mixed`/`jit` backend を受け取ります。`void main(string[]
args)` と `int main(string[]
args)` を support します。non-zero integer result も success run で `exitCode` として返します。embedded NUL input は reject します。

browser baseline は script-level worker thread を advertise しません。unbounded program は owning Web Worker を terminate して停止します。independent program は independent runtime module/worker を使用します。

[WebAssembly ABI、artifact、release 契約 — プログラムと入力の機能](release-contract.md#web-execution-policy) を参照してください。

フィールドとメソッドの実行可能なリフレクションは `vm`、`mixed` で動作します。公開ブラウザモジュールは pthread を使わないため、新しい callable タスクも従来のタスクも起動できません。`is_file`、`is_directory`、`remove_dir` が参照するのは、組み込み標準ライブラリを含むモジュール内の仮想ファイルシステムだけです。閲覧者のローカルファイルにはアクセスしません。

## コンパイル済みプログラム

`compileSource` は source file label/in-memory virtual file を受け result と optional owned `VeroScriptProgram` を返します。program handle は source graph を reparse せず input、argument、backend selection を変えて繰り返し run できます。各 run は fresh execution state で compilation diagnostic は compile result に残ります。

`VeroScriptProgram.dispose()` は native handle を release し SDK boundary で idempotent です。runtime は live program を track し runtime disposal で release します。interactive compiled execution には Asyncify artifact、`readLine` provider、同 WebAssembly module に他の active interactive run がないことが必要です。

## complete source と compiled-program surface

TypeScript SDK は runSource を `vero_web_run_source_with_context` に map し script argument、selected backend、paired in-memory virtual file を含めます。compileSource は `vero_web_compile_source_with_context` に map され、success result は VeroScriptProgram を own します。VeroScriptProgram.run は `vero_web_run_program`、runInteractive は Asyncify-only `vero_web_run_program_interactive` を使います。program run は fresh VM state で input、argument、backend を選べます。

native C++ surface には `FRuntime::RunSource`、`RunSourceWithInputStream`、`CheckSource`、`SetLanguage`、`GetAvailableLanguages` があります。C ABI は argument、option、context、compile、program、interactive、context-check entry point も公開します。C ABI array/string は call 中 borrowed、returned JSON/result pointer は runtime-owned lifetime rule に従います。VeroScriptResult は backend、output、error output、diagnostic、timing、program handle、per-run JIT statistic を別 field に保持します。

## result と JIT field mapping

`VeroScriptResult.jit` は検査、コンパイル、API エラー、またはソースの読み込み失敗時に null です。読み込みに成功した実行は、バックエンドなしでの明示的な `jit` 要求など、スクリプト初期化の前に拒否された場合も実行ごとのカウンターを返します。公開モジュールのカウンターはすべてゼロです。`directNativeCalls` と `compileAttempts` は統計値であり、言語や ABI のバージョンではありません。

## 管理メモリ

[WebAssembly/WASM SDK — 管理メモリ](../../User/ja-JP/index.md#managed-memory-contract) を参照してください。

## 失敗時の解放とスナップショット

入力ブリッジのメモリ確保失敗や Wasm32 サイズ上限の超過では、ネイティブスタックを再開して API 失敗結果を返し、モジュールは次の実行にも利用できます。故障注入は `vm` と `mixed` のソース実行とコンパイル済み実行を対象に、各失敗後の回復も検証します。通常の EOF の動作は変わりません。

失敗時も所有権を維持して解放します。対話入力行のコピーが例外を投げても入力領域は解放され、結果 JSON を生成できないコンパイル済みプログラムはハンドルを呼び出し元へ返す前に削除されます。読み取り専用 lease は独立したスナップショットを一度だけ生成し、`toArray` は全体を再コピーせずそのスナップショットを返します。回帰テストはスナップショットの独立した寿命と、コンパイル済み実行の失敗後に管理メモリのグローバル変数が再初期化されることを確認します。

## コアの互換性と診断

[WebAssembly/WASM SDK — コアの互換性と診断](../../User/ja-JP/index.md#compiler-diagnostics) を参照してください。

## 対話入力の所有権

対話呼び出しはオプションを読み取った後にモジュールの状態を再確認します。再入呼び出しや破棄済みランタイムの要求は入力プロバイダーを取得する前に拒否されるため、実行中のプログラムはコールバックと後続の入力を保持します。

文字列ポインターテーブルは実際に転送した項目数で確保し、独自の配列イテレーターや途中で変化する反復が初期の長さに基づく領域を超えて書き込むことを防ぎます。同期・非同期の両ヘルパーで Wasm32 の上限を検証します。ランタイムの破棄では追跡中のプログラムを直接走査し、別の配列を作りません。

[WebAssembly/WASM SDK — 対話入力の所有権](../../User/ja-JP/index.md#input-provider-failures) を参照してください。

## コンソール出力と機能

[WebAssembly/WASM SDK — コンソール出力と機能](../../User/ja-JP/index.md#console-capabilities) を参照してください。

無効な色名、消去モード、カーソル座標、引数の型や数は、端末機能が未対応でも診断を生成します。拒否後もランタイムは再利用できます。取得したテキストは実行完了時に `VeroScriptResult` で返されます。

## 現在の言語機能

[WebAssembly/WASM SDK — 現在の言語機能](../../User/ja-JP/index.md#compiler-and-application-services) を参照してください。

公開 C ABI と TypeScript オプションは、`FModuleEnvironment`、ネイティブ拡張の登録、リソースプロバイダー、`FEngineConfig::I18nServices` を公開しません。Core は独自の C++ WebAssembly ビルドで静的ネイティブ登録をサポートします。公開エンジンはデスクトップ用の動的ライブラリを読み込めません。これらのホスト連携には別途構成した成果物が必要です。既存のソースオーバーレイとプログラム所有権の契約は引き続き有効です。

## 注釈名の照合

[WebAssembly/WASM SDK — 注釈名の照合](../../User/ja-JP/index.md#annotation-name-matching) を参照してください。

受理する名前の範囲が重なり、定義が競合する注釈登録はコンパイル診断になります。両エンジンは検査、ソース実行、コンパイル済みプログラムの作成で同じ方針を適用します。コンパイル済みプログラムは定義の方針を保持するため、変更後は再コンパイルしてください。Web C ABI と TypeScript オプションは変わりません。ソースメタデータ契約の変更後は、古い開発版パッケージとキャッシュをソースから再生成してください。SDK のプログラムハンドルは引き続きメモリ内だけに保持されます。

## Core の実行と制限

[WebAssembly/WASM SDK — Core の実行と制限](../../User/ja-JP/index.md#numeric-and-library-limits) を参照してください。

コンパイル済みプログラムの繰り返し実行では内部領域を再利用できますが、失敗後も含め、毎回スクリプトのグローバル変数、ローカル変数、引数を新しく初期化します。Core の変更後は両方の Wasm を再ビルドして既存の SDK リリース検証を実行してください。過去の SDK 検証記録は、記録された成果物だけを対象にします。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/runtime-and-bindings.md) · [fr-FR](../fr-FR/runtime-and-bindings.md) · [ja-JP](runtime-and-bindings.md) · [ko-KR](../ko-KR/runtime-and-bindings.md) · [zh-CN](../zh-CN/runtime-and-bindings.md) · [zh-TW](../zh-TW/runtime-and-bindings.md)
