# 診断ローカリゼーション

このページは診断とホストツールの表示言語を説明します。読み込んだカタログは表示を選択し、スクリプトの意味は変えません。アプリケーションの文言には独立した [std::i18n API](standard-library/i18n.md) を使います。アプリのコンテキストを変更してもプロセスの診断言語は変わりません。

言語選択は、読み込まれたカタログにある診断に適用されます。CLI のヘルプと一部の引数検証メッセージは現在英語です。カタログにないメッセージは元のテキストを保持します。

## サポートポリシー

標準パッケージは `en-US`、`fr-FR`、`ja-JP`、`ko-KR`、`zh-CN`、`zh-TW` を提供します。利用可能な言語は実際にロードされたカタログによって決まります。`GetAvailableLanguageTags` または対応する SDK API で確認してください。有効なカルチャディレクトリを追加すると別の言語を使えます。そのディレクトリをアプリケーションに同梱するか、カタログルートを明示的にロードしてください。

アプリケーションでは、`en-US`、`zh-CN`、`zh-TW` などの正規の BCP 47 タグを使用する必要があります。ランタイムは、`ja_JP.UTF-8`、`@modifier` のタグ、`C`/`POSIX` などの一般的なオペレーティング システムの形式も正規化します。解決された形式では、ハイフン、小文字の言語、タイトル文字のスクリプト、および大文字のアルファベットの領域サブタグが使用されます。

前後の空白とエンコーディング・修飾子の接尾辞を取り除いてから `C`/`POSIX` を `en-US` に解決します。`C.UTF-8` と `POSIX.UTF-8@modifier` にも対応します。

## マッチングとフォールバック

言語解決は次のように行われます。

1. すでにロードされている正規タグと正確に一致します。
2. 最後のサブタグを繰り返し削除し、親タグを試します。
3. 同じ第一言語から決定的な候補を選択します。
4. 候補が存在しない場合、明示的選択 API は失敗を報告し、初期化は `en-US` に戻ります。

中国語の文字体系は保持されます。`zh-Hant-HK` は `zh-TW`、`zh-Hans` は `zh-CN` に一致できます。明示的な `Hans` または `Hant` サブタグは地域からの推定より優先され、拡張や私用のサブタグは文字体系の選択に使いません。既定のカタログでは、`zh-Hans-TW` は `zh-CN`、`zh-Hant-CN` は `zh-TW` に一致します。簡体字または繁体字を要求したときに、別の文字体系を黙って選択することはありません。翻訳ファイルやメッセージキーが欠けている場合、英語の基準カタログに対応する項目があれば `en-US` のテキストにフォールバックします。

`TrySetLanguageFromTag` が成功すると、アクティブな言語は実際に一致した正規タグになります。選択に失敗すると、現在の言語は変更されません。 `GetAvailableLanguageTags` は、現在カタログからロードされている言語を返します。

英語の基準カタログにない診断は元のテキストを保持します。翻訳カタログを読み込んでもスクリプトの出力や任意のアプリケーション文字列は翻訳されません。

## 言語の選択

CLI は `--lang` を使用します。環境変数は `VERO_LANG` です。

```powershell
veroscript --lang ja-JP --check script.vs
$env:VERO_LANG = "en-US"
veroscript --check script.vs
```

CLI はコマンドライン引数を解析する前に環境とカタログを初期化するため、`--lang` は `VERO_LANG` をオーバーライドします。 `VERO_LANG` はオペレーティング システムの言語をオーバーライドします。どちらの値も解決できない場合、初期化では `en-US` が使用されます。明示的にサポートされていない `--lang` は引数エラーとして報告されます。

