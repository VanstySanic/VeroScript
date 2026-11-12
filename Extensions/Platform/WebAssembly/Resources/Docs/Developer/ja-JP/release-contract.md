# WebAssembly ABI、artifact、release 契約

## 三つの公開インターフェイス

`VeroScriptWebAssembly.h` は C++ ファサード、`VeroScriptWebAssemblyC.h` は WebAssembly C ABI を定義し、TypeScript ラッパーと型宣言はブラウザー SDK を構成します。共通の結果フィールド、バックエンド名、言語タグと入力動作の整合性を保ってください。独立したネイティブ Interop ABI は WebAssembly ABI の定義ではありません。

## 名前と互換性

モジュールと CMake ターゲットは `VeroScriptWebAssembly`、C++ 名前空間は `VeroScript::WebAssembly`、TypeScript エントリーポイントは `veroscript-webassembly.ts` です。SDK アーカイブは `VeroScriptWebAssemblySDK`、インストーラーは `VeroScriptWebAssemblyInstaller` です。C ABI の `vero_web_*`、`VeroWeb*`、`VERO_WEB_*` と ABI バージョン `0.1.1` を維持します。プロファイル `web`、コンポーネント `web-sdk`、製品 ID `dev.veroscript.web` は既存のインストール記録を識別します。

## 成果物と公開時の検証

通常版と高速版の `.mjs`/`.wasm`、ラッパーの `.ts`/`.d.ts`、二つのエンジンの `.d.mts` 宣言の計八ファイルを公開します。標準ライブラリとカタログは仮想ファイルシステムに埋め込まれます。バインディングと宣言の型検査、全ファイルの監査を行い、ヘッドレス Chromium で対話入力、Web Worker とその停止を検証します。成果物監査の Release ダウンロード上限は通常版 Wasm が 17 MiB、高速版が 11 MiB です。これは公開時の制限であり ABI 制限ではありません。ソース確認のみでブラウザー上の受け入れ完了とは判断できません。

現在の基準には、正確な数値と、ネストした型・ローカル型・ジェネリック型のスコープが含まれます。

Web 製品は `VeroScriptBytecodeVm` を静的に組み込みます。ソース検査、コンパイル拡張、直接実行、再利用可能なプログラムは、明示的に選択されたこのインタープリターを使用します。

両 Wasm エンジンは Core を通じて初期化キャプチャ、`mutable` クロージャ、ローカル `static` 変数をサポートします。値キャプチャは既定で読み取り専用です。関数値のコピーは環境を共有し、ラムダを再評価すると新しい環境が作られます。ローカル静的変数は初回使用時に初期化され、初期化失敗後は再試行できます。対話実行を含む各プログラム実行は、ローカル静的変数も含めて新しい状態から開始します。Web ABI と TypeScript API は変更されません。

## ABI とローカライズ

ランタイムのセマンティックバージョン、WebAssembly ABI バージョン、バイトコードバージョン、機能フラグと選択言語を区別します。利用可能な BCP 47 タグは実行時に取得してください。API 識別子、プロトコルフィールド、診断 ID とバージョンは翻訳せず、固定の言語数から動作を推測しないでください。

## プログラムと入力の機能

C ABI のエクスポート、TypeScript 宣言と機能検出を一致させます。再利用可能なコンパイルにはコンパイル、実行、破棄のエクスポートが必要で、対話実行には Asyncify の対話用エクスポートも必要です。追加エクスポートがない場合は未対応として扱い、プログラムハンドルを捏造しないでください。

コンテキスト配列の UTF-8 パスとソースは同数で、呼び出し中だけ借用します。ハンドルは作成したランタイムに属し、別インスタンスへ移せません。結果 JSON には診断、出力、要求されたバックエンド方針、時間と JIT 統計を含めます。公開時にはハンドル再利用、実行ごとの新しい状態、破棄、対話待機と言語選択を検証します。

<a id="web-execution-policy"></a>

公開モジュールは JIT バックエンドを選択しません。`vm` はバイトコードを解釈し、既定の `mixed` も VM で実行します。`jit` はアプリケーションの初期化、`main`、入力要求の前に構造化診断 `diag.jit_execution_requires_an_explicitly_selected_backend_file_directory_or_factory` を返します。ネイティブ JIT の実行層は `Modules/Execution/VeroScriptAsmJit` に属し、Web モジュールはこの JIT バックエンドを組み込まず、動的にも読み込みません。従来 `jit` を指定していた呼び出し元は `vm` または `mixed` を選択してください。ポリシー名と C ABI ID は維持され、`result.backend` は要求されたポリシーを示し、JIT カウンターはすべてゼロです。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/release-contract.md) · [fr-FR](../fr-FR/release-contract.md) · [ja-JP](release-contract.md) · [ko-KR](../ko-KR/release-contract.md) · [zh-CN](../zh-CN/release-contract.md) · [zh-TW](../zh-TW/release-contract.md)
