# WebAssembly/WASM SDK

`VeroScriptWebAssembly` は、ブラウザーなどの JavaScript ホスト向けの UI 非依存 WebAssembly SDK です。メモリ上のソースコードの検査と実行を提供します。画面表示、エディター、出力パネル、フレームワーク連携はホスト側で実装します。

<a id="runtime-factory-configuration"></a>

TypeScript ファクトリーは Emscripten インスタンスの生成前に呼び出し元の設定をコピーします。同じ設定オブジェクトは凍結済みでも再利用でき、各ランタイムのエクスポート、ヒープ、リソース所有権は独立します。継承された `locateFile` と `wasmBinary` の設定も保持されます。

## 成果物と配置

同じビルドから生成されたモジュール、WASM、宣言ファイルを一式として配置してください。

| 成果物 | 用途 |
| --- | --- |
| `veroscript-engine.mjs` | 対話入力に対応する Emscripten モジュール。 |
| `veroscript-engine.wasm` | 通常ランタイムの WebAssembly バイナリ。 |
| `veroscript-engine-fast.mjs` | Asyncify を使わない低遅延ランタイム。 |
| `veroscript-engine-fast.wasm` | 高速ランタイムの WebAssembly バイナリ。 |
| `veroscript-webassembly.ts` | TypeScript ラッパー。 |
| `veroscript-webassembly.d.ts` | ラッパーと WebAssembly C ABI の型宣言。 |
| `veroscript-engine.d.mts`、`veroscript-engine-fast.d.mts` | Emscripten モジュールオプションの宣言。 |

標準ライブラリとローカライズカタログは仮想ファイルシステムに組み込まれます。`.mjs`、`.wasm`、宣言、ABI は一致した組み合わせで使用し、生成済み成果物を直接編集しないでください。TypeScript のエントリポイントは選択した Emscripten エンジンを必要になった時点で読み込みます。ラッパーの import だけではどちらのエンジンもダウンロードされず、高速ランタイムは高速エンジンだけを、通常ランタイムは対話用エンジンだけを読み込みます。

## ランタイムの選択

非同期入力でスクリプトが停止する可能性がある場合は `createVeroScriptRuntime`、入力があらかじめ用意されている検査や実行には `createFastVeroScriptRuntime` を使います。

```TypeScript
const fast = await createFastVeroScriptRuntime();
console.log(fast.supportsInteractiveInput); // false
fast.dispose();

const normal = await createVeroScriptRuntime();
console.log(normal.supportsInteractiveInput); // true
normal.dispose();
```

高速ランタイムには完全な Asyncify 計装がないため、`runSourceInteractive` は利用できません。通常ランタイムは `readLine` の Promise が解決するまでスクリプトを停止します。

`read_line`、型付き入力関数、`pause` を使って非同期の入力を待つ場合は、通常のランタイムと `runSourceInteractive` を使用します。高速ランタイムは入力データがすでに用意されている場合に使用し、非同期入力を待つことはできません。

## バックエンドと API

`checkSource` は `main` を呼び出さずに source を検査し、`runSource` は検査後に
実行します。`supportsCompiledPrograms` と `supportsThreads` を使って artifact
の capability を検出できます。公開 artifact はそれぞれ `true` と `false` を
返します。fast runtime は対話入力をサポートしません。

公開モジュールは JIT バックエンドを選択しません。`vm` はバイトコードを解釈し、既定の `mixed` も VM で実行します。`jit` はアプリケーションの初期化、`main`、入力要求の前に構造化診断 `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory` を返します。従来 `jit` を指定していた呼び出し元は `vm` または `mixed` を選択してください。ポリシー名と C ABI ID は維持され、`result.backend` は要求されたポリシーを示し、JIT カウンターはすべてゼロです。

公開 API には `VeroScriptDiagnosticSeverity`、`VeroScriptExecutionBackend`、`VeroScriptLanguage`、`VeroScriptDiagnostic`、`VeroScriptResult`、`VeroScriptJitStatistics`、`VeroScriptVirtualFile`、`VeroScriptInputRequest`、各種実行オプション、`VeroScriptProgram`、`VeroScriptCompileResult`、`VeroScriptProgramRunOptions`、`VeroScriptProgramInteractiveRunOptions`、`VeroScriptRuntime`、`VeroScriptEngineModuleOptions`、`createVeroScriptRuntime`、`createFastVeroScriptRuntime` があります。`VeroScriptInputRequest.index` は 0 始まりの入力要求インデックスです。`VeroScriptProgram` は `disposed`、`run`、`runInteractive`、`dispose` を提供します。`runtime.supportsCompiledPrograms` は再利用可能なコンパイルが使用できるかを示します。`checkSource` は `main` を呼ばずに検査し、`runSource` は検査後に実行します。

