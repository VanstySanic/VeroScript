# Python toolchain と package 契約

## strict static-tool 契約

frontend は class 内で field、enum value、property、method declaration が重複すると `PYF0003` を報告します。対応する property の setter は一つだけ許可されます。Transpiler で `None` を使えるのは callable の return だけで、すべての constructor は `-> None` を宣言し、instance receiver は default のない positional `self` でなければならず、method decorator としては `@staticmethod` だけを受け入れます。

runtime binding の前に BindGen は完全な Binding Catalog と実際の Python callable も検証します。duplicate constructor/visible member name、field/getter/setter signature の不一致、catalog と required positional shape が完全一致しない constructor、function、method、property accessor を拒否します。これらの失敗は partial binding を登録せず、generated artifact も置き換えません。

## ネイティブ拡張

`VeroScriptPython` は embedded Python distribution ではなく CPython extension です。CMake は Python 3.11 から 3.15 の CPython interpreter と `Development.Module` を要求し、他 implementation を拒否します。extension は `VeroScriptInterop` に link し、platform CPython ABI suffix を用い、Interop library を importable package の隣に配置します。matching `localization/` と `stdlib/` tree も package の隣に deploy するため、test/local import に global PATH/library-path 変更は不要です。

## tool と component

frontend、BindGen、Transpiler、Python AOT package は Python tool component として install します。CMake target は selected interpreter で test し、relative site-package path に install して staged prefix の relocatability を保ちます。runtime wheel は native extension、Interop library、Python source、type information、standard library、Catalog asset を含みます。

## リリースの検証

distribution validation は importability、native-library placement、version agreement、package content、wheel/source-distribution behavior、supported interpreter/platform matrix を検査します。repository source tree からしか動かない package は release artifact として無効です。

## Wheel マトリックスの公開

Python 製品の完全公開では、`Extensions/Language/Python/pyproject.toml` から対象のインタープリターを決定します。`requires-python` と `cibuildwheel` の選択は一致が必要で、現在は CPython 3.11–3.15 が対象です。対象 OS とアーキテクチャに合う五つのネイティブインタープリターを準備してください。検出にはバージョン付きコマンドと Windows の `py` ランチャーを使います。完全公開の明示指定には Windows の `-PythonExecutables`、Unix の複数の `--python-executable` 引数を使います。`VEROSCRIPT_PYTHON_EXECUTABLES` でも指定でき、区切りは Windows で `;`、Unix で `:` です。バージョンの不足、デバッグ版、フリースレッド版、アーキテクチャの不一致は事前検証で拒否されます。インタープリターは自動インストールされません。

以下はリポジトリルートから Windows x64 のインタープリター一式をビルドせずに確認する例です。ネイティブホストに一致するターゲットを選んでください。単独のエントリーポイントでは複数の `--python` 引数でパスを明示できます。

```text
python Resources/Tools/Packaging/build-veroscript-python-wheels.py --target win-x64 --check-interpreters
```

パイプラインは一つのソース配布物を作り、そこから各 Wheel を別々のソース・ビルドディレクトリでビルドします。各 Wheel を対応するインタープリターの新しい環境にインストールし、`Extensions/Language/Python/Resources/Tools/Packaging/test-wheel.py` が選ぶインストール済みパッケージ向けの全テストスイートと `pip check` を実行します。完全公開にはすべての Wheel ABI、ソース配布物、整合するタグとネイティブ識別情報、同梱ライセンスが必要です。製品マニフェストの `pythonWheelVersions` に対象バージョンを記録するため、リポジトリがなくても監査できます。コンポーネントの部分公開は部分公開のままです。Python を選択した場合、`-SkipBuild` または `--skip-build` でもこのマトリックスのビルドと検証を行います。

