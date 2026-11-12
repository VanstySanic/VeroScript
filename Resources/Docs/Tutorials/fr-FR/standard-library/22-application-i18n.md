# 22: Formater un texte applicatif et capturer son état

Cet exercice utilise i18n.vs et la VM. La langue de l’application est indépendante des diagnostics. Les textes littéraux et la substitution nommée ne nécessitent aucun fournisseur de ressources externe.

## 1. Enregistrer et exécuter

Enregistrez application-text.vs puis lancez veroscript --backend=vm application-text.vs. La sortie suit.

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

## 2. Observer et expérimenter

La valeur uint64 reste exacte et les accolades littérales sont conservées. Modifier app.language change sa version, pas l’instantané capturé. N’utilisez une clé de ressource qu’après configuration d’un résolveur par l’hôte. Changer de langue ne traduit pas une chaîne codée en dur.

[Référence](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/i18n.md)

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/standard-library/22-application-i18n.md) · [fr-FR](22-application-i18n.md) · [ja-JP](../../ja-JP/standard-library/22-application-i18n.md) · [ko-KR](../../ko-KR/standard-library/22-application-i18n.md) · [zh-CN](../../zh-CN/standard-library/22-application-i18n.md) · [zh-TW](../../zh-TW/standard-library/22-application-i18n.md)
