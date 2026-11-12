# コマンドラインツール

コマンドライン プログラムの名前は `veroscript` です。スクリプトの実行、実行を伴わないコンパイルチェック、ソーステキストの評価、REPLの入力、DAPデバッガーの起動、パッケージの構築、バインディングカタログの検証、およびAOTソースの生成が可能です。

## プロジェクトの初期化

```sh
veroscript project init
veroscript project init --path "./MyProject"
veroscript build --path "./MyProject"
```

不足している `veroscript-project.json` と `veroscript-build.json` を作成します。
バージョンは `0.1.1`、依存関係は空で、既定のポータブルアプリケーションターゲットは
`app.vsapp` を出力します。既定のビルド設定を新規作成する際、入口がなければ最小の
`main.vs` も生成します。既存の通常ファイルは `Kept` と表示され、上書きされません。
既存のビルド設定の入口は変更せず、余分な `main.vs` は生成しません。出力先をディレクトリや
シンボリックリンクが占有する場合はエラーになります。再実行すると不足する設定を補完できます。
`project --help` と `project init --help` はファイルを書かずにヘルプを表示します。

`--path` を省略すると現在の作業ディレクトリを使用します。相対パスはコマンドの起動場所を
基準とし、絶対パスと `--path=<ディレクトリ>` にも対応します。不足するディレクトリは作成します。
ディレクトリ名を有効な小文字 ASCII のプロジェクト名に正規化し、必要なら `my-project` を
使用しますが、ディレクトリ自体は改名しません。初期化はローカルで行い、依存関係のインストールや
スクリプトの実行は行いません。`veroscript.lock` はインストール成功時に生成され、
`.veroscript/` はパッケージ管理時に必要に応じて作成されます。ビルドには対応サービスを含む
CLI が必要です。[最小プロジェクト例](../../../Examples/Project/README.md)を参照してください。

## コマンド形式

```powershell
# Run a source script or application package
veroscript [options] hello.vs [args...]
veroscript [options] DemoApp.vsapp [args...]

# Check without executing
veroscript --check hello.vs

# Execute source text
veroscript -e 'print(1 + 2);'

# Enter the REPL
veroscript -i

# Generate C++17 AOT source (the default target)
veroscript aot hello.vs -o generated.cpp

# Generate standalone Python source
veroscript aot --target=python hello.vs -o generated.py

# Generate a deterministic C# source bundle
veroscript aot --target=csharp hello.vs --output-dir generated --bundle-name App

# Validate or format a binding catalog
veroscript bindings validate catalog.json
veroscript bindings format catalog.json -o formatted.json

# Inspect a package
veroscript package inspect DemoApp.vsapp
```

`string[] args` を持つ `main` は、スクリプト パスの後に引数を受け取ります。パス自体は `args` には含まれません。パラメータのない `main` は追加の引数を無視します。 `int main` 戻り値がプロセス終了コードになります。正常に完了した `void main` は成功を返します。

CLI プロセスの終了コードはスクリプトの最終結果です。`int main` 値が返され、`exit(n)` が優先され、`void main` または右中括弧に達する `int main` は `0` を生成します。これは、埋め込み API の `FRunResult` セマンティクスとは異なります。埋め込みホストは、`bSuccess` を使用してエンジンが正常に完了したかどうかを判断し、`ExitCode` を個別に検査する必要があります。

## 共通オプション

