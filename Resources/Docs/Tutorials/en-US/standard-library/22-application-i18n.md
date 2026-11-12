# 22: Format application text and retain a snapshot

This exercise uses the standard i18n.vs module with VM execution. Application language is independent from compiler diagnostics. No external resource provider is required for literal text and named substitution.

## 1. Save and run

Save as application-text.vs and run veroscript --backend=vm application-text.vs. Output follows.

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

## 2. Observe and experiment

The uint64 value is rendered exactly. Literal braces are preserved. Changing app.language changes its version but not the captured snapshot. Replace the literal text with a resource key only after the host supplies a resolver. Merely selecting a language does not translate hard-coded strings.

[Reference](../../../../../VeroScript/Resources/Docs/User/en-US/standard-library/i18n.md)

<!-- docs-navigation -->

---

[Tutorials](../index.md)

Language: [en-US](22-application-i18n.md) · [fr-FR](../../fr-FR/standard-library/22-application-i18n.md) · [ja-JP](../../ja-JP/standard-library/22-application-i18n.md) · [ko-KR](../../ko-KR/standard-library/22-application-i18n.md) · [zh-CN](../../zh-CN/standard-library/22-application-i18n.md) · [zh-TW](../../zh-TW/standard-library/22-application-i18n.md)
