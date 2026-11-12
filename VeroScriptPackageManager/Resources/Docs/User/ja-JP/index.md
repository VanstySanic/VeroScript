# 拡張パッケージのインストールと管理

プロジェクトのルート依存で明示した取得元は、取得元を指定していない同名の推移的依存にも適用します。バージョンとダイジェストの制約は引き続き検証し、明示的な取得元の競合はエラーになります。オフラインでは先にローカルの `vs-network` アーカイブを宣言してから `vs-http` または `vs-websocket` をインストールします。

`veroscript-project.json` はプロジェクトの依存、パッケージの有効状態と設定を保存します。配布物のルートは `veroscript-package.json`、ビルドグラフは `veroscript-build.json` を使用し、それぞれの形式とスキーマで検証します。

任意のパッケージ管理機能で、スクリプトライブラリ、資源、ネイティブ拡張を明示的なプロジェクトまたは Runtime/Core SDK に導入します。配布物はルートに veroscript-package.json を持つ .vspkg/.zip の ZIP です。.vslib、.vsapp、.vsbc とは別形式です。

## 環境の選択

コマンドの既定プロジェクトは起動ディレクトリです。--path は別プロジェクト、--root は実行 CLI の検証済み Runtime/Core SDK、--root --path は別の検証済みコンポーネントを選びます。相対パスは起動ディレクトリ基準です。保存先の既定はプロジェクト/SDK が .veroscript/packages、Runtime が Packages。ユーザー共通インストール、自動合成、実行中の自動導入はありません。

## コマンド

```sh
veroscript env --json
veroscript install ./library.vspkg --path ./app
veroscript install --path ./app --locked --offline
veroscript list --path ./app --json
veroscript show example-math --path ./app
veroscript disable example-math --path ./app
veroscript enable example-math --path ./app
veroscript verify --path ./app
veroscript uninstall example-math --path ./app
veroscript clean --path ./app
veroscript pack ./library --output ./library.vspkg
veroscript --path ./app ./app/main.vs
veroscript deploy --path ./app --output ./deployment
```

install は解決と導入、--locked はロック維持、--offline は通信禁止です。リモート配布物は完全なキャッシュ、ローカルアーカイブは現存するソースが必要です。固定したディレクトリのスナップショットは元の削除後も利用できます。--upgrade と --locked は併用不可です。list/show は表示、verify は整合性検査、disable はファイルと固定版を残して無効化しますが、有効な依存を破壊できません。enable で再有効化し、clean は参照も活動中のリースもない所有世代だけを削除します。

pack は既存の出力を上書きしません。新しい出力パスを指定するか、既存のアーカイブを移動してから再生成します。

## 更新、アンインストール、再インストール

```sh
veroscript install example-math --path ./app --upgrade
veroscript install --path ./app --upgrade
veroscript uninstall example-math --path ./app
```

最初のコマンドは指定したルート依存、次のコマンドは依存グラフ全体を再選択します。独立した `update` サブコマンドはありません。`--upgrade` は宣言された版の制約と取得元を守り、`--locked` と併用できません。版を省略して新規追加したレジストリ依存は選択された正確な版に固定されます。その固定を越えるには要件を変更するか `name@version` を明示して導入します。ローカルディレクトリ／アーカイブ依存の取得元はローカルのままです。[ローカル例](../../../Examples/README.md)ではレジストリなしで両方の更新コマンドを試せます。

`--upgrade` がなければ、有効な固定版を優先して同一内容を再利用します。要件、取得元、ローカル内容の変更によって別の解決結果が必要になることはあります。再インストールでも新しい世代を作り、導入アクション、信頼されたプロセッサー、許可されたフックを再実行します。フックには毎回 `--allow-hooks` が必要です。`--locked` は固定結果を制約し、`--offline` は通信を禁止しますが、導入処理を省略するものではありません。

