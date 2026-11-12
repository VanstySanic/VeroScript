# 22: 格式化應用文字並保留快照

本練習使用標準程式庫 i18n.vs 與 VM。應用語言獨立於編譯器診斷語言，常值文字與具名替換不需外部資源提供者。

## 1. 儲存與執行

儲存為 application-text.vs，執行 veroscript --backend=vm application-text.vs，輸出如下。

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

## 2. 觀察與練習

uint64 維持精確輸出，常值括號原樣保留。修改 app.language 會改變版本，但不改變已擷取快照。宿主提供解析器後再使用資源鍵，單純選擇語言不會翻譯硬編碼字串。

[參考文件](../../../../../VeroScript/Resources/Docs/User/zh-TW/standard-library/i18n.md)

<!-- docs-navigation -->

---

[教學](../index.md)

語言: [en-US](../../en-US/standard-library/22-application-i18n.md) · [fr-FR](../../fr-FR/standard-library/22-application-i18n.md) · [ja-JP](../../ja-JP/standard-library/22-application-i18n.md) · [ko-KR](../../ko-KR/standard-library/22-application-i18n.md) · [zh-CN](../../zh-CN/standard-library/22-application-i18n.md) · [zh-TW](22-application-i18n.md)
