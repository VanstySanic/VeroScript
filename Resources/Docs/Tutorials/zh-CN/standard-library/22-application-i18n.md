# 22: 格式化应用文本并保留快照

此练习使用标准库 i18n.vs 和 VM。应用语言独立于编译器诊断语言，字面文本与具名替换不需要外部资源提供者。

## 1. 保存与运行

保存为 application-text.vs，运行 veroscript --backend=vm application-text.vs，输出如下。

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

## 2. 观察与练习

uint64 保持精确输出，字面花括号原样保留。修改 app.language 会改变其版本，但不改变已捕获快照。宿主提供解析器后再使用资源键，单纯选择语言不会翻译硬编码字符串。

[参考文档](../../../../../VeroScript/Resources/Docs/User/zh-CN/standard-library/i18n.md)

<!-- docs-navigation -->

---

[教程](../index.md)

语言: [en-US](../../en-US/standard-library/22-application-i18n.md) · [fr-FR](../../fr-FR/standard-library/22-application-i18n.md) · [ja-JP](../../ja-JP/standard-library/22-application-i18n.md) · [ko-KR](../../ko-KR/standard-library/22-application-i18n.md) · [zh-CN](22-application-i18n.md) · [zh-TW](../../zh-TW/standard-library/22-application-i18n.md)
