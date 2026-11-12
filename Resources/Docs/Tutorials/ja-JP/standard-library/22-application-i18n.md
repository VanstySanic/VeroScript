# 22: アプリの文言とスナップショット

i18n.vs と VM を使います。アプリ言語と診断言語は独立し、リテラルと名前付き置換には外部資源プロバイダーは不要です。

## 1. 保存して実行する

application-text.vs として保存し、veroscript --backend=vm application-text.vs を実行します。出力を以下に示します。

```VeroScript
import "i18n.vs";

void main()
{
	std::i18n::context app = std::i18n::context();
	std::i18n::arguments args = std::i18n::arguments();
	args.set("name", std::i18n::argument("Ada"));
	args.set("count", std::i18n::argument(18446744073709551615u64));
	auto captured = app.snapshot();
	print(captured.format("Hello {name}: {count}", args));
	print(captured.resolve(std::i18n::text("{literal}")));
	app.set_language("fr-FR");
	assert(app.version() != captured.version());
}
```

```text
Hello Ada: 18446744073709551615
{literal}
```

## 2. 確認と練習

uint64 は正確に表示され、リテラルの波括弧も残ります。app.language の変更は版を変えますが取得済みスナップショットを変えません。資源キーはホストのリゾルバー設定後に使ってください。言語選択だけではハードコード文字列を翻訳しません。

[リファレンス](../../../../../VeroScript/Resources/Docs/User/ja-JP/standard-library/i18n.md)

<!-- docs-navigation -->

---

[チュートリアル](../index.md)

言語: [en-US](../../en-US/standard-library/22-application-i18n.md) · [fr-FR](../../fr-FR/standard-library/22-application-i18n.md) · [ja-JP](22-application-i18n.md) · [ko-KR](../../ko-KR/standard-library/22-application-i18n.md) · [zh-CN](../../zh-CN/standard-library/22-application-i18n.md) · [zh-TW](../../zh-TW/standard-library/22-application-i18n.md)