`VeroScriptResult` の基本フィールドは `abiVersion`、`operation`、`success`、`exitCode`、`exitRequested`、`backend`、`output`、`errorOutput`、`diagnostics` です。コンパイルや実行では `programHandle`、`timings`（`timings.compileMs` と `timings.executeMs`）、および実行ごとの JIT 活動を示す `jit` が含まれることがあります。`VeroScriptJitStatistics` は `compileAttempts`、`compiledEntries`、`bridgeTrampolineEntries`、`linearTrampolineEntries`、`vmTrampolineEntries`、`unsupportedEntries`、`dispatchAttempts`、`dispatches`、`fallbacks`、`hotFunctions`、`directNativeCalls` を提供します。`directNativeCalls` は VM の値ブリッジを通らず別のネイティブエントリに到達した世代単位の JIT 呼び出し数です。`success` は処理全体の成功を示し、`exitCode === 0` と同義ではありません。`exitRequested` は `exit(n)` が明示的に呼ばれた場合だけ true になります。

バインドされたメソッド呼び出しなど、ネイティブフレーム経由で VM に再入する処理には、コアのプラットフォーム別スタック保護が適用されます。ネストが深すぎる場合は `diag.call_stack_overflow` が返されます。この保護は通常のインタープリター再帰の呼び出し深度制限とは別です。再試行する前に、ネイティブ再入のネストを減らしてください。

## 一度コンパイルして繰り返し実行する

同じソースを繰り返し実行する場合は、`compileSource` を一度呼び出して
返されたプログラムを再利用します。解析、import 解決、意味解析、バイト
コード生成を毎回繰り返さずに済みます。

```TypeScript
const compiled = runtime.compileSource(
	`int main(string[] args)
{
	return len(args);
}`,
	{ fileLabel: "main.vs" },
);

if (compiled.program) {
	const first = compiled.program.run({ args: ["one", "two"], backend: "mixed" });
	const second = compiled.program.run({ args: ["one"], backend: "vm" });
	console.log(first.exitCode, first.timings?.executeMs, first.jit?.dispatches);
	console.log(second.exitCode, second.timings?.executeMs);
	compiled.program.dispose();
}
```

コンパイル時に指定できるのは `fileLabel` と `files` であり、実行時の
`input`、`args`、`backend` は指定しません。各 `program.run` は新しい実行
状態を受け取り、入力、引数、バックエンドを個別に選択できます。コンパイル
済みバイトコードは共有され、VM 状態と JIT 統計は実行ごとに分かれます。
通常ランタイムでは `program.runInteractive({ args, backend, readLine })` も
使用できますが、高速ランタイムでは対話実行を使用できません。
異なるリリースの成果物を扱う場合は `supportsCompiledPrograms` と
`supportsInteractiveInput` を確認してください。不要になったら
`program.dispose()` を呼び、ランタイム破棄時には残りのプログラムも解放されます。

ファクトリの `options` は生成された Emscripten モジュールへ渡されます。
`.wasm` を独自 URL から配信する場合は `locateFile(path, prefix)` を使用し、
すでに読み込んだ `ArrayBuffer` または `Uint8Array` がある場合は
`wasmBinary` を使用します。

## 言語、仮想ファイル、対話入力

初期言語は決定的な `en-US` です。`availableLanguages` で組み込まれたカタログを確認し、`setLanguage` で選択します。Wasm モジュール内の言語状態は共有されるため、異なる言語を同時に使う場合は Web Worker を分けてください。

`files` は実ファイルシステムを書き換えずに一回の検査または実行へインポートモジュールを提供します。

```TypeScript
const result = runtime.runSource(
	`import "helper.vs";

int main()
{
	return answer();
}`,
	{
		fileLabel: "main.vs",
		files: [{ path: "helper.vs", source: `export int answer()
{
	return 42;
}` }],
	},
);
```

対話入力には通常ランタイムを使い、`runSourceInteractive` の `readLine` から `null` を返すと EOF になります。Wasm モジュールで同時に待機できる対話実行は一つだけです。TypeScript ラッパーは待機中の他の操作と破棄を拒否します。生の C ABI ではモジュール全体が保護され、create とテキスト getter は null、状態を変更するテキスト操作は API エラー、数値 getter とプログラム操作は 0 を返し、メモリ操作は Busy（9）、メモリ結果 getter は 0 を返します。lease の解放は引き続き利用でき、破棄は対話呼び出しの完了まで無視されます。長時間実行はページをブロックしないよう Worker で行ってください。[対話入力チュートリアル](../../../../../../../Resources/Docs/Tutorials/ja-JP/toolchain/16-web-interactive-input.md)と[Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md)も参照してください。

