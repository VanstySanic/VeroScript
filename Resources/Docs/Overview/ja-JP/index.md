# VeroScript 総合概要

[GitHub のソースコード](https://github.com/VanstySanic/VeroScript)

この概要は目的に応じて言語、ツール、ホスト SDK、保守文書をつなぎます。詳細な意味、API、制限は各モジュールの参照文書で確認し、モジュール間の関係と読む順序はここで管理します。

## クロスプラットフォーム展開

| 展開先 | 対象構成 | ガイド |
| --- | --- | --- |
| デスクトップの Runtime と SDK | Windows (x86, x64, ARM64); Linux (x64, ARM64); macOS (x64, ARM64) | [インストールと配布](../../../../VeroScriptInstaller/Resources/Docs/User/ja-JP/index.md) |
| モバイル組み込み | Android、iOS（実験的なアダプター） | [モバイル SDK](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/User/ja-JP/index.md) |
| ブラウザーと JavaScript ホスト | WebAssembly | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ja-JP/index.md) |

OS と CPU アーキテクチャに合う配布パッケージを選んでください。パッケージの提供範囲と検証状況は製品・版によって異なります。依存関係、利用できるバックエンド、展開要件は各製品のプラットフォーム情報で確認してください。

## 読む順序を選ぶ

| 目的 | 最初に読む文書 | 次に読む文書 |
| --- | --- | --- |
| スクリプトの作成と実行 | [概要](../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md) → [言語の基本](../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/index.md) | [CLI](../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md)、[標準ライブラリ](../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/index.md)、[サンプル一覧](../../../../VeroScript/Resources/Docs/User/ja-JP/examples.md) |
| インストール、更新、配布 | [インストールと配布](../../../../VeroScriptInstaller/Resources/Docs/User/ja-JP/index.md) | [パッケージ](../../../../VeroScriptCli/Resources/Docs/User/ja-JP/packages.md)、[CLI リファレンス](../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md) |
| ホストへのスクリプト組み込み | 下表で SDK を選ぶ | [Interop の境界](../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md)、[ツールと統合](../../User/ja-JP/toolchain/index.md) |
| 編集、診断、デバッグ | [CLI リファレンス](../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md) | [LSP](../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/lsp.md)、[デバッグ](../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/debugging.md)、[診断のローカライズ](../../../../VeroScript/Resources/Docs/User/ja-JP/localization.md) |
| 実装の開発と保守 | [開発者の読む順序](#開発者の読む順序) | [すべての開発者文書](../../Developer/ja-JP/index.md) |

実行バックエンド、ホストバインディング、パッケージ設定を選ぶ前に、インストール済みのコンポーネントとバージョンを確認してください。コマンドと SDK サンプルには対応するコンポーネントが必要です。参照文書は実際のビルドと実行の検証を置き換えません。

## ホスト SDK を選ぶ

| ホスト | ユーザー向け参照 | 保守文書 |
| --- | --- | --- |
| ネイティブ C++ | [C++ SDK](../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/index.md) | [C++ モジュール](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ja-JP/index.md) |
| C ABI または他の言語 | [Interop SDK](../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md) | [ABI と寿命](../../../../VeroScriptInterop/Resources/Docs/Developer/ja-JP/abi-and-lifetime.md) |
| CPython | [Python SDK](../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/index.md) | [Python モジュール](../../../../Extensions/Language/Python/Resources/Docs/Developer/ja-JP/index.md) |
| .NET / C# | [C# SDK](../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/index.md) | [C# モジュール](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/ja-JP/index.md) |
| ブラウザーまたは JavaScript | [WebAssembly SDK](../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ja-JP/index.md) | [WebAssembly モジュール](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/ja-JP/index.md) |

値、ハンドル、コールバック、スレッド、解放の規則は選んだ SDK の契約を確認してください。受け付けるソースの部分集合、AOT の制限、対象環境も各モジュールの文書で定義します。言語名だけで互換性を判断しないでください。

## 開発者の読む順序

1. [コアのアーキテクチャとパイプライン](../../../../VeroScript/Resources/Docs/Developer/ja-JP/architecture-and-pipeline.md)と[実行と状態](../../../../VeroScript/Resources/Docs/Developer/ja-JP/execution-and-state.md)でコンパイル、実行、状態の境界を理解し、バックエンドや並行処理を扱う場合は [JIT と並行処理](../../../../VeroScript/Resources/Docs/Developer/ja-JP/jit-and-concurrency.md)を読みます。
2. ホスト境界に沿って [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/ja-JP/index.md)、[C++ セッション](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/ja-JP/session.md)、対象言語のアダプター文書を読みます。
3. ツール境界に沿って [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/ja-JP/index.md)、[ツールのプロトコル](../../../../VeroScriptTooling/Resources/Docs/Developer/ja-JP/architecture-and-protocols.md)を読みます。
4. リリース前に[ビルドとリソース](../../../../VeroScript/Resources/Docs/Developer/ja-JP/build-and-assets.md)、[インストーラーのビルドとパッケージ](../../../../VeroScriptInstaller/Resources/Docs/Developer/ja-JP/build-and-bundle.md)、[公開の検証手順](../../../../VeroScriptInstaller/Resources/Docs/Developer/ja-JP/release-validation.md)を確認します。

過去の検証結果は記録されたソース・成果物・環境だけに適用されます。製品のビルド、完全性検査、実行時の受入結果の記録は[公開の検証手順](../../../../VeroScriptInstaller/Resources/Docs/Developer/ja-JP/release-validation.md)に従い、証拠は無視対象の出力ディレクトリに保存します。

## 文書と公開

モジュール全体の一覧は[ユーザー入口](../../User/ja-JP/index.md)を参照してください。文書の所有者、六言語の保守、検証コマンド、All/User パッケージの手順は[文書保守ガイド](../../Developer/ja-JP/index.md)で管理します。

公開文書のリンクとアンカーはオフラインで利用できます。ソースコード参照と外部サイトにはネットワークが必要です。使い方とチュートリアルには User、保守ガイドと規約には All を利用してください。どちらの集合にも History は含まれません。

[ユーザー文書](../../User/ja-JP/index.md) · [開発者文書](../../Developer/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
