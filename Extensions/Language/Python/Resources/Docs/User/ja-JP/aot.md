# Python AOT

`veroscript aot --target=python` は、検査済みの VeroScript プログラムを
読みやすい Python ソースへ変換します。生成物には小さな互換ランタイムが
含まれ、VeroScript VM、`VeroScriptInterop`、`veroscript` 拡張を実行時に
読み込みません。

## 実行境界

生成物にはユーザーが用意する CPython 3.11–3.15 が必要です。Python 実行環境、
Python SDK、ホストバインディング、ネイティブ VeroScript ライブラリは同梱
されません。既存 Python アプリケーションへランタイムを組み込む場合は
[Python SDK](index.md) を使用してください。

生成された worker は OS スレッドで動作します。スクリプトのグローバルと
static フィールドは worker ごとのスナップショットから初期化され、worker
間で共有する同期機構は明示的な channel です。task ID は所属 worker に
限定され、別 worker から join や cancel はできません。

## 単一ファイル

```powershell
veroscript aot --target=python app.vs -o app.py
python app.py
```

アプリケーションの import が別の場所にある場合は `-I`/`--import-path`
を繰り返し指定できます。生成に成功した場合だけ出力ファイルが置換されます。

## ソース bundle

```powershell
veroscript aot --target=python app.vs --output-dir app_python --bundle-name app
python app_python
python -m app_python
```

bundle には `__main__.py`、`__init__.py`、`_veroscript_runtime.py`、
`_veroscript_program.py`、`python-aot.manifest.json` が含まれます。完全な
import graph を検査してから書き込み、無関係な内容を含むディレクトリは
置換しません。

ファイルを一時ディレクトリに準備してから通常ディレクトリを置換します。最終パスの
シンボリックリンクや Windows junction は拒否されます。置換中は旧ディレクトリを
退避するため、出力パスが一時的に存在しない場合があります。失敗時は復元を試みます。

## 対応範囲と制限

スカラー、文字列、コンテナ、関数、namespace、import、enum、class、struct、
interface、変換、例外、`switch`、`algorithm`/`numeric`/`random`、標準の
concurrent task wrapper をサポートします。

`ref`/`out`、ユーザー定義の開いた generic、overload 集合、destructor、
複数の具象基底、class default、重複する private 型名、`goto`/label、
`switch` fallthrough、host binding、source lambda、ローカル `static` 宣言は拒否されます。未対応
構文では診断を返し、不完全な出力は生成しません。

関数値は明示的な `fn(...) -> T` 型で宣言してください。関数値の `auto` 推論は
未対応です。名前空間で修飾された関数値はサポートします。`Super` レシーバーと
可変長引数の関数値は拒否しますが、可変長引数関数の直接呼び出しはサポートします。

`shared<T>` / `handle<T>` ラッパー、呼び出し可能オブジェクトを使うタスクの
エントリ、実行時リフレクションも拒否されます。これらの実行時機能は
Python SDK から利用できます。

終了処理では既存のチャネルを閉じ、同じ待機期限内に worker とそこから派生した各世代の task の完了を待ちます。終了処理中に作成したチャネルは閉じた状態で始まります。

生成プログラムは `VERO_JOIN_TIMEOUT_MS` で worker 終了時の待機時間を制限します。
既定値は 30000 ミリ秒です。worker が `exit(n)` を呼ぶとその task が失敗しますが、
親プロセスは終了しません。

独立して検査可能な Python ソースを配布する場合は Python AOT を、VM/JIT、動的ロード、
ホストバインディング、リフレクション、ホットリロード、保持 callable、実行時パッケージ
操作が必要な場合は [Python SDK](index.md) を使用してください。Python フロントエンド、
BindGen、トランスパイラーは別のソースツールチェーンです。

## ランタイムのメモリ機能

独立 AOT は現在、`std::memory` の照会、バッファー、ビュー、および関連する型に対応していません。実際の呼び出しやこれらの型の使用には明示的な生成エラーを返します。未使用の `memory.vs` または `core.vs` の import は引き続き可能です。

[Runtime SDK](index.md)

独立した Python AOT は `std::console` のテキスト出力、端末機能・サイズの照会、色・カーソル・行の制御をサポートします。出力がリダイレクトされている場合は端末機能なし、サイズゼロ、制御操作失敗を返します。不正な名前・モード・座標は引き続きエラーです。未知のコンソール組み込み関数はソース診断付きで拒否します。

Windows の色設定、スタイルのリセット、画面・行の消去とカーソル制御にはネイティブのコンソール API を使い、ANSI モードを有効にする必要はありません。サイズと座標は可視ウィンドウを基準とし、画面消去はウィンドウ外の履歴を保持します。

## 属性のソース出力

属性の設定比較は読み取り専用 builtin の例外です。`has_same_configuration(ref other)` は書き込み可能な別名を作りません。`try_get()` は対応し、`try_get(ref output)` は非対応のままです。操作ごとの境界は attribute リファレンスを参照してください。

[std::attribute](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/attribute.md)

<a id="native-bundle-api"></a>

ネイティブ SDK では `VeroScriptPythonAot.h` を使います。`FPythonAotSourceBundle` は `Files` と `ManifestJson` を持ち、`EmitPythonSourceBundleFromSource` / `EmitPythonSourceBundleFromFile` の後に `WritePythonSourceBundle` でステージ済みディレクトリを置き換えます。`BuildPythonSourceBundle` は生成済み Python をパッケージ化します。Python 制御ラッパーの `veroscript_executable` は CLI 実行ファイルまたは SDK ルート/bin を選択し、省略時は `VEROSCRIPT_EXECUTABLE`、`VEROSCRIPT_SDK`、PATH の順に探索します。この外部 CLI ラッパーは生成された単独プログラムと別です。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/aot.md) · [fr-FR](../fr-FR/aot.md) · [ja-JP](aot.md) · [ko-KR](../ko-KR/aot.md) · [zh-CN](../zh-CN/aot.md) · [zh-TW](../zh-TW/aot.md)