`uninstall` は宣言済みのルート依存を受け取り、その要件と他のルートに不要となった依存を除きます。共有依存は維持されます。間接依存だけの名前を指定せず、それを要求するルートを削除・更新します。参照されなくなった導入世代は、活動中のリースがなくなった後で `clean` により別途回収できます。

## 実行と配置

実行、検査、eval、REPL、watch、バイトコード生成と対応するソース AOT は選択環境を使います。明示しなければスクリプトは入口、eval/REPL は起動場所からプロジェクトを探索し、最後に実行コンポーネントへフォールバックします。LSP/DAP/Interop との統合は各アダプターの責務で、CLI の指定が自動共有されるものではありません。DAP は未対応の明示指定を拒否します。AOT は対象のネイティブブリッジが必要です。

deploy は新しい出力先へ有効な runtime 依存閉包、資源、ライセンス、可搬なロード情報を複製します。compile/development 専用依存とアプリ/.vsapp は含まず、アプリは別途配置します。クロスターゲット導入は対象を実行せず検証・複製します。各プラットフォームで個別に検証してください。

導入済みの異なるターゲットをエクスポートするには、導入に使った完全なターゲット記述を `deploy --target <file>` に指定するか、`FInstalledEnvironment::DeployForTarget` を呼び出します。エクスポートは依存閉包をデータとして検証し複製します。実行のためのロードにはホストと一致するターゲットが必要です。

## 設定、エラー、言語

`recovery-conflict` は管理ファイルが中断したログの旧値・新値のどちらとも一致しないことを示します。編集と `transaction.json` を保持し、ロールバックが止まる場合は `recovery-required` になります。競合ファイルを退避し、残ったログの競合を解決してから再試行してください。ログだけを削除しないでください。

--packages-dir、--cache-dir、--lockfile で保存先を指定します。既存導入の保存先設定だけを書き換えず、新環境への再導入か配置を使います。environment-changed は並行編集を保持するため再読込して再試行、layout-conflict は状態/ロック/所有権パスの重複、restart-required は生存中のネイティブ実装と置換版が共存できない状態です。

パッケージの --lang と繰返し可能な --fallback-lang は表示文言を選び、コンパイラー診断言語とは独立します。管理機能の組込診断は現在英語メッセージと安定した code を持つため code で分岐します。--json は機械結果とメッセージを分離します。管理機能を無効にしたビルドにはこれらのコマンドはありません。

[配布パッケージの作成](authoring.md) · [C++ からの利用](embedding.md)

## 依存リストからインストール

```sh
veroscript install -r requirements.txt
veroscript install --requirement "config/dependencies.txt" --path ./app
veroscript install -r requirements.txt --root
veroscript install -r requirements.txt --locked --offline
```

`-r FILE`、`-rFILE`、`--requirement FILE`、`--requirement=FILE` は繰り返し指定でき、`install` 専用です。個別のパッケージ引数とも併用できます。ファイル名は任意で `.txt` は必須ではありません。コマンド行のリストパスは起動ディレクトリを基準とし、インストール先の `--path`／`--root` と独立します。リスト内の入れ子リストとローカル配布物は、それぞれの記述元ファイルのディレクトリを基準にします。絶対パス、UTF-8、空白を使用できます。空白やコメント文字を含むパスは引用符で囲んでください。引用符内の Windows バックスラッシュはそのまま保持し、スラッシュも使用できます。

UTF-8 リストの例です。LF／CRLF と任意の UTF-8 BOM に対応します。

```text
# Registry requirements use VeroScript versions.
gui==0.1.1
math >= 0.1.1, < 0.2.0 # both comparisons apply
widgets@0.1.1-beta.1
-r "lists/common.txt"
"../archives/local library.vspkg"
tools @ "../tools"
assets @ https://example.com/assets/download
```