|オプション |目的 |
| --- | --- |
| `-e <source>` / `--eval <source>` |ソーステキストを実行します。 `-i`、`--check`、または監視モードと組み合わせることはできません。 |
| `-i` / `--interactive` | REPL に入ります。 |
| `--repl-strict` | `-i` が必要です。最後の REPL 入力が失敗したときに非ゼロで終了します。 |
| `-I <path>` / `--import-path <path>` |モジュール検索パスを追加します。オプションは繰り返し指定できます。 |
| `--stdlib-path <path>` |標準ライブラリの検索ルートを追加します。 |
| `--implicit-import <file>` |ファイルをコンパイルされたすべてのソースにインポートします。オプションは繰り返し指定できます。ソースの編集や宣言の再エクスポートは自動的には行われません。 |
| `--no-circular-imports` |循環モジュール依存関係を拒否します。サイクルはデフォルトで許可されます。 |
| `--allow-circular-imports` |循環モジュール依存関係を許可するデフォルトの動作を明示的に復元します。 |
| `--allow-project-wide-access` |エクスポートされていない宣言へのプロジェクト全体のアクセスを許可します。控えめに使用してください。 |
| `--binding-catalog <file>` |ホストバインディングカタログをロードします。繰り返し可能でマージされます。通常のチェック/実行モードでは使用できますが、`-e`、`-i`、または監視モードでは使用できません。 |
| `-D name[=value]` / `--define name[=value]` |オブジェクトのようなマクロまたは条件付きコンパイル シンボルを定義します。 |
| `--macro NAME(params)=body` |構造化されたホストスタイルの関数マクロを定義します。 `--predefined-macro` は互換性エイリアスです。 |
| `--check` |実行せずに診断をコンパイルしてレポートします。 `-e`、`-i`、または監視モードと組み合わせることはできません。 |
| `--lang <tag>` |ロードされたカルチャ ディレクトリから解決可能な診断言語を選択します。デフォルトのリソースは、ディストリビューションに同梱されたカルチャ ディレクトリによって決まります。カスタム カタログ ルートでは、より多くの言語を提供できます。 |
| `-o <path>` / `--output <path>` | AOT またはバインディング カタログ フォーマットの出力パス。 |
| `--output-dir <path>` | C# または Python AOT の複数ファイル bundle ディレクトリ。 |
| `--bundle-name <name>` | ソース bundle の名前。省略時は入力ファイル名を使用します。 |
| `--manifest <path>` | C# bundle の manifest パス。省略時は出力ディレクトリです。 |
| `--target <name>` | AOT 対象: `cpp` (既定)、`csharp`、`python`。 |
| `-h` / `--help` / `-?` |ヘルプを表示します。 |
| `--version` |バージョン、プラットフォーム、および JIT 機能の情報を表示します。 |

## 実行バックエンドとキャッシュ

|オプション |目的 |
| --- | --- |
| `--backend=vm` または `--backend vm` |バイトコード仮想マシンのみを使用します。 |
| `--backend=mixed` または `--backend mixed` |サポートされている場合は JIT を使用し、VM にフォールバックします。これがデフォルトです。 |
| `--backend=jit` または `--backend jit` | 明示指定またはモジュール環境による自動選択で利用可能になった互換 JIT モジュールが必要です。対象を積極的にコンパイルし、未対応の本体は実行開始前に VM へフォールバックします。 |
| `--bc-cache=off` / `--bc-cache=on` | `.vsbc` バイトコード キャッシュを無効または有効にします。デフォルトで有効になっています。 |
| `--bc-cache-dir <path>` |共有バイトコード キャッシュ ディレクトリを選択します。省略するとプラットフォーム ユーザー キャッシュが使用されます。 |
| `--vm-backend <id\|file\|directory>` / `--vm-backend=<id\|file\|directory>` | インタープリターを選択。同梱 `vs-bytecode`、互換ファイル／ディレクトリー、パッケージ対応時のインストール済み ID。実行方針とは独立。 |

完全な Runtime は互換インタープリターを `Vm/` に含みます。CLI 配置時もこのディレクトリーを保持してください。`--vm-backend=vs-bytecode` は同梱版を選択します。他の単純 ID はパッケージ対応時に明示的に選んだインストール済み環境だけで解決し、インストールだけでは選択しません。ファイル／ディレクトリーは起動ディレクトリー基準で、Runtime との互換性が必要です。`--vm-backend` はインタープリター、`--backend=vm|mixed|jit` は実行方針、`--jit-backend` は JIT 実装を選択します。スクリプトによるコンパイル処理にも VM が必要です。その処理を実行しない通常の検査はエントリープログラムを実行しません。

```sh
veroscript --vm-backend=vs-bytecode script.vs
veroscript --vm-backend=./custom-vm script.vs
```

デバッグは VM を使用します。`--watch` は監視中のファイルが変更されると、選択した実行ポリシーで新しく実行します。

## ウォッチモード

`--watch` は、ソース ファイルまたは関連モジュールが変更されたときにスクリプトを再実行します。 `--watch-once` は監視モードを有効にし、最初のゼロ以外の実行結果の後に終了します。チェック モード、評価、REPL、デバッグ、およびパッケージ ファイルを監視モードと組み合わせることはできません。ビルドされたパッケージは `--watch` をサポートしません。

## デバッグ

標準入出力を介した DAP:

```powershell
veroscript --debug hello.vs
```

TCP 上の DAP:

