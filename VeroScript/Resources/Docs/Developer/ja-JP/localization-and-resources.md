# ローカライズ資源と配布

このページはカタログとビルド・配布ツールの保守担当者向けです。言語選択、独自カタログ、SDK の利用は[診断のローカライズ](../../User/ja-JP/localization.md)を参照してください。

## ソースの所有と配布

ソースの翻訳カタログは、各モジュールまたは領域の `Resources/Localization/<culture>/` で管理します。複数の Python ツールが使用するメッセージだけを Python 領域で共有します。ビルドと配布では、依存モジュールのカタログを配布先の `localization/` にまとめます。Runtime/Core SDK は CLI、Tooling、LSP、DAP、Interop の依存リソースを含み、WebAssembly はコアと C++、Python パッケージはコア、C++、Interop、Python ツールのカタログを含みます。IDE とインストーラーの UI カタログは独立しています。

```text
VeroScript/Resources/Localization/<culture>/
  language.json, runtime.json, shared.json, host-binding.json, memory.json, i18n.json
Extensions/Language/Cpp/VeroScriptCpp/Resources/Localization/<culture>/embedding.json
VeroScriptInterop/Resources/Localization/<culture>/interop.json
VeroScriptTooling/Resources/Localization/<culture>/
  tooling.json, lsp.json, dap.json
Extensions/Language/Python/Resources/Localization/<culture>/python-tools.json
Extensions/Language/Python/VeroScriptPythonFrontend/Resources/Localization/<culture>/python-frontend.json
Extensions/Language/Python/VeroScriptPythonBindGen/Resources/Localization/<culture>/python-bindgen.json
Extensions/Language/Python/VeroScriptPythonTranspiler/Resources/Localization/<culture>/python-transpiler.json
Extensions/Language/Python/VeroScriptPythonAot/Resources/Localization/<culture>/python-aot.json
VeroScriptInstaller/App/Resources/Localization/<culture>/
```

ソースツリーから直接実行する場合、コアは自身のカタログを検出し、各 SDK やツールモジュールが自身の Resources/Localization を追加します。明示的に読み込んだカタログと配布カタログが優先され、置換に成功した後にソースのリソースが自動追加されることはありません。

## ソース資源の検索

`VERO_LOCALIZATION_DIR` は完全なローカライズのルートを指定します。未設定または読み込みに失敗した場合、コアは次の順序で検索し、最初の有効なツリーで停止します。

1. 実行ファイルの隣の `localization`、続いて実行ファイルのディレクトリを基準にした `../assets/localization`。
2. 実行ファイルのディレクトリから最大八階層の親。それぞれの `localization`、続いてその親自体またはリポジトリの直下で検出したソースモジュールのコアカタログ。
3. 現在の作業ディレクトリ内の `localization`。
4. 現在のディレクトリ、親、さらにその親の順にソースモジュールを検出し、そのモジュールの `VeroScript/Resources/Localization` を読み込みます。

ソース検索は `VeroScript/Public/VeroScriptVersion.h` を含むモジュール、または `CMakeLists.txt` があり、そのような直下の子が一つだけあるリポジトリを受け入れます。外側のディレクトリ名は変更できます。見つからない場合や候補が曖昧な場合は無視します。`InitializeModuleFromEnvironment` は初期化でソースカタログを選んだ場合だけ、ソースモジュール相対の `Resources/Localization` を追加します。明示的に読み込んだツリーと配置済みツリーは独自のリソース集合を保持します。

## リリースチェック

- すべてのディストリビューションには、完全で有効な `en-US` ディレクトリが含まれている必要があります。
- リリースで必要な追加言語はすべて、ターゲット ホストにコピー、埋め込み、またはパッケージ化する必要があります。
- 翻訳されたカルチャ パスとメッセージ ID は、英語ベースラインのサブセットである必要があります。
- 古いドメイン優先の `en.json`/`zh-CN.json` レイアウトを使用しないでください。現在の形式は `<culture>/<component>.json` です。
- 診断 ID は表示テキストよりも安定しているため、ホスト、LSP、および DAP はロジックに `MessageId` を使用する必要があります。
- パッケージ化後、利用可能な言語セット、不足しているキーの英語フォールバック、および診断プレースホルダー置換を確認します。


## モバイルリソースと言語

共通モバイルカタログの一覧、配備パス、明示ロードの手順は [Mobile](../../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/ja-JP/index.md#mobile-localization) で管理します。

翻訳バッファーの所有権、エラー、エクスポートの保守は [Interop](../../../../../VeroScriptInterop/Resources/Docs/Developer/ja-JP/abi-and-lifetime.md#localization-abi) で定義します。

<!-- docs-navigation -->

---

[開発者文書](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/ja-JP/index.md)

言語: [en-US](../en-US/localization-and-resources.md) · [fr-FR](../fr-FR/localization-and-resources.md) · [ja-JP](localization-and-resources.md) · [ko-KR](../ko-KR/localization-and-resources.md) · [zh-CN](../zh-CN/localization-and-resources.md) · [zh-TW](../zh-TW/localization-and-resources.md)
