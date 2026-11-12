# Annotations et métadonnées source

Placez @Nom ou @Nom(arguments) avant une déclaration. Une annotation reste descriptive tant qu’une extension chargée ne lui attribue pas de comportement. Elle est distincte du conteneur de valeurs `std::attribute<T>`.

Les arguments conservent indicateurs, types scalaires, valeurs nommées et groupes imbriqués dans l’ordre source, y compris les doublons. Ce ne sont pas des expressions exécutables arbitraires. L’accès indexé préserve les doublons ; find_annotations suit la règle du nom enregistré ; find_arguments utilise le nom exact de l’argument. Le nom d’une annotation comprend @.

```VeroScript
/// Example metadata.
@Tag(data=(size=4, enabled=true), label="Example")
class Value
{
};

int main()
{
	type<Value> info = type<Value>();
	assert(info.has_annotation("@Tag"));
	annotation_info tag = info.annotation(0);
	assert(tag.name() == "@Tag");
	assert(tag.argument(0).argument(1).bool_value());
	print(tag.argument(0).argument(0).int64_value());
	return 0;
}
```

## Réflexion structurée

Voir [Réflexion — Réflexion structurée](reflection.md#reflection-metadata-contract).

[Réflexion](reflection.md) · [Commentaires](comments.md) · [std::compiler](../standard-library/compiler.md)

## Correspondance des noms

Les noms sont sensibles à la casse par défaut. Avec `annotation_definition.case_sensitive = false`, `@GuiWidget`, `@guiWidget` et `@GUIWIDGET` désignent une seule définition. Les abonnements, `has_annotation`, `find_annotations` et `annotation_info.matches_name(name)` suivent cette règle. `name()` et `raw_text()` conservent la graphie source ; `definition().name()` donne le nom enregistré et `definition().case_sensitive()` la règle. Les arguments et clés restent sensibles à la casse. `find_annotations` et `matches_name` exigent `@` ; il reste facultatif pour `has_annotation`. Les annotations non enregistrées restent sensibles à la casse.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md) · [en-US](../../en-US/language-basics/annotations.md) · [fr-FR](annotations.md) · [ja-JP](../../ja-JP/language-basics/annotations.md) · [ko-KR](../../ko-KR/language-basics/annotations.md) · [zh-CN](../../zh-CN/language-basics/annotations.md) · [zh-TW](../../zh-TW/language-basics/annotations.md)