```powershell
veroscript --debug --dap-port=4711 hello.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 hello.vs
```

`--dap-port` は `1` ～ `65535` を受け入れます。 `--dap-bind` はリスニング アドレスを選択します。デフォルトは `127.0.0.1` で、`--debug --dap-port` が必要です。デバッグ モードでは、DAP `launch` または `attach` がプログラムとその引数を提供します。コマンドライン スクリプト パスはプログラムを事前に設定し、その後ろに書いたコマンドライン引数はその request の default script argument になります。 `veroscript-dap` は、同じデバッグ ホストのシン エイリアスです。完全なプロトコル境界については、[デバッグ](../../../../../VeroScriptTooling/Resources/Docs/User/ja-JP/debugging.md) を参照してください。

## パッケージ、AOT、カタログ

`--emit-package` を使用してアプリケーションとライブラリのパッケージをビルドします。 `--package-file` はソース モジュールを繰り返し追加でき、`--package-version` は署名されていないパッケージのバージョンを設定し、`--strip-package-debug` はソース ファイルとラインテーブル情報を削除します。これらのオプションは、`-e`、`-i`、`--check`、監視モード、またはデバッグと組み合わせることはできません。 [パッケージと配布](packages.md) を参照してください。

`aot` サブコマンドにはソーススクリプトが必要です。既定の対象は C++17 で、
`-o`/`--output` に変換ユニットを出力します。`--target=python` は Python 3.11-3.15
以降のスタンドアロンソース、`--target=csharp` は .NET 8/9 用 C# ソースを
生成します。Python と C# では `--output-dir` と `--bundle-name` も使えます。
結果は実行ファイルではなくソースです。[C++ AOT](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/aot.md)、[Python AOT](../../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/aot.md)、
[C# AOT](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/aot.md) を参照してください。

`bindings validate` はカタログをチェックし、`bindings format` はカタログを正規化します。後者は、`-o`/`--output` を使用して新しいファイルを書き込むことができます。 [ホスト バインディング](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/bindings.md) を参照してください。

## 診断と環境変数

```powershell
veroscript --lang en-US --check script.vs
veroscript --version
```

一般的な環境変数には次のものがあります。

|変数 |目的 |
| --- | --- |
| `VERO_PATH` |モジュール検索パス。 Windows では `;`、Unix では `:`。 |
| `VERO_STDLIB_PATH` |標準ライブラリの検索ルート。 |
| `VERO_CACHE_DIR` |バイトコードキャッシュディレクトリ。 |
| `VERO_LANG` | 現在のカタログ ルートから読み込まれた任意の正規 BCP 47 タグなどの診断言語。 |
| `VERO_LOCALIZATION_DIR` |ローカリゼーション カタログのルート。 |
| `VERO_MAX_CALL_DEPTH` |合計再帰呼び出し深さの制限。 |

コマンドライン オプションは、対応する環境設定をオーバーライドします。サポートされていないシステム言語または `VERO_LANG` は英語に戻ります。明示的に指定した未対応の `--lang` 値は引数エラーとして拒否されます。 `veroscript --version` を使用して、インストールされているバージョンを検査します。

ローカリゼーション ルートは `localization/<culture>/<component>.json` レイアウトを使用します。検証、検索順序、モジュールレベルのカタログについては、[診断ローカリゼーション](../../../../../VeroScript/Resources/Docs/User/ja-JP/localization.md) を参照してください。

## パフォーマンス測定時の選択

VM、mixed、JIT 優先の各バックエンドは同じ言語セマンティクスを実装します。mixed は対象コードを最適化層で実行しつつ VM フォールバックを保持し、JIT はコンパイルをより早く要求しますが、すべての関数がコンパイルされる保証はありません。

性能比較にはデバッグ以外の通常実行を使ってください。デバッグは VM を使用します。バイトコード キャッシュはコンパイル時間に影響するだけです。入力、プロセスの寿命、キャッシュ状態、バックエンドを揃えて測定します。

## Unicode のパスと引数

Windows の CLI は Unicode のプロセス引数を受け取り、処理の振り分け前に UTF-8 に変換します。そのため、非 ASCII のスクリプトパス、インポートパス、`main(string[] args)` への引数を保持できます。ソースとローカライズ JSON は UTF-8 を使います。リダイレクトした stdout または stderr を読むツールも UTF-8 としてデコードしてください。

## バックエンドの選択

<a id="cli-backend-policy"></a>

`vm`、`mixed`、`jit` は実行ポリシーを選び、JIT バックエンドの選択とは独立しています。Runtime は JIT を同梱しません。インストール済みモジュール環境がある場合、`--jit-backend` の省略時は `services.jit.backend` を継承します。未設定なら既定の `auto` が有効な候補をバックエンド ID の昇順で試し、最初の利用可能な互換バックエンドを選びます。`--jit-backend=auto` は設定を上書きし、`--jit-backend=none` は JIT を無効化します。ID、ライブラリファイル、ディレクトリの明示指定も可能で、無効な明示指定はエラーになります。`auto` でも、不正な候補 ID や同一バックエンド ID の有効なプロバイダーの競合はエラーになります。自動選択にはモジュール環境が必要です。JIT が無効、または自動選択で利用可能なバックエンドが見つからない場合、`mixed` は VM を使用し、強制 `jit` は実行前に失敗します。CLI オプションはスクリプトパスの前に置きます。デバッグは VM を使用します。未対応コードはエントリー前に VM へ戻れますが、エントリー後の失敗で副作用を再実行することはありません。

自動選択・明示選択・無効化の共通規則は次を参照してください： [サービスのバックエンド選択](../../../../../VeroScript/Resources/Docs/User/ja-JP/introduction.md#backend-selection).

```sh
veroscript --vm-backend=auto --jit-backend=auto main.vs
veroscript --jit-backend=none --backend=mixed main.vs
```

```text
veroscript --jit-backend="/path/to/Jit" --backend=mixed main.vs
```

## スクリプトによるコンパイル拡張

[スクリプトによるコンパイル拡張](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/compiler.md#source-extension-activation) を参照してください。

<a id="cli-compiler-options"></a>

`--compiler-config <key=value>` は文字列設定を一つ追加します。異なるキーには繰り返し指定でき、値を JSON として解釈せず、重複キーは拒否します。--compiler-instructions と --compiler-timeout は処理予算を指定します。--no-compiler-imports は設定済み拡張を残してソースからの有効化を禁止します。拡張はコンパイル VM、協調的キャンセル、制限されたサービスを使います。通常の import だけではコンパイル権限は得られません。生成コードにも通常の言語規則が適用され、単独出力には拡張 VM を残しません。プロセッサーの作成は Developer ガイドを参照してください。

[スクリプトによるコンパイル拡張](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/compiler.md)

拡張全体の既定予算は VM 命令 1,000,000 回と 2,000 ms です。0 は該当する予算を無効にします。ブロック中のネイティブコールバックを強制中断する制限ではありません。

`install -r <file>` / `install --requirement=<file>`: [依存一覧ファイル](../../../../../VeroScriptPackageManager/Resources/Docs/User/ja-JP/index.md).

## ネイティブモジュールのコマンド

ネイティブの `cli` ロールを持つインストール済みの有効パッケージは、選択したプロジェクトまたはコンポーネント環境にコマンドを追加します。パッケージが提供する実際の名前を使ってください。別のプロジェクトを選ぶ `--path <directory>` はモジュールオプションより前に指定します。

ローカルライブラリーは `--cli-module <file>` で読み込み、必要なら繰り返します。相対パスは起動時のディレクトリー基準で、ライブラリーファイルのみを受け付け、ディレクトリーや PATH を探索しません。Windows は `.dll`、Linux/macOS は対応する `.so`/`.dylib` を使います。環境/バックエンドオプションとライブラリー選択、モジュールオプション、コマンドまたはスクリプトの順に指定します。それ以後の引数はそのまま渡されます。

モジュールコマンドは同名ファイルより優先されます。そのファイルを実行する場合は `./name` を指定します。組み込み名や登録済みの重複名は使えません。モジュールコマンドを check、eval、REPL、watch、debug/DAP、バイトコード出力モードと組み合わせることはできません。`--help` は登録済み項目を列挙し、`<command> --help` は設定や実行をせず使用方法を表示します。ただし登録のためのネイティブモジュール読み込みは行います。出力と説明はモジュールが提供し、`--lang` は選択言語をコールバックへ渡しますが文章を自動翻訳しません。Core CLI のヘルプと一部の検証メッセージは英語です。

```text
veroscript my-command arg
veroscript --path ./project --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll --my-option=value my-command arg
veroscript --cli-module ./my-cli-module.dll my-command --help
```

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