論理行ごとに依存項目またはローカル `-r`／`--requirement` を記述します。空行と `#` コメントは無視されます。行末コメントの `#` は引用符外で直前に空白が必要です。エスケープされていない行末バックスラッシュは次の物理行を空白で連結し、その後コメントを除去します。同名項目のバージョン条件は積集合となり、明示された取得元や設定が異なる場合は失敗します。名前付き直接参照は実際のマニフェスト名を検証します。ローカルパスは選択プロジェクトを基準に変換して `veroscript-project.json` に保存し、URL／アーカイブのハッシュは通常どおり記録します。他のプロジェクト依存を保持し、明示項目が対応するルート依存を更新します。全項目で一度の解決と一つのインストールトランザクションを共有します。`--locked`、`--offline`、`--upgrade` とフック権限は従来どおりです。失敗時に一部だけを確定せず、リストの並行変更は保留中の確定を無効化します。

比較演算子は `==`／`=`、`<`、`<=`、`>`、`>=` で、カンマまたは空白区切りは AND です。既存の `name@<条件>` と `name@*` にも対応します。名前だけなら既存の依存を保持し、新規のバージョンなしレジストリルートは選択した版に固定します。直接参照には空白付き `name @ <パスまたはURL>` を使います。スラッシュのないディレクトリは `./directory` または引用符で名前と区別してください。

pip の習慣を参考にした VeroScript 固有の名前／バージョン形式です。Python extras、環境マーカー、VCS／editable、制約ファイル、`~=`、`!=`、pip オプションには対応しません。インクルードはローカルファイルのみで、環境変数、`~`、shell エスケープ、glob は展開しません。レジストリ、ミラー、認証、権限は既存の CLI／ホスト設定を使います。循環、無効な UTF-8、構文エラーはファイル／行番号を報告します。読み込み、深さ、ファイル／項目数、累積メタデータ、キャンセル、期限は既存の制限に従います。リストは明示指定が必要で、通常の `install` は `veroscript-project.json` を読み、テキストリストを自動検索しません。

### オフラインの例

[ローカル例](../../../Examples/README.md)の `library` を `workspace/vendor/library` にコピーし、次の二つのファイルを作成して `workspace` から実行します。結果には `example-math` が表示され、対象アプリの veroscript-project.json とロックに依存情報が保存されます。

`workspace/requirements.txt`:

```text
# Local package list
-r lists/local.txt
```

`workspace/lists/local.txt`:

```text
"../vendor/library"
```

```sh
mkdir app
veroscript install -r requirements.txt --path ./app --offline
veroscript install --requirement=requirements.txt --path ./app --locked --offline
veroscript list --path ./app --json
```

循環インクルード、不正な文字エンコード、リソース上限違反は拒否します。診断には可能な場合にソースファイルと開始物理行を示します。`requirements-changed` はコミット前のリスト変更を表し、ファイルを読み直して再試行してください。

明示的な URL 項目に `--locked` または `--offline` を指定すると、同じ URL に記録済みのハッシュを再利用します。名前付き参照はパッケージ名も一致する必要があります。キャッシュのバイト列は再検証され、未知または変更された URL に別の URL のキャッシュ識別子を流用しません。オフラインでの再実行には、保存済みのプロジェクト依存と完全で検証可能なアーカイブキャッシュが必要です。

初回インストールがプロジェクト／状態の作成前に中断した場合も `recovery-required` になります。明示的なパッケージ書き込み操作で復旧してください。有効で全変更が適用済みの `committed` ログは読み取りを妨げません。`combine-processing` は生成物やネイティブローダーが別の依存関係／設定に対応することを示します。必要なグラフを同じ環境にインストールしてください。`pack` は同時作成されたものを含め既存の出力を置換しません。

パッケージ格納先と管理ファイルはシンボリックリンクや特殊エントリーを拒否します。macOS では root 所有の `/tmp`、`/var`、`/etc` が所定の `/private/...` を指す場合だけ、通常の一時プロジェクト／キャッシュの祖先として許可します。パッケージアーカイブ内や選択した格納先の下にあるリンクの拒否規則は変わりません。

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md) · [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