入力ブリッジのメモリ確保失敗や入力行の Wasm32 サイズ上限超過は API 失敗結果として返されます。モジュールは次の実行にも利用でき、`null` は通常の EOF を示します。

## ビルドとブラウザーの境界

公開ブラウザー成果物はコンパイル済みプログラムをサポートし、pthread を使用しないため COOP/COEP ヘッダーを必要と
しません。TypeScript の `runtime.supportsThreads` は `false` です。
`void main(string[] args)` と `int main(string[] args)` の両方を使用でき、
成功した 0 以外の `int main` も `success: true` のまま `exitCode` に返されます。
ブラウザーの標準環境では `spawn` は使えず、
`diag.failed_to_spawn_task_thread` が返されます。タスクを生成しない channel、
mutex、semaphore、synchronized-value の操作は 1 回の実行で利用できます。

## C++ ファサードと C ABI

`FRuntime` によるソース実行が成功した場合、JSON 結果と同じ JIT 統計も返され、
`directNativeCalls` も含まれます。

C++ の `VeroScript::WebAssembly::FRuntime` は言語選択、ソース検査と実行、仮想ファイル、入力ストリーム、バックエンド名、JSON 結果変換を提供します。現在の C++ ファサードはコンパイル済みプログラムの所有権 API を直接公開しないため、再利用可能なコンパイルには TypeScript ラッパーまたは C ABI を使用してください。TypeScript ラッパーを使わないホストは `VeroScriptWebAssemblyC.h` を利用できます。C ABI には `vero_web_compile_source_with_context`、`vero_web_run_program`、`vero_web_run_program_interactive`、`vero_web_destroy_program` もあります。プログラムハンドルはランタイムに所有され、解放が必要です。バックエンド ID は `0 = VM`、`1 = Mixed`、`2 = JIT` です。言語文字列は言語変更の成功またはランタイム破棄まで、利用可能言語の JSON は次の同じ問い合わせまたはランタイム破棄まで有効です。その他の JSON はランタイム所有テキストを返す次の API 呼び出しまたは破棄まで有効で、入力の文字列と配列は呼び出し中だけ借用されます。

## 管理メモリ

ホスト API：`runtime.memory` / `runtime.memory.setLimits` (bigint)。所有権、上限、ビューの失効、リースについては [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md) を参照してください。

<a id="managed-memory-contract"></a>

ホスト API はバッファー、スライス、コピー、スカラー読み書き、スナップショット、明示的リースを提供します。サイズ、オフセット、上限、ID、64 ビット整数は `bigint` を使用し、スカラーはリトル／ビッグエンディアンに対応します。`fromArray` は Node.js の `Buffer` も含め独立コピーを作成し、`toArray` もコピーを返します。使用後は領域を破棄してください。リースはストレージを固定してサイズ変更を禁止し、ランタイム破棄後も解放できます。`withBytes` は同期コールバック専用です。コールバック内で同じリースを解放したり、書き込み可能ビューを保持したり、Wasm ヒープを拡張したりしないでください。読み取り専用リースはコピーを渡し、書き込み可能ビューはヒープ拡張後の次の呼び出しで再作成されます。対話入力の待機中は状態を伴うメモリ操作を拒否します。

ホスト領域とスクリプトセッションの上限は独立し、各コンテキストの初期値はバッファー単体 16 MiB、保持合計 32 MiB です。`runtime.memory.setLimits` はホスト領域だけを変更し、ゼロは無制限です。上限は管理されたバイトストレージだけを対象とし、Wasm ヒープ全体や完全なサンドボックスを対象としません。

## コアの互換性と診断

<a id="compiler-diagnostics"></a>

組み込みコンパイラはファイル間の宣言を準備し、構造化アノテーションを受け付けます。未知のアノテーションはメタデータとして保持され、ブラウザー SDK はネイティブのアノテーションプロセッサを登録しません。`delegate.vs` をインポートすると、小文字のメンバー名を持つ `std::delegate` と `std::multicast_delegate` を使用できます。診断は `error`、`warning`、`info`、`hint` を保持し、成功結果にもエラー以外の診断を含められます。ソース実行はコンパイル診断を保持し、再利用可能なプログラムではコンパイル結果に含まれます。バイトコードは固定識別子 `VSBC` と製品バージョン `0.1.1` を使用します。以前の開発ビルドのパッケージとキャッシュはソースから再構築してください。

## 対話入力の所有権

対話呼び出しはオプションを読み取った後にモジュールの状態を再確認します。再入呼び出しや破棄済みランタイムの要求は入力プロバイダーを取得する前に拒否されるため、実行中のプログラムはコールバックと後続の入力を保持します。

