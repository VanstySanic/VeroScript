# Python AOT と packaging

## スタンドアロンコードの生成

`VeroScriptPythonAot` は小さな compatibility runtime を含む readable Python module を emit し、VeroScript VM、Interop library、CPython extension を load しません。control wrapper は executable path、SDK root、`bin` directory、environment variable、`PATH` から user-provided CLI を探せます。

`FCodegenOptions` を使う SDK 呼び出しは、`Bindings` の注釈定義と
`CaseSensitive` の名前規則を処理段階へ渡します。注釈メタデータだけなら実行時の
依存関係は増えません。実行時のホスト関数とホスト型はサポートされません。

## bundle 契約

bundle mode は `__main__.py`、`__init__.py`、`_veroscript_runtime.py`、`_veroscript_program.py`、`python-aot.manifest.json` を書き出し、complete import graph を一 program として検査します。ファイルを一時ディレクトリに準備してから置換し、空の通常ディレクトリ、または構造が有効な VeroScript manifest を持ち無関係な内容がないディレクトリだけを対象にします。recognized interpreter `__pycache__` は許容される generated content です。

最終パスのシンボリックリンクや Windows junction は拒否されます。置換中は旧ディレクトリを退避するため、出力パスが一時的に存在しない場合があります。失敗時は復元を試みます。

## distribution 契約

wheel と source distribution は version-matched native extension、Interop library、Python source、type information、standard library、locale catalog を含みます。`VeroScriptVersion.h`、package metadata、imported runtime version は一致します。release validation は CPython/platform matrix、installed-package type surface、source-distribution rebuild を検査します。

Python スタンドアロンランタイムは `ValueError`／`OverflowError` を外へ出さず Core の浮動小数点結果へ変換します。ゼロの対数は負の無限大、累乗のオーバーフローは奇数整数指数の符号を保持し、floor／ceil／trunc は符号付きゼロを保持します。Python／C# の生成 NaN 文字列は Darwin を含む対象の表記に従います。エミッターの回帰検証で維持し、公開の有限値確認は [Math](../../../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/math.md#floating-results)を参照してください。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/aot-and-packaging.md) · [fr-FR](../fr-FR/aot-and-packaging.md) · [ja-JP](aot-and-packaging.md) · [ko-KR](../ko-KR/aot-and-packaging.md) · [zh-CN](../zh-CN/aot-and-packaging.md) · [zh-TW](../zh-TW/aot-and-packaging.md)