Python sdist は `Release/Extensions/Language/Python/any/<version>/packages/` に一つだけ保存します。Wheel は実際のプラットフォーム・ABI タグで選び、タグで正当化できる場合にだけマニフェストを共有します。対象索引は sdist と Wheel の両マニフェストを参照します。チェックアウトがクリーンでも、sdist の実メンバーに `.cxx`、`.gradle`、`.externalNativeBuild` キャッシュがないことを確認します。`pyproject.toml` は明示的に除外し、sdist 検証も拒否します。分離展開後のビルドに必要なソース、ヘッダー、ビルドスクリプト、メタデータ、ライセンス表示は保持します。

SDK とインストーラーのネイティブモジュールは、メインビルドの単一 CPython ABI を保持します。Linux Wheel は実際のネイティブタグを保持し、このマトリックスは `manylinux` や `musllinux` の互換性を保証しません。

## ローカライズの保守

各 Python ツールが自身の `Resources/Localization/<culture>/` カタログを管理し、複数のツールで使うメッセージだけを Python 領域で共有します。配布時は `python_tools.*` ID が重複しないように統合します。ファイルは UTF-8 の JSON オブジェクトとし、ID は一意、値は空白だけでない文字列にします。破損したカルチャは除外し、次の一致候補を試します。

英語カタログが有効なメッセージ ID と位置引数の契約を定義します。翻訳は `{0}`、`{1}` などの番号付き引数を並べ替えられますが、インデックスの集合を維持する必要があります。名前付きフィールド、属性参照、添字アクセスはサポートしません。リテラルの波括弧は `{{` と `}}` でエスケープします。テンプレートが欠けているか無効な場合は実際に読み込んだ英語カタログに、書式処理が失敗した場合は組み込みの英語にフォールバックします。公開前に Frontend のローカライズ回帰テストと六言語の配布検証を通してください。

不正な JSON、重複したメッセージ ID、文字列以外または空白のみの値、不正な UTF-8 は該当カルチャを拒否する原因になります。翻訳にしか存在しない ID は無視します。

<a id="python-package"></a>

Python グループは `Extensions/Language/Python/pyproject.toml` の PEP 517 プロジェクトを保守します。リポジトリ直下から次のコマンドでローカルの wheel／ソース配布候補を作成・監査します。チェックアウトからのインストールは `python -m pip install ./Extensions/Language/Python` です。ソース配布はネイティブ入力を同梱しますが、選択した CPython の開発ファイルとネイティブツールチェーンは必要です。`VEROSCRIPT_PYTHON_INSTALL_DIR` は CMake の移設可能なステージングを保つ相対先です。設定済み wheel 行列はリポジトリ直下から `cibuildwheel` を実行します。ビルドと検証はリリースをアップロードしません。版情報は `VeroScriptVersion.h` から取得し、ロードしたランタイムと一致させます。

```sh
python -m pip install build
python -m build Extensions/Language/Python --outdir .tmp/python-dist
python Extensions/Language/Python/VeroScriptPython/Resources/Tools/Packaging/validate_distribution.py .tmp/python-dist --expected-version 0.1.1
```

## 配布メタデータの確認

wheel とソース配布の `Requires-Python` は宣言した行列全体と一致させます。現在の範囲は `>=3.11,<3.16` です。一部だけの公開でも、渡された全成果物を確認します。Windows 拡張の接尾辞は `win32`、`win_amd64`、`win_arm64` と選択した CPython ABI に一致させます。macOS wheel の最小バージョンタグは、拡張と同梱 Interop ライブラリーの両方が要求する OS 版を満たす必要があります。別ターゲット向けの組み立てはコンパイラー、ツールチェーン、ジェネレーターのプラットフォーム、デプロイ設定を保持し、拡張のコンパイル前に対象 CPython のヘッダーを確認します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/toolchain-and-packaging.md) · [fr-FR](../fr-FR/toolchain-and-packaging.md) · [ja-JP](toolchain-and-packaging.md) · [ko-KR](../ko-KR/toolchain-and-packaging.md) · [zh-CN](../zh-CN/toolchain-and-packaging.md) · [zh-TW](../zh-TW/toolchain-and-packaging.md)
