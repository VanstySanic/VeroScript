# Rapports d’annotations Schema

FSchema::TryValidateWithOptions et schema.validate_with_options rendent erreurs et annotations ; TryIsValid/is_valid garde le mode rapide sans collecte. Seules les branches évaluées avec succès contribuent ; branches échouées/non choisies et $defs inutilisés exclus. anyOf collecte toutes les branches réussies, contains tous les éléments correspondants. Si le résultat global est invalide, les annotations sont abandonnées, les erreurs conservées.

InstancePointer → instance_pointer, SchemaLocation → schema_location, EvaluationPath → evaluation_path, Keyword → keyword, Value → value : JSON Pointer d’instance, URI absolue du mot-clé, chemin d’évaluation avec $ref, nom et valeur JSON immuable exacte. title/description/default/examples/deprecated/readOnly/writeOnly/format, métadonnées content applicables et mots-clés inconnus ordinaires sont collectés. contentSchema seulement avec contentMediaType sur une chaîne, sans décodage/validation implicite. $comment exclu ; default ne modifie pas l’instance.

Annotations d’évaluation : noms de propriétés pour properties/patternProperties/additionalProperties/unevaluatedProperties ; dernier indice ou true pour prefixItems ; indices ou true pour contains ; true si items/unevaluatedItems s’applique. Les marques enfants ne fusionnent pas dans le parent. Ordre reproductible pour mêmes entrées/options ; lisez par chemin/mot-clé, pas indice fixe.

FSchemaReportOptions collecte par défaut, MaxIssues=MaxAnnotations=256. Erreurs 1–16384, annotations 0–16384. Atteindre la limite d’annotations conserve la validité et fixe bAnnotationsTruncated/bTruncated ; une branche échouée abandonne aussi sa troncature. MaxAnnotations=0 détecte encore les omissions ; désactivez via bCollectAnnotations=false. Limits/FWork bornent travail, octets d’annotations, nœuds/profondeur et annulation. Exécution incomplète : false, ancien rapport conservé, erreur Cancelled/Limit/Allocation distincte. Les valeurs survivent aux Schema/entrées/résolveurs ; validations concurrentes avec rapports séparés.

TryValidate/schema.validate utilisent les options d’annotations par défaut ; MaxIssues/Limits et validate_with_limits restent effectifs. Structure stable du module, sans promesse de sortie verbose standard. schema_report : annotation_count/annotation/annotations_truncated ; schema_report_options : limits/max_issues/max_annotations/collect_annotations et setters. Entrée contrôlée : try_validate_schema_with_options(schema,value,options,ref report,ref error). Échec ordinaire conserve le rapport ; annulation ou livraison impossible échouent au niveau natif.

```VeroScript
json::schema schema = json::compile_schema(json::parse("{\"title\":\"Count\",\"type\":\"integer\"}"));
json::schema_report_options options = json::schema_report_options();
options.set_max_annotations(16);
json::schema_report report = schema.validate_with_options(json::from_int(3), options);
print(report.annotation(0).get("value").as_string());
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/schema-annotations.md) · [fr-FR](schema-annotations.md) · [ja-JP](../../ja-JP/Reference/schema-annotations.md) · [ko-KR](../../ko-KR/Reference/schema-annotations.md) · [zh-CN](../../zh-CN/Reference/schema-annotations.md) · [zh-TW](../../zh-TW/Reference/schema-annotations.md)
