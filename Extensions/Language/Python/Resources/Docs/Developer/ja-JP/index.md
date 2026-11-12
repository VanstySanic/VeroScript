# VeroScript Python 技術文書


この directory は maintainer と binding author 向けの source/binary contract です。Python/VeroScript syntax、API/module name、option、JSON key、diagnostic は source のままで、user tutorial ではありません。

## モジュールの責務

`VeroScriptPython` は stable C Interop ABI 上の CPython adapter、`VeroScriptPythonAot` は standalone source/bundle emitter、`VeroScriptPythonFrontend` は input を実行しない typed Python 3.11 subset parser、`VeroScriptPythonBindGen` は Catalog/API reference generator、`VeroScriptPythonTranspiler` は typed subset lowering/validation tool です。

## 静的ツールの境界

## runtime と generated-code boundary

Frontend、BindGen、Transpiler は static tool で、input module を import/execute せず runtime side effect を使用しません。unsupported dynamic feature、untyped signature、preserve できない arbitrary-precision semantics、async、reflection、inheritance は明示的に失敗します。project mode は complete module graph を解決し、package layout/module/import/source map/API reference/manifest を一貫させ、failure 時に output を置換しません。

CPython adapter は可能な native work で GIL を release し callback で reacquire します。creator-thread rule、callable ownership、callback reentrancy、shared Python state synchronization は public contract であり、worker callback は confined session operation を呼べません。standalone AOT runtime は documented executable subset の integer/container/exception/module/concurrency semantics を保存し、OS-thread worker の global/static field を分離して channel identity を明示的 shared mechanism とします。

## Python object と artifact detail

`Session`、`Value`、`Package`、retained callable、`HostParameter`、host registration が primary runtime object です。session は creator thread で close し、container は explicit element type、iteration と `keys()`/`values()`/`items()` は owned snapshot です。callback-owned state は同期します。AOT bundle replacement は valid manifest 以外の foreign content を許さず、`__pycache__` だけを tolerated generated extra とします。ファイルは一時ディレクトリに準備し、最終パスのリンクや Windows junction は拒否します。置換中は出力パスが一時的に存在しない場合があり、失敗時は復元を試みます。

## packaging と versioning

wheel/source distribution は matching extension/Interop/Python/type/stdlib/localization を含み、`VeroScriptVersion.h` が version authority です。frontend project mode は AST artifact/input と project manifest、BindGen は merged Catalog/reference/input と `bindgen.manifest.json`、Transpiler は relative layout/import/source map と `transpile.manifest.json` を complete graph validation 後に emit します。

## 詳細ページ

- [Python runtime と static tool](runtime-and-tools.md)
- [Python AOT と packaging](aot-and-packaging.md)
- [Python toolchain と package 契約](toolchain-and-packaging.md)

## 仕様の基準となるソース

Python source package、native adapter、frontend AST、generator/transpiler test、CMake、package metadata、distribution validator を根拠とし、subset/threading/artifact/layout/ABI mapping の変更と同時に更新します。


[Android/iOS の Python](mobile.md)

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/index.md) · [fr-FR](../fr-FR/index.md) · [ja-JP](index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
