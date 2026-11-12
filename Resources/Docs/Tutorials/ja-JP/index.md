# VeroScript チュートリアル

これらのページは、タスクの完了を中心に構成されています。各チュートリアルは小さな目標から始まり、プログラムを段階的に成長させ、演習で終わります。言語ルールと完全な API の詳細については、[ドキュメント](../../User/ja-JP/index.md) を使用してください。

## 始める前に

必要なものは次のとおりです。

1. `veroscript --version` を通じて利用できる VeroScript コマンドライン ツール。
2. スクリプト ファイルの作業ディレクトリ。
3. プレーンテキストファイルを保存できるエディタ。

コードブロックのラベルは言語またはシェルを示します。`powershell` は PowerShell、`sh`/`bash` は対応するシェルで実行し、OS に合わせてパスを変更します。`text` は期待出力です。完全なプログラムでは通常 `int main` と明示的な終了値を使います。終了値が不要な短い例には `void main` も使えます。

`--check` はアプリケーションの `main` を呼ばずに検証します。有効な[スクリプトコンパイル拡張](../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/compiler.md)は、コンパイラーの制限と権限に従って実行されることがあります。

## 推奨されるパス

### ステージ 1: 最初のスクリプトを実行する

- [01: 最初のスクリプト](getting-started/01-first-script.md)
- [02: 引数の確認、実行、読み取り](getting-started/02-cli-and-arguments.md)

### ステージ 2: 言語の骨格を学ぶ

- [03: 値、条件、およびループ](language/03-values-and-control-flow.md)
- [04: 関数とコレクション](language/04-functions-and-collections.md)
- [05: 構造体、クラス、インターフェイス](language/05-structs-classes-and-interfaces.md)
- [06: 参照、参照戻り、ラムダ キャプチャ](language/06-references-and-captures.md)
- [07: モジュール、インポート、およびプリプロセッサ](language/07-modules-and-preprocessor.md)

### ステージ 3: 役立つプログラムを構築する

- [08: 文字列、ファイル、およびパス](standard-library/08-strings-files-and-paths.md)
- [09: 同時実行性とイベント](standard-library/09-concurrency-and-events.md)
- [20: バイナリパケットを読み、コピーする](standard-library/20-managed-memory.md)
- [22: アプリケーションの文面とスナップショット](standard-library/22-application-i18n.md)

### ステージ 4: 出荷と統合

- [10: パッケージの構築とアプリケーションの配布](toolchain/10-packages-and-distribution.md)
- [11: デバッグと AOT 生成](toolchain/11-debugging-and-aot.md)
- [12: ホスト バインディングとスクリプトの統合](advanced/12-host-bindings.md)
- [13: ランタイムリフレクション](advanced/13-reflection.md)
- [16: WebAssembly SDK の対話入力](toolchain/16-web-interactive-input.md)
- [17: Optional 値、weak 参照、共有構造体ハンドル](advanced/17-value-utility-types.md)
- [18: C++ ツールチェーン モジュールを選ぶ](cpp/index.md)
- [Python ソースツール](python/index.md)
- [C# ソースツール](csharp/index.md)
- [Tooling ワークフロー: editor と debugger を接続する](toolchain/17-tooling-workflow.md)
- [19：モバイルアプリでスクリプトを実行する](toolchain/19-mobile-embedding.md)
- [21: ローカルパッケージを導入する](toolchain/21-package-manager.md)
- [23: ライブラリーとアプリケーションをビルドする](toolchain/23-application-build.md)
- [24: コレクション・ナビゲーション・ダイアログ](toolchain/24-gui-collections.md)


### ステージ 5: プロジェクトを標準化する

- [14: `std` 標準名前空間を使用する](advanced/14-standard-namespace.md)
- [15: ワークスペースと診断ローカリゼーションの構成](toolchain/15-workspace-and-localization.md)

## これらのチュートリアルの使用方法

最初の 2 つのステージを順番に完了します。ステージ 3 のチュートリアルはタスク指向であり、必要に応じて選択できます。ステージ 4 は、配信または埋め込みの準備ができているスクリプトを対象としています。

すべてのチュートリアルは個別に読むことができますが、`--check` をスキップしないでください。実行前に構文、型、インポートの問題が検出されます。たとえ変化が小さくても、演習を実行してください。入力を変更するかブランチを追加するだけで、例を理解したかどうかを確認できます。

## 参考文献

- [言語の基本](../../../../VeroScript/Resources/Docs/User/ja-JP/language-basics/index.md): 構文とセマンティクス。
- [標準ライブラリ参照](../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/index.md): 関数のシグネチャと境界。
- [ツールチェーン](../../User/ja-JP/toolchain/index.md): CLI、パッケージ、デバッグ、およびホスト統合。
- [実行可能なサンプルのインデックス](../../../../VeroScript/Resources/Docs/User/ja-JP/examples.md): トピックごとにグループ化された配布サンプル。

<!-- docs-navigation -->

---

[チュートリアル](index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
