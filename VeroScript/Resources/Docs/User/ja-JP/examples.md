# 例のインデックス

サンプル ファイルは言語と実行時の動作を検証し、ドキュメントの実行可能相互参照を提供します。これらは API 仕様ではありません。署名、エラー条件、および互換性要件については、言語、標準ライブラリ、およびツールチェーンのリファレンスを使用してください。サンプルは Resources またはソースリポジトリから入手できます。実行時には、以下のファイル名を実際の場所に置き換えてください。

例を実行します。

```powershell
veroscript hello.vs
veroscript --check reflection.vs
```

[属性とメソッド関数値](standard-library/attribute.md)：`attribute_demo.vs`、`attribute_functions.vs`、`attribute_methods.vs`、`attribute_weak.vs`。

## 実行バックエンド、リソースと権限

インストール済みモジュール環境がある場合、`--jit-backend` の省略時はその JIT 設定を継承し、未設定なら `auto` が既定です。自動選択で利用可能なバックエンドがない場合、既定の `mixed` は VM を使用します。互換バックエンドのファイルやディレクトリに手動で固定する例：

```powershell
veroscript --jit-backend="/absolute/path/to/backend" --backend=jit integer_aliases.vs
```

パスを実際のバックエンド配置先に置き換えてください。標準ライブラリが Runtime のリソース配置外にある場合は `--stdlib-path` を使用します。組み込みホストはモジュールまたは仮想ソースと出力の取得処理を明示的に提供します。

組み込み・モバイル統合は `portable_basics.vs` から始められます。`numeric.vs` のインポート、値キャプチャの Lambda を検証し、`portable basics: 42` を出力します。必要なスクリプト権限は出力だけです。`runtime_utilities.vs` の `std::filesystem::cwd()` はファイル読み取り、`script_tool.vs` は環境変数読み取り、`concurrent_demo.vs` はタスク作成権限と分離された VM ワーカーを必要とします。ゼロ時間の待機でも完了済みの結果を取得できるため、結果は一度だけ消費します。`path_helpers.vs` と `windows_path_text.vs` は文字列を操作し、ファイルにはアクセスしません。アプリのワーカースレッドで適切な実行予算を設定して実行してください。

C# モバイルガイドには MAUI サンプル、Python モバイルガイドには組み込み CPython の受け入れ確認スクリプトがあります。モバイルでのビルド、アプリ統合と実機確認はデスクトップのスクリプトテストとは別に必要です。

## サンプルの読み方

目的のトピックを選び、ファイルのコメントと対応するリファレンスを読んでから、入力を変更して再実行します。`int main()` はプロセスの終了コードを返し、`int main(string[] args)` はスクリプト引数も受け取ります。戻り値が不要な場合は `void main()` も使えます。

## 入門と機能の組み合わせ

|例 |メイントピック |
| --- | --- |
| `hello.vs` |最小限のスクリプト、`main`、および出力。 |
| `portable_basics.vs` | 数値モジュールの明示インポート、配列、値キャプチャの Lambda と結果確認。出力権限のみ使用。 |
| `argv_echo.vs` | `int main(string[] args)` とコマンドライン引数。 |
| `object_basics.vs` | 構造体メソッド、型の宣言と同時に定義するインスタンス、null を許すクラス参照。 |
| `language_features.vs` |制約、ボックス化、オーバーロード、デフォルト、および可変長引数。 |
| `fib.vs` |再帰関数と条件付き制御フロー。 |
| `goto_demo.vs` | `goto` とラベル。 |
| `exceptions.vs` | 型付き例外、包括ハンドラー、再送出、`finally`。 |

## 値、配列、コンテナ、および参照

|例 |メイントピック |
| --- | --- |
| `arrays.vs` |ネイティブ配列と配列アクセス。 |
| `dynamic_array.vs` |動的配列。 |
| `brace_initialization.vs` |ブレースの初期化。 |
| `integer_aliases.vs` |整数のエイリアスと明示的な型。 |
| `numeric_primitives.vs` |異なる `float`/`double`、数値サフィックス、整数部を省略した浮動小数点リテラル、および 8/16/32/64 ビット整数。 |
| `pair_map.vs` | `pair`、マップのエントリビュー、分解、および構造体値と弱参照値の初回書き込み。 |
| `multimap.vs` |マルチマップ。 |
| `multiset.vs` |複数のセットと重複した要素。 |
| `priority_queue.vs` |優先キュー。 |
| `ref_foreach.vs` |参照変数と範囲の反復。 |
| `reference_returns.vs` |参照エイリアス、参照戻り、クラス ハンドル スロットの再バインド。 |

## 型、ジェネリック、および呼び出し可能な値

