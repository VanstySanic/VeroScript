# C++ SDK

C++ SDK は VeroScript のネイティブ統合インターフェイスです。実行時の
埋め込み API と、C++ 向けのソース ツールをまとめています。Python、C#、
Rust などが使用する C ABI は [Interop SDK](../../../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md)
で説明します。

## モジュールを選ぶ

| モジュール | 用途 |
| --- | --- |
| [埋め込み](embedding.md) | C++ ホストから検査、ロード、実行、呼び出し、リロード、設定を行う。 |
| [ホスト バインディング](bindings.md) | ネイティブ関数、ホスト型、メンバー、演算子、ジェネリック ホスト型を登録する。 |
| [AOT](aot.md) | VeroScript からネイティブ ビルド用の C++17 ソースを生成する。 |
| [C++ フロントエンド](frontend.md) | 対応する C++17 サブセットを共有 AST に解析する。 |
| [BindGen](bindgen.md) | C++ 宣言からバインディング カタログと API リファレンスを生成する。 |
| [Transpiler](transpiler.md) | 対応する C++17 サブセットを VeroScript とソース マップに変換する。 |

フロントエンドは BindGen と Transpiler で共有されます。意味を保持でき
ない構文は近似せず、診断を出して拒否します。これらは汎用 C++ コンパイラ
ではありません。

## 統合の順序

<a id="explicit-vm-selection"></a>

実行前に `FEngineConfig::VmBackendFactory`／`VmBackendPath` または Session の選択 API で互換 VM を指定します。C++ SDK のリンクだけではインタープリターは選択されません。コンパイル処理スクリプトにも VM が必要です。SDK ターゲットと設定は [VeroScriptBytecodeVm](../../../../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/User/ja-JP/index.md) を参照してください。

1. 一回限りの処理には `VeroScriptEngine`、長寿命のモジュールと状態には
   `VeroScriptSession` を選びます。
2. モジュールをロードする前にホスト関数と型を登録します。
3. 必要に応じて `CheckSource`/`CheckFile` の後に `RunSource`/`RunFile` を呼びます。
4. IDE、LSP、パッケージ、静的チェッカーが使用する場合はカタログを出力して検証します。
5. コールバックと worker の実行は並行し得るため、ホスト状態を同期します。

スクリプトのグローバルとクラス静的フィールドはネイティブ worker ごとに
分離されます。明示的な同期共有には `channel<T>` を使用します。ネイティブ
コールバック、インターセプター、ミューテーション監視、AOT のホスト シンボル
は worker から並行して呼び出される可能性があります。

## 管理メモリ

ホスト API：`VeroScriptSession::GetMemoryContext()` / `VeroScript::Memory`。所有権、上限、ビューの失効、リースについては [`std::memory`](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/memory.md) を参照してください。

## ソースツールの診断言語

C++ フロントエンドと C++ BindGen／Transpiler 固有のヘルプ、引数エラーと宣言診断は現在英語です。BindGen／Transpiler の CLI は `--lang` を受け付けず、ランタイムの診断言語を変えてもツール固有のメッセージは翻訳されません。診断コードとソース位置はプログラムからの処理に使用できます。

## モジュール環境とバックエンド

指定したモジュール環境に実行リゾルバーがあれば、Engine/Session は自動選択を継承できます。明示ファクトリやファイル／ディレクトリが優先し、生の `VeroScriptVm` は明示的に組み立てます。[バックエンド選択](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection)を参照してください。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