editor 管理の Language Server session では、document を open する前に BCP 47
preference を `initialize.locale` に設定します。`workspace/didChangeConfiguration`
は analysis 設定を変更できますが、language は次の initialize で適用し、open
document が同じ catalog を使うようにしてください。`veroscript-lsp` は CLI と同じ
`tooling.json` および compiler catalog を使用します。logic では翻訳 text ではなく
diagnostic ID を使用してください。
`static class` や `static struct` の制約を含むパーサーの宣言診断も、同じ安定した
ID とカタログ パスを使用します。そのため CLI と LSP は、同じソース エラーに対して
同じローカライズ済みメッセージを選択します。

## ディレクトリのレイアウト

ローカリゼーション ルートには、完全な `en-US` ベースラインが含まれている必要があります。他のすべての直接ディレクトリはカルチャ ディレクトリであり、その名前は修飾された正規の BCP 47 タグである必要があります。現在、VeroScript ではハイフンで区切られた修飾カルチャ名が必要であるため、裸の `zh`、`ja`、または非正規の `zh_cn` ディレクトリは拒否されます。

デフォルトのカタログ レイアウトは次のとおりです。

```text
localization/
  en-US/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
  <culture>/
    embedding.json
    host-binding.json
    interop.json
    i18n.json
    memory.json
    language.json
    runtime.json
    shared.json
    tooling.json
    lsp.json
    dap.json
```

ツリーは、同じルールを維持しながら、さらにカルチャを追加したり、JSON ファイルをサブディレクトリに分割したりできます。

```text
localization/
  en-US/
    runtime.json
    tooling/compiler.json
  <culture>/
    runtime.json
    tooling/compiler.json
```

ローダーは `.json` ファイルを再帰的に検出し、相対パスによってそれらをマージします。翻訳されたあらゆる文化について:

- 翻訳された各ファイルは、`en-US` の下に同じパスを持つ必要があります。
- 翻訳されたファイルには、英語の対応するファイルにないメッセージ ID を追加することはできません。
- 翻訳されたファイルまたはキーが欠落している場合は、英語のテキストが使用されます。
- 1 つのカルチャ内のファイルでは重複したメッセージ ID を定義できません。
- カルチャ ディレクトリは、複製したり、空にしたり、非正規にしたり、修飾したりすることはできません。

各 JSON ファイルは、キーと値が文字列である UTF-8 フラット オブジェクトである必要があります。

```json
{
	"diag.example": "Example message",
	"diag.with_argument": "Value: {0}"
}
```

テキストは、`{0}` ～ `{9}` を使用して診断引数を受け取ることができます。 JSON は、重複キー、無効なエスケープ、無効な UTF-8、末尾のコンテンツ、または文字列以外の値のない完全なオブジェクトである必要があります。

## ロード、置換、およびマージ

`VERO_LOCALIZATION_DIR` で完全なローカライズルートを指定します。ツールの起動または SDK の初期化前に設定してください。有効な明示指定がなければ、実行ファイル付近の配布資源（`localization`、`../assets/localization`）を作業ディレクトリの資源より先に探します。独自カタログを確実に使うには、この環境変数または `LoadCatalogDirectory` を使用してください。自動検索はカタログがまだロードされていない場合にのみ行われます。

`LoadCatalogDirectory` は、プロセス全体の完全なカタログ ツリーを検証して置き換えます。ロードが失敗した場合、以前にロードされたツリーはアクティブなままになります。 `MergeCatalogDirectory` は、その引数が完全に有効なカタログ ツリーであることを要求し、その後、すべてのカルチャをアクティブ セットにマージします。既存のメッセージ ID はデフォルトで保持され、オーバーライドが明示的に有効になっている場合にのみ置き換えられます。どちらの操作も、コアのスレッドセーフなプロセス全体のローカリゼーション状態を使用します。

標準ライブラリ ルート、ライブラリ パッケージ ルート、またはモジュール ファイルの兄弟は、`localization/` ディレクトリを持つことができます。モジュール ローダーは、既存の ID を上書きせずにこれらのツリーをマージするため、ライブラリはホストのテキストを予期せず変更することなく独自のメッセージを追加できます。ソース オーバーレイは論理モジュール パスを保持するため、兄弟カタログは引き続き検出可能です。