<a id="input-provider-failures"></a>

SDK の入力プロバイダーの例外や Promise の拒否は現在の実行を中止し、元のエラーで拒否します。無効な結果型や NUL を含む行は `TypeError` で拒否します。失敗時はネイティブスタックを再開して後処理を行い、EOF として通常のスクリプト実行を続けることはありません。ランタイムとコンパイル済みプログラムは再利用できます。`null` は引き続き通常の EOF を意味します。マークのない生のモジュールプロバイダーは、拒否または無効な入力に対する従来の EOF フォールバックを維持します。

## コンソール出力と機能

<a id="console-capabilities"></a>

`std::console::write` は改行を付けずに `output` にテキストを追加し、`flush` は標準出力をフラッシュします。`std::console::error` はホストの診断行チャネルを使用するため、改行で終わらない最終行には `errorOutput` で改行が追加されます。SDK は端末ホストを提供しません。機能の問い合わせは `false`、サイズはゼロを返し、有効な色・カーソル・消去操作も `false` を返します。エスケープシーケンスの追加や取得済みテキストの削除は行いません。非同期入力の対応は端末機能とは独立しています。

無効な色名、消去モード、カーソル座標、引数の型や数は、端末機能が未対応でも診断を生成します。拒否後もランタイムは再利用できます。取得したテキストは実行完了時に `VeroScriptResult` で返されます。

## 現在の言語機能

<a id="compiler-and-application-services"></a>

両エンジンは、小文字の `pair<T, U>` / `std::pair`、`std::attribute<T>`、`std::bind_method` と、組み込みの `attribute.vs`、`callable.vs`、`i18n.vs`、`compiler.vs` をサポートします。アプリケーションの `std::i18n::context` はリテラル文字列、型を保持した名前付き書式設定、独立したスナップショットを提供します。その言語設定は SDK の診断言語を変更しません。SDK はアプリケーションのリソースや書式設定のプロバイダーを提供しません。

`import compiler "builder.vs";` は、`checkSource`、`compileSource`、`runSource` の処理中に `files` からスクリプトのコンパイル拡張を読み込めます。そのため、検査はアプリケーションの `main` を呼び出さなくても拡張コードを実行する場合があります。Core は拡張の既定の制限と権限ポリシーを適用します。フロントエンドからコンパイルを中断できる必要がある場合は Web Worker を使用してください。拡張の失敗は診断として返され、ランタイムは再利用できます。

## 対話実行のスタック領域

対話入力はネストしたバインド済みメソッド呼び出しの内部でも待機できます。深いネイティブ再入はホストのコールスタックに制約されます。高速エンジンには実行前に入力を渡してください。

## 注釈名の照合

<a id="annotation-name-matching"></a>

注釈名は既定で大文字と小文字を区別します。スクリプトコンパイル拡張は登録前に `annotation_definition.case_sensitive = false` を設定すると、その定義に対してロケールに依存しない ASCII の大小文字照合を有効にできます。プロセッサの購読と `has_annotation`、`find_annotations`、`annotation_info.matches_name` によるリフレクションはこの方針に従います。`name()` と `raw_text()` はソースの表記を保持し、`definition().case_sensitive()` は方針を返します。`@Name` 形式の注釈では、`find_annotations` と `matches_name` の照合に `@` 接頭辞が必要で、`has_annotation` では省略できます。引数名、メタデータキー、未登録の注釈は引き続き大文字と小文字を区別します。

## Core の実行と制限

<a id="numeric-and-library-limits"></a>

Core は `bigint.vs`、`rational.vs`、`core.vs` を通じて `std::bigint` と `std::rational` を提供します。これらは VM 内で整数と有理数の正確な値を保持します。正確な文字列として出力するには `to_string()` を使います。固定幅の Wasm ABI 引数に使う JavaScript の `bigint` とは別の型です。ネストしたジェネリック型、継承したネスト型、ローカル型は Core のスコープとアクセス規則に従います。

`runtime.setStandardLibraryLimits(maxTextBytes, maxReadBytes, maxSplitParts)` は、符号なし 64 ビット範囲の `bigint` で、操作ごとのテキストのバイト数、一括読み込みのバイト数、分割要素数を指定します。ゼロはホスト側の追加制限なしを意味し、省略した引数はゼロに戻ります。変更は後続のソース実行と対話実行、および既存のコンパイル済みプログラムの次回実行に適用されます。対話入力の待機中は変更できません。これらは管理メモリの割り当て制限や Core の数値制限とは独立しており、Wasm ヒープ全体の上限ではありません。Web API は Core 既定の有限な数値制限を使います。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
