# 15：プロジェクトのオプションと診断言語を設定する

CLI オプションでインポートパス、定義、スクリプト引数を指定します。これらの設定は VeroScript の言語仕様を変更しません。

## 学習ファイルを用意する

対応する Runtime と同梱 VM・言語リソースを使います。書込可能な学習ディレクトリに `main.vs` と `src`/`shared` を作り、以下の三つのファイルを保存します。全コマンドはそのディレクトリから実行します。検索パスとマクロは一回の呼び出しに適用されるため、`main.vs` をチェック・実行する各コマンドに繰り返し指定します。

### main.vs

```VeroScript
import "score.vs";

int main(string[] args)
{
#ifdef FEATURE_SCORE
	print("score={}, argc={}", scoreValue(), len(args));
#else
	print("feature disabled");
#endif
	return 0;
}
```

### src/score.vs

```VeroScript
import "settings.vs";

export int scoreValue()
{
	return baseValue() + 2;
}
```

### shared/settings.vs

```VeroScript
export int baseValue()
{
	return 40;
}
```

`FEATURE_SCORE=1` を指定して `demo 42` を渡すと、`score=42, argc=2` を表示しゼロを返します。定義を外すと `feature disabled` の分岐になります。チェック成功時は `main` を呼ばずにゼロを返します。

## 手順 1：コンパイルオプションを設定する

`-I` でインポートパス、`-D` で定義を指定します。複数の値はオプションを繰り返して指定できます。CLI はこれらのオプションを直接読み取ります。

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

## 手順 2：実行バックエンドを選ぶ

このレッスンでは Runtime 同梱の VM を使うため、任意の JIT パッケージは不要です。以下のコマンドは `demo` と `42` を `main.vs` に渡し、通常の出力と終了結果を保ちます。別のポリシーとモジュール環境での選択は [CLI バックエンドの参照](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md#cli-backend-policy)を確認してください。

```powershell
veroscript --vm-backend=vs-bytecode --backend=vm -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

## ステップ 3: 実行前に確認する

実行前にエントリを確認してください。

```powershell
veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript -I src -I shared -D FEATURE_SCORE=1 main.vs demo 42
```

`main(string[] args)` はスクリプト パスの後に引数を受け取ります。パス自体は `args` にはありません。 CLI プロセスの終了コードは、`int main`、`void main`、または `exit(n)` の最終結果から取得されます。

## ステップ 4: 診断言語を選択する

`--lang` を使用して、1 つの CLI 呼び出しの言語を選択します。

```powershell
veroscript --lang ja-JP --check -I src -I shared -D FEATURE_SCORE=1 main.vs
veroscript --lang en-US --check -I src -I shared -D FEATURE_SCORE=1 main.vs
```

現在の PowerShell セッションに `VERO_LANG` を設定することもできます。

環境変数の例はチェック後に以前の `VERO_LANG` を復元します。

```powershell
$tutorialPreviousLanguage = $env:VERO_LANG
try
{
	$env:VERO_LANG = "ja-JP"
	veroscript --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
	$env:VERO_LANG = $tutorialPreviousLanguage
}
```

言語タグは配置されたカタログから検出されます。未翻訳のメッセージは英語に戻ります。明示的な `--lang` の値が未対応の場合は引数エラーになります。
無効な `static class` や `static struct` の宣言を含むパーサーの宣言診断は、CLI と LSP で同じ安定した ID とカタログを使用します。ツールのロジックでは診断 ID を使用し、翻訳されたメッセージは表示テキストとして扱ってください。

## ステップ 5: プロジェクトのローカリゼーション ディレクトリを指定する

配布物の `localization/` ツリー全体をプロジェクトにコピーし、完全な `en-US` 基準カタログを保持します。下の例は言語カタログを示しています。コピーした他のファイルも保持してください。

```text
localization/
  en-US/
    language.json
    ...
  ja-JP/
    language.json
    ...
```

`VERO_LOCALIZATION_DIR` でプロジェクトのツリーを明示的に選択します。実行ファイルの隣にあるカタログは、作業ディレクトリからの自動検出より優先されます。この例ではコマンドの実行後に以前の設定を復元します。

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang ja-JP --check -I src -I shared -D FEATURE_SCORE=1 main.vs
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

`en-US` が完全な基準を定義します。他の修飾された正規 BCP 47 ディレクトリには、英語ツリーに存在するファイルとメッセージ ID だけを含められます。翻訳されたファイルやキーが欠けると英語にフォールバックします。ファイルは文字列値を持つフラットな UTF-8 JSON オブジェクトとし、重複キーや末尾の追加内容を含めないでください。言語を追加するには、選択したツリーに正規ディレクトリと JSON ファイルを追加します。

## 演習

1. 一回の実行から `-D FEATURE_SCORE=1` を外して `feature disabled` を確認します。定義を戻し `demo 42` を渡して `score=42, argc=2` を確認します。
2. 3 つの引数をエントリに渡し、`main(string[] args)` から出力します。
3. プロジェクトのツリーを選択した状態で `ja-JP/language.json` から `diag.expected_expression` を削除し、以下のコマンドを実行して英語の「expected expression」が表示されることを確認します。

```powershell
$tutorialPreviousLocalization = $env:VERO_LOCALIZATION_DIR
try {
    $env:VERO_LOCALIZATION_DIR = (Resolve-Path ./localization).Path
    veroscript --lang ja-JP -e 'int main() { return +; }'
}
finally
{
    $env:VERO_LOCALIZATION_DIR = $tutorialPreviousLocalization
}
```

[CLI リファレンス](../../../../../VeroScriptCli/Resources/Docs/User/ja-JP/index.md) · [Localization](../../../../../VeroScript/Resources/Docs/User/ja-JP/localization.md)

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/toolchain/15-workspace-and-localization.md) · [fr-FR](../../fr-FR/toolchain/15-workspace-and-localization.md) · [ja-JP](15-workspace-and-localization.md) · [ko-KR](../../ko-KR/toolchain/15-workspace-and-localization.md) · [zh-CN](../../zh-CN/toolchain/15-workspace-and-localization.md) · [zh-TW](../../zh-TW/toolchain/15-workspace-and-localization.md)