|例 |メイントピック |
| --- | --- |
| `operators.vs` | 演算子の参照引数、書き込み可能／const の添字参照、代入参照と後置演算の値コピー。 |
| `static_members.vs` |静的メンバーと初期化。 |
| `default_delete.vs` |デフォルト化および削除された特別メンバー。 |
| `constructor_initializers.vs` | コンストラクター初期化リスト、`const` フィールド、初期化順序。 |
| `friend_access.vs` | `friend` アクセス ルール。 |
| `handle_struct.vs` | `shared<T>` / `handle<T>` の構造体共有識別、コピー構築と weak の失効。 |
| `pure_virtual_out_of_class.vs` |クラス外の定義と純粋な仮想構文。 |
| `template_prefix.vs` |テンプレートメンバー定義のプレフィックス構文。 |
| `method_templates.vs` | 独自のメソッドパラメーターと callable 参照。 |
| `value_template_parameters.vs` | 検査付き値パラメーター、列挙型方針、固定配列長。 |
| `constexpr.vs` | コンパイル時スカラー値、純粋関数、通常の実行時呼び出し。 |
| `template_methods_main.vs` | ファイル間のメソッドテンプレート定義。 |
| `generic_constraints.vs` |一般的な制約と `where`。 |
| `lambda.vs` | Lambda、値／参照の初期化キャプチャ、共有 mutable カウンターと初回到達時のローカル static。 |
| `delegates.vs` | `std::delegate` / `std::multicast_delegate`、小文字メンバー、キャプチャ、再バインド、ブロードキャストのスナップショット。 |
| `delegate_functions.vs` | 名前付き関数へのバインド、0～3 個の引数。VM と C++/C#/Python AOT。 |
| `reflection.vs` |型、フィールド、メソッド、宣言メタデータ、`mutable` フィールドのリフレクション。 |
| `reflection_executable.vs` | 型付きフィールド読み書き、インスタンス／静的／既定インターフェース呼び出し、参照引数と戻り値、値引数の分離。 |

## モジュール、前処理、およびパッケージ

|例 |メイントピック |
| --- | --- |
| `include_alias.vs` |エイリアスを含める/インポートします。 |
| `alias_exports.vs`, `alias_reexports.vs` | ファイル間の `export using` / `export typedef`、名前空間の検索とファサード経由の再公開。対応するモジュール／ファサードはインポート専用です。 |
| `macro_scope.vs` と `macro_scope_module.vs` | 入口スクリプトで通常の `#define` がモジュール内に限定されることを確認します。後者はインポート専用です。 |
| `macro_exports.vs`, `macro_exports_module.vs`, `macro_exports_facade.vs` | 公開オブジェクト・関数・可変引数マクロ、ファサードの再エクスポートと隔離。`macro_exports.vs` を実行します。 |
| `macro_implicit.vs` | 利用側にインポートを書かず公開マクロを使います。`veroscript --implicit-import macro_exports_facade.vs macro_implicit.vs` で実行します。 |
| `reexport_facade.vs` |モジュールの再エクスポートのファサード。 |
| `reexport_main.vs` |エントリモジュールからの宣言の再エクスポート。 |
| `preprocessor.vs` |プリプロセッサの条件とマクロ。 |
| `package/package_library.vs` |パッケージライブラリのソース。 |
| `package/package_app.vs` |パッケージ アプリケーションのエントリ ポイント。 |
|パッケージ例のメモ |パッケージのレイアウトとコマンドの例。 |

## 標準ライブラリ、ランタイム、およびツール

|例 |メイントピック |
| --- | --- |
| `memory_operations.vs` | 参照の同一性、バイナリバッファ、バイト順序、共有ビューと独立したコピー。 |
| `formatting.vs` | `print`、`printf`、`format`、および `formatf`。 |
| `path_helpers.vs` |パスの標準ライブラリ。 |
| `standard_library_algorithms.vs` |ジェネリック アルゴリズム、決定論的な乱数範囲、再現可能なシャッフル。 |
| `standard_namespace.vs` |推奨される `std::` 表記、`readonly` 値、明示的な変換。 |
| `windows_path_text.vs` | 任意のプラットフォームでの Windows 形式のパス文字列。ファイルシステムへのアクセスなし。 |
| `runtime_utilities.vs` | `range`、現在のディレクトリ、および時間ユーティリティ。 |
| `concurrent_demo.vs` | チャネル、関数・静的メソッド・ラムダによる callable タスク、および協調的なキャンセル。 |
| `script_tool.vs` | スクリプト ツールの引数、環境変数、文字列、アサーション、および終了ステータス。 |

## ホストとバインディングの構成サンプル

|ファイル |メイントピック |
| --- | --- |
| `workspace.analysis.sample.json` | 言語サーバーの解析設定。 |
| `bindings.sample.json` |最小限のホスト バインディング カタログ。 |

`workspace.analysis.sample.json` の内容を `initializationOptions.veroscript` または `workspace/didChangeConfiguration.settings.veroscript` として渡すか、対応する IDE 設定を適用してください。言語サーバーはこのファイルを自動では読み込みません。`bindingCatalogPaths` には実際のカタログの場所を指定し、設定を直接渡す場合は絶対パスを使用してください。

実行方法と規則の詳細は[サンプルディレクトリの説明](../../../Scripts/examples/README.md)を参照してください。言語ルールには [言語リファレンス](language-reference/index.md)、署名には [標準ライブラリ リファレンス](standard-library/index.md)、オプションには [CLI リファレンス](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md) を使用します。

- `console_demo.vs`: `std::console` (端末制御).

[アプリケーションの国際化 — std::i18n](standard-library/i18n.md): `i18n_demo.vs` · `VeroScript/Resources/Examples/I18n/`.

- [標準ライブラリ基盤](standard-library/foundation.md)：`standard_library_foundation.vs`

## variant

[variant — 動的な値ボックス](standard-library/variant.md): `variant.vs`.

```text
veroscript variant.vs
```
<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/examples.md) · [fr-FR](../fr-FR/examples.md) · [ja-JP](examples.md) · [ko-KR](../ko-KR/examples.md) · [zh-CN](../zh-CN/examples.md) · [zh-TW](../zh-TW/examples.md)