## SDK の境界

- C++ ホストは `VeroScript::Locale` API を直接使用します。言語とカタログの状態は、コア内のプロセス全体に影響します。
- Interop は、`vs_interop_locale_set_language`、`vs_interop_locale_get_language`、`vs_interop_locale_get_available_languages`、`vs_interop_locale_load_catalog_directory`、および `vs_interop_locale_merge_catalog_directory` を通じて同じポリシーを公開します。
- WebAssembly/WASM ビルドでは、仮想ファイル システムに完全なビルド時 `localization` ツリーが埋め込まれます。 `setLanguage` は、そのビルドによってサポートされるカルチャのみを選択できますが、`language` と `availableLanguages` は、正規の現在の言語と利用可能なカタログを報告します。

CLI オプションと環境変数については [コマンドライン ツール](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md)、C++ ホストについては [ホスト統合](../../../../../Extensions/Language/Cpp/Resources/Docs/User/ja-JP/embedding.md)、C ABI については [Interop SDK](../../../../../VeroScriptInterop/Resources/Docs/User/ja-JP/index.md)、Web 境界については [WebAssembly/WASM SDK](../../../../../Extensions/Platform/WebAssembly/Resources/Docs/User/ja-JP/index.md) を参照してください。

独立した Python Frontend、BindGen、Transpiler、AOT ツールは、ネイティブランタイムとは別に `python_tools.*` のメッセージを選びます。`--lang` は `VERO_LANG` とシステム言語より優先されます。システムのタグを正規化して中国語の字体を保持し、完全一致、親、同じ言語の決定的な候補の順に検索します。不足分は英語を使います。ネイティブ SDK のプロセス全体の言語は変更しません。[Python frontend](../../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/frontend.md)を参照してください。


## モバイル言語の明示設定

ネイティブ Android AAR と iOS C ABI SDK は 6 言語のカタログを提供し、明示的な読み込みが必要です。スクリプト実行前にツリー全体をアプリ管理の実ディレクトリへコピーします。APK アセットパスはファイルシステムのディレクトリではありません。Android AAR のリソースは `veroscript/localization`、iOS SDK は `assets/localization` にあります。Android の `VeroScriptEngine.loadCatalogDirectory(path)` と `setLanguage(tag)` は成功を表す bool を返します。iOS C ABI は `vs_interop_locale_load_catalog_directory` と `vs_interop_locale_set_language` を提供します。エラーバッファーを `vs_interop_buffer_destroy` で解放し、すべての戻り値を確認してください。

C# モバイルアプリは `VeroScriptMobileResources.InstallAsync` でリソースを配置し、`VeroScriptRuntime.LoadCatalogDirectory` を呼び出してから `VeroScriptRuntime.CurrentLanguage` を設定します。Python は `veroscript` のインポート時に隣接する `veroscript/localization` ツリーを読み込みます。配置したパッケージ内でツリーを保持し、その後に `veroscript.set_language` を呼び出します。[C# モバイル統合](../../../../../Extensions/Language/CSharp/Resources/Docs/User/ja-JP/mobile.md)と [Python モバイル統合](../../../../../Extensions/Language/Python/Resources/Docs/User/ja-JP/mobile.md)を参照してください。

言語設定はエンジン別ではなくプロセス全体で共有されます。選択に失敗すると現在の言語を保持します。スクリプトの出力は変わりません。カタログにないメッセージは元のテキストを保持し、メモリ確保失敗時も英語のままになる場合があります。

<!-- docs-navigation -->

---

[ユーザー文書](index.md) · [VeroScript](../../../../../Resources/Docs/User/ja-JP/index.md)

言語: [en-US](../en-US/localization.md) · [fr-FR](../fr-FR/localization.md) · [ja-JP](localization.md) · [ko-KR](../ko-KR/localization.md) · [zh-CN](../zh-CN/localization.md) · [zh-TW](../zh-TW/localization.md)
