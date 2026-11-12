# VS Code の VeroScript GUI

**VeroScript**、**VeroScript GUI** の順にインストールします。ローカル版はツール API 1 に対応した組み合わせが必要です。GUI 拡張は `.vswml` と `.vswcss` を担当し、`.vs`、LSP、DAP、Runtime 選択とコンパイル設定は基本拡張が担当します。色は VS Code テーマに従います。

ウィジェット、プロパティ、`bind:`／`on:`、列挙値、パスの補完、ホバー、アウトライン、折りたたみ、対応タグ強調、6／8 桁の16進カラー編集を提供します。名前と Mark 別名は大文字小文字を区別し、`bind:`／`on:` に `xmlns` は不要です。VSWCSS はネイティブ GUI の対応範囲です。構造診断は情報レベルで、最終判断はネイティブコンパイラが行います。埋め込みスクリプトは着色のみで、GUI LSP や式の意味解析はありません。

組み込みヒントは ListView/ListItem、TreeView/TreeItem、TableView/TableColumn/TableRow、ComboBox、TabView/TabItem、Menu/MenuItem、Dialog と、その継承プロパティ・イベントに対応します。SelectionColor / selection-color はマークアップの色編集に対応し、Popup Placement は below、right、center を候補にします。実行時は同じソースで再ビルドした GUI プロバイダーとスクリプトライブラリを使用し、更新後のネイティブ構造に以前の 0.1.1 バイナリを混在させないでください。 [GUI フレームワーク](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/ja-JP/Reference/Framework.md).

**VeroScript GUI** のチェック、コンパイル、プレビュー、エントリ選択、出力表示コマンドを使用します。プレビューはネイティブアプリを起動します。`veroscript.runtimePath` に GUI／GUI スクリプト対応の Runtime を指定し、必要なネイティブプロバイダーを用意してください。編集だけではネイティブプロセスを起動しません。

明示的な GUI コマンドは初回にフレームワーク全体をコンパイルし、1 分以上かかる場合があります。タスクは非同期で実行され、必要に応じて VS Code のタスク操作で停止できます。

## ワークスペース設定

`veroscriptGui.entry` は `.vs`／`.vswml`、チェック／プレビューでは `.vsapp` も選択できます。空の場合は選択ファイル、スタイルの場合は同名 `.vswml` を使います。共有スタイルでは明示的に指定してください。`assets` はコンパイル時の埋め込みファイル、`packageName` はパッケージ名、`schemaFiles` は追加編集メタデータです。配列は既定値を置き換えます。

`backends` のキーは `platform`、`graphics`、`gui`、`image`、`font`、`text-layout` です。拡張の値を省略または空にするとネイティブ環境を継承し、明示的なバックエンド ID はそのバックエンドを選択します。`auto` はネイティブの候補選択、`none` はサービスの無効化を要求します。プレビューに必須のサービスを無効にするとネイティブエラーになります。`environmentPath` は CLI の `--path` でパッケージ環境を明示的に読み込みます。バックエンド指定が先です。LSP／デバッガー設定には影響しません。引数、環境、作業ディレクトリ、コンパイル設定は基本拡張を継承します。チェック／コンパイルは VM で JIT モジュールを追加せず、プレビューは実行バックエンドを継承します。`previewFrames: 0` は制限指定なし、正の安全整数は `.vswml` のみ有効です。`.vs` は自身のループを管理します。

標準のネイティブ Context ファクトリは、コンストラクター引数、呼び出し設定、アプリケーション／環境の既定値のいずれにも選択がない場合だけ `rmlui` を使い、明示した `auto` と `none` は保持します。拡張の空設定は上書きを省略しますが、ネイティブ環境に明示的に保存した空のバックエンド値はサービスを無効にします。VM/JIT 実行オプションは基本拡張が担当し、GUI サービスのロールには含めません。

パスとプロセスはリモートを含むワークスペース拡張ホスト側です。相対パスは所属フォルダー、フォルダーなしでは選択ファイルのディレクトリが基準です。`${workspaceFolder}`、`${file}`、`${fileDirname}`、`${env:NAME}` をサポートし、対応するコンテキストが必要です。文書内リソースは宣言ファイルが基準で、不透明なリソース ID はパスに変換しません。

## タスクとメタデータ

タスクは `type: "veroscript-gui"`、`action: "check" | "compile" | "preview"`、`input` を指定します。コンパイルでは入力と異なる `.vsapp` の `output` が必要です。`assets`、`name`、`frames`、`args`、`backends`、`environmentPath` は設定を上書きし、配列は継承値を置き換えます。`args` はプレビュー用です。コマンドは関連する未保存ソースの保存を確認し、設定タスクは VS Code の保存方針に従います。範囲外の依存ファイルは別途保存してください。ネイティブエラーはファイルと行を表示し、UTF-8 バイト列を UTF-16 列とみなしません。

`*.veroscript-gui.json` を `schemaFiles` に追加すると JSON 検証付きの補完情報を拡張できます。`schemaVersion: 1`、`types` を使い、型は `name` と任意の `mark`、`bases`、`properties`、`events`、メンバーは `name`、`type` と任意の `mark`、`bindable`、`style`、`values`、`description` を持ちます。実行バインドやコンパイルプロセッサーは登録されず、曖昧なタグ別名は候補から除外されます。

```json
{
	"schemaVersion": 1,
	"types": [
		{
			"name": "Demo::Card",
			"mark": "card",
			"bases": ["VS::Gui::Widget"],
			"properties": [
				{ "name": "Title", "mark": "title", "type": "string", "bindable": true }
			]
		}
	]
}
```

GUI `.vs` の意味解析／デバッグは基本拡張で対応ホストとバインドカタログを設定する必要があります。GUI プロセッサーの自動起動、独立 GUI デバッガー、ビジュアルデザイナーはありません。2 Mi UTF-16 コード単位を超えるファイルのローカル解析は停止します。

未完成ヘッダーと EOF の CSS 宣言でも補完し、`=`／`:` 前の空白を保持して数値寸法全体を置換します。閉じたヘッダーでは属性を、クラス／ID セレクターではウィジェット型を提案しません。CSS URL リンクは原生の大文字小文字・エスケープ規則に従い、script／style 内の XML コメントで構造を誤認しません。

GUI のコンパイルにはネイティブの Application 対応が必要で、標準 `.vsapp` コンテナーを生成します。同じ成果物を GUI の検査／プレビューと通常の CLI の inspect／検査／実行で利用でき、ホストには GUI と必要なプロバイダーの対応が必要です。従来の生の GUI `.vsapp` は再コンパイルしてください。バックエンドは手動選択のままで、拡張機能は RmlUi を自動選択しません。

同梱の `Vm/` モジュールを含む、対応した GUI 対応 Runtime 一式を使用してください。GUI の検査とコンパイルはスクリプト製のコンパイルプロセッサーを実行する場合があり、選択した GUI プロバイダーと Runtime VM の両方が必要です。プロバイダーは明示的に選択し、エディターメタデータは編集可能な宣言を表します。実行時通知の実装を表すものではありません。

通常の CLI の `--check` は、生成した `.vsapp` を独立したコンテナーとして検証でき、`--path` で対応するパッケージ環境を選択することもできます。GUI チェックでは `environmentPath` でその環境を選択します。修正済み Runtime と対応するインストール環境を使用し、Core／ABI 更新後は以前のビルドを対象とする環境を再生成してください。

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
