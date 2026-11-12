# Rider GUI アドオンの開発

## 所有範囲と依存

[プラグイン記述子](../../../META-INF/plugin.xml) が識別子と基本プラグインへの必須依存を定義します。基本 VeroScript プロジェクトから Runtime の解決、コンパイラー設定、VS 字句強調を利用し、そのクラスを再同梱しません。[Gradle の localPlugin プロジェクト依存](https://plugins.jetbrains.com/docs/intellij/tools-intellij-platform-gradle-plugin-dependencies-extension.html#local-plugin) は開発、テスト、IDE サンドボックスで使います。インストール用 ZIP はそれぞれ独立しています。

JDK/JVM 25、Gradle 9.5.0、Kotlin 2.4.0、IntelliJ Platform Gradle プラグイン 2.19.0 を使用。
Rider SDK は 2026.2.2、最小ビルド `262.10315.191`、厳密な上限系列 `262.*` です。
他系列は別検証が必要です。ネイティブ実行ファイルやプロバイダーは同梱しません。
MIT ライセンスは `META-INF/LICENSE`、Wrapper の原文告知は `Resources/Licenses` にあります。

## 設計と契約

`gui-compile` は標準の `.vsapp` アプリケーションコンテナーを出力します。Check と Preview は
ネイティブ CLI に渡し、コンテナーと Core バイトコードの読み取りを任せます。
対応する Runtime、VM ライブラリ、GUI プロバイダーを組み合わせて使用します。

- `language/` は大小文字を区別する独立レクサーと平坦な PSI です。XML 名前空間検査や
  ブラウザー CSS の意味解析は加えません。インラインコードは VS/GUI レクサーで層別強調します。
- `GuiMarkupModel` はファイル単位でキャッシュする構造情報で、型検査を行いません。
  編集時のプロジェクト全走査、ネイティブ検査、プレビューはありません。
  1 MiB 超または複数キャレットでは入力補助を省略します。
- `Resources/Catalog/gui.json` は自有 GUI ソースの組み込みメタデータです。
  `python Resources/Tools/update-catalog.py` で更新し差分を確認。ソースハッシュで変更を検出します。
  `FGuiSchema` を代替しません。独自 Schema を有限リストで拒否せず、Original/Mark、継承、バインディング、イベントを保ちます。
  `VeroScriptGui.vs` のローカルインポートと間接的な支援ファイルをたどります。
  Mark 省略時は宣言名を使用し、クラス境界とコメント/リテラルのマスクでメタデータの所属を保ちます。
  カタログ形式 2 は `VS::Gui::Internal` を含む宣言の `qualifiedName` を保持します。
  検索とドキュメントは実際の修飾名を使い、短い名前と Mark の別名も維持します。
  カタログは読み取り側と一緒に生成・配布します。
- `run/` は元の基準を保って設定・パスを継承。引数は全体のコンパイラー/環境設定（`--path` を含む）、
  GUI バックエンド、`gui-*` タスクとその設定、`--` とスクリプト引数の順です。
  `--asset` の繰り返しとフレーム数の `int64` 境界を保ちます。
- 検査・ベイクはスクリプトを実行しません。プレビューのネイティブプロセスは Rider が停止・終了します。
  バックエンド ID は任意。GUI DAP 起動はありません。準備済みホストのアタッチは基本プラグインの担当です。
  行リンクは ML のコードポイント列を UTF-16 オフセットに誤変換しません。

メタデータ抽出は `@GuiWidget` と `@ GuiWidget` のような、`@` と注釈名の間の言語上有効な空白を受け入れ、同じプロパティ・イベント・継承情報を生成します。公開型が変わらなくても、ソースハッシュが変わった場合はカタログを再生成し差分を確認します。

## 検証と配布

レクサーの token は各言語に属し、途中で切れた終了記号も指定走査範囲を越えて読みません。
インライン本文は次の raw/CDATA 境界で止まり、ファイル残部を繰り返し探索しません。
文字読み取り数の回帰ケースで線形走査を確認し、マシン依存の時間しきい値は使いません。
補完は確定文字 `=`/`:` を消費し、既存値を保持し、Tab で完全な名前を置換します。
通常のローカル `src` は `GuiStringLiteralManipulator` でパス編集・名前変更に対応します。
挿入内容をエスケープし、未終了の引用符は保ちます。実体参照を含むパスはローカル移動対象外です。
OS ネイティブの絶対パスはファイルシステムのルートから解決し、再バインドでは完全なルート接頭辞を書きます。
SDK のプロジェクトルート基準を使わず、相対パスは宣言元ファイルを基準に保ちます。
SDK のプロジェクトルート式パス変換は無効にします。
ネイティブの区切り文字だけを変換し、POSIX のファイル名に含まれるバックスラッシュを保持します。
参照のデコードはファイル名の `?` と SDK の macOS NFC 正規化を保持します。
相対パスの再バインドは SDK に従い、相対パスを生成できない場合は参照を変更しません。
ボリューム/ファイルシステムをまたぐ移動は別途確認と受け入れが必要で、自動移行対応とは表記しません。
文書ターゲットは編集済みスナップショットを破棄し、スタイルの文書は属性 token に限ります。
コンテキスト操作はタスク/入口と互換性のないテンプレート設定を除去し、適用可能な独自出力は保持します。
静的検査は所有する入力だけを走査し、Gradle SDK キャッシュやサンドボックスには入りません。

このディレクトリで実行します。

```sh
python -B Resources/Tests/test_catalog.py
python -B Resources/Tools/check-static.py
./gradlew :test :verifyPluginProjectConfiguration :verifyPluginStructure :buildPlugin
./gradlew :veroscript:buildPlugin
./gradlew :verifyPlugin
```

Windows は `gradlew.bat`、POSIX は必要に応じ `bash ./gradlew`。
`-PlocalIdePath=/absolute/path/to/Rider` で既存 SDK を選択できます。明示ルートタスクで
兄弟プロジェクト全体のテストを不用意に選択しません。GUI 出力は独立した
`VeroScriptGuiJetBrains-0.1.1.zip` です。統合 IDE パッケージでは GuiJetBrains を独立製品として登録し、
基本プラグインもビルドして、各製品のバージョンに従って両方のローカル ZIP を一緒に検証します。
単独の開発ビルドには引き続き上記コマンドを使用できます。

基本プラグインが未公開の場合、`:verifyPlugin` 単独では Marketplace から依存関係を解決できません。
公式 Plugin Verifier CLI で両方の ZIP を同時に検証してください。`plugins.txt` の各行にそれぞれの
絶対パスを書き、次のコマンドを実行します。ローカルの基本プラグインで依存関係を解決し、
公開前に両方の検証結果を確認します。

```sh
java -jar /absolute/path/to/verifier-cli.jar check-plugin @/absolute/path/to/plugins.txt /absolute/path/to/Rider
```

テスト対象はレクサー再開、不完全入力、カタログの継承/スタイル制約、パス移行、引数順と区切り、
入口/フレーム制約、リソースと上書き、永続化、登録、六言語リソースです。
実際の GUI Runtime で ML/VS/成果物の Check/Compile/Preview、移行、プロバイダー、非 ASCII/空白パス、
停止・診断リンクを各 OS で確認してください。Rider でインライン強調、先頭候補、引用符/タグ、
`src` 移動、基本 `.vs` の動作も確認が必要です。SDK フィクスチャは UI 受け入れの代わりではありません。

[エディタ統合の保守](../../../../../../../../Resources/Docs/Developer/ja-JP/editors.md#acceptance-evidence) を参照してください。

エディタのフィクスチャはテスト専用の同梱 SDK モジュールで通常の Rider フォルダーソリューションを
初期化し、実際のローカルファイルを使用します。ReSharper の無効化はテスト JVM 内だけです。
SDK の設定破損検出は、初期化済みの Rider 設定を基準に維持します。

Rider の対話 UI、実際のウィンドウでの Preview/Stop、移行、他 OS、実際のホスト統合は別途受け入れが必要です。
今回の結果は検証した環境に限ります。

六言語の UI・User・Developer を同期してください。モジュールのマニフェストは共有文書カタログへ登録済みです。
文書を更新する際は公開検査を実行してください。

[ユーザーガイド](../../User/ja-JP/index.md) · [規約](../../../../../../../../Resources/Docs/Standards/ja-JP/GENERAL.md)

## 検査／ベイク呼出しと保存パス

`GuiCommand.arguments()` は `check` と `compile` で `--backend=vm` を先頭に置き、`--path` によるパッケージ環境の準備前に実行用 JIT の選択を抑止します。プレビューでは実行バックエンドと JIT の上書きをどちらも出力せず、ネイティブ側の既定値を保ちます。GUI バックエンド欄は別のサービス選択です。`GuiCommand.persist()` はプロジェクト相対の保存パスで `java.io.File.separatorChar` のみを `/` に変換し、POSIX ファイル名のリテラルなバックスラッシュを保持します。オプション順序、設定保存後の省略/auto/none/独自プロバイダー、ネイティブファイル名の往復を回帰対象に保ちます。対象 OS ごとに検証し、Windows の結果を POSIX の検証としないでください。

[Rider SDK](../../../../../VeroScriptJetBrains/Resources/Docs/Developer/ja-JP/index.md#rider-platform-sdk)

<!-- docs-navigation -->

---

[開発者ドキュメント](../../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
