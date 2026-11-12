# Référence JSON

Les captures par valeur des callbacks de script suivent les règles de liaison en lecture seule de Core. Pour appeler une méthode native qui modifie une valeur hôte capturée, utilisez un lambda `mutable` ou une capture par référence d’une liaison modifiable. Les captures avec initialisation et les variables statiques locales suivent la sémantique habituelle de Core.

`VeroScriptJson.h` fournit une API C++17 indépendante, liée avec `VeroScript::Json`, sans VM. Installez le paquet natif `vs-json` compatible avant de compiler un script utilisant `json`. La conversion d’objets de script exige aussi le processeur de compilation du module ; un catalogue de liaisons seul ne l’installe pas.

Les six catégories de valeurs sont des instantanés partagés immuables, avec texte numérique original, comparaison décimale exacte et conversions contrôlées. Une modification ne se valide qu’en cas de succès. Les lectures peuvent partager un instantané ; synchronisez les mutations d’un même objet/builder. Try* efface l’erreur au succès et conserve la sortie à l’échec ; constructeurs et fabriques peuvent encore lever une erreur d’allocation. Le texte d’erreur peut alors être vide : utilisez FError::Code et Id(), sans allocation. Une exception du contrôle de travail donne Callback ; celle du sink donne Sink, ou Allocation. Tailles/indices : int64. Profondeur DOM par défaut et maximale : 256. as_double peut arrondir ; demandez explicitement une conversion exacte.

FBuilder évite les copies répétées des grands conteneurs. Ses copies partagent l’état, TryFinish le ferme et TryReset le réutilise. FPointer (RFC 6901), Patch (RFC 6902) et Merge Patch (RFC 7396) préservent les valeurs exactes ; FDocument::bHasValue représente la suppression de la racine.

FReader::Feed rend le nombre d’octets consommés : conservez et resoumettez le suffixe, lisez chaque EventReady, marquez le dernier bloc et continuez jusqu’à Done. Un bloc non final `1` puis `2 ` produit `12`. Visit appelle le visiteur synchroniquement ; false suspend après l’événement courant sans le répéter ; une exception impose un reset de Failed. Les contextes sont empruntés pendant l’appel. FWriter::Write indique bAccepted : après acceptation, appelez Resume, sans resoumettre. `0 + NeedOutput` est une contre-pression, `0 + Progress` une erreur. Finish peut se suspendre et se répéter après la fin. Le flux conserve l’ordre ; triez avec TryWrite du DOM.

Les options de script json::limits/parse_options/write_options sont copiées par valeur ; modifiez une copie retournée puis set_limits. Limites : input_bytes/output_bytes/token_bytes/nodes/depth/number_bytes/exponent/work_units et setters. Les quantités sont des int64 non négatifs ; augmenter depth ne dépasse pas 256. duplicate_keys : 0 Reject, 1 KeepFirst, 2 KeepLast, 3 ReportAll (reader/SAX seulement). allow_leading_bom contrôle le BOM ; pretty/sort_keys/indent (0–16) contrôlent l’écriture. Entrées : parse_with_options, try_parse_with_options, write_with_options, make_reader_with_options, make_writer_with_options.

Fonctions contrôlées : try_parse, try_write, try_write_with_options, try_number, `try_from_string`, `try_from_double`, try_compile_pointer, try_as_int64/uint64/double/double_exact/bool/string, try_resolve, try_patch, try_merge_patch. Les références de résultat précèdent celle d’erreur ; les variantes avec options suivent les déclarations publiques. Une erreur de données rend false et conserve le résultat, avec json::error ; annulation et impossibilité de livrer les objets échouent toujours au niveau natif. Accesseurs : code/message/message_id/has_source/byte_offset/line/column/instance_pointer/patch_operation.

array_builder/object_builder proposent push/set/reserve/size/capacity/finish, reset_array/reset_object et variantes try. Les alias voient le même état ; finish produit un DOM indépendant. Pour try_finish/size/capacity, sortie en première référence et erreur en dernière. Le DOM fournit try_contains/try_keys. Reader : 0 NeedInput, 1 EventReady, 2 Done, 3 Failed ; event() possède ses données. Writer : 0 Active, 1 NeedOutput, 2 Done, 3 Failed ; write indique l’acceptation, take retire au plus 64 Kio, puis resume continue. Consultez error et réinitialisez explicitement.

Schema suit Draft 2020-12 : nombres exacts, références dynamiques, combinaisons unevaluated et regex Unicode ECMAScript. format et content restent des annotations sans décodage implicite. schema_options fournit retrieval_uri, limites et nombre de ressources ; URI vide : urn:veroscript:json-schema:root. compile_schema_with_options(schema,options,resources) reçoit un objet URI → Schema. C++ peut emprunter un résolveur synchrone durant la compilation ; le résultat possède toutes les ressources. Aucun téléchargement ni résolution par cwd. La méta-Schema standard et ses huit vocabulaires sont intégrés.

Compilation/validation bornent travail, annulation, nœuds, texte et profondeur. URI : 64 Kio ; motif regex : 4096 octets ; allocations retenues d’une exécution regex : 16 Mio. Les protections de pile/étapes peuvent aussi produire Limit. Les rapports comprennent 1–16384 erreurs, enfants inclus ; tronquer ne signifie jamais réussir. schema_report : valid/truncated/size/issue, avec details JSON.

Autres entrées : try_compile_schema, try_validate_schema, try_compile_schema_with_options(value,options,resources,ref schema,ref error), try_validate_schema_with_limits(schema,value,max_issues,limits,ref report,ref error), try_compile_pointer_with_limits, pointer.try_resolve[_with_limits], document.try_get. Une racine absente conserve la sortie et rend false. Les pointeurs compilés revérifient les limites de jetons/profondeur/travail à chaque utilisation.

- [Conversion C++](native-mapping.md)
- [Conversion des objets de script](script-mapping.md)
- [Conversion polymorphe fermée](polymorphic-mapping.md)
- [Rapports d’annotations Schema](schema-annotations.md)
- [Formats et dialectes Schema](schema-formats.md)
- [Diagnostics et localisation](diagnostics.md)

```VeroScript
json::schema schema = json::compile_schema(json::parse("{\"type\":\"integer\",\"minimum\":0}"));
bool valid = schema.is_valid(json::from_number("1.0"));
json::schema_report report = schema.validate(json::from_string("bad"));
print(report.issue(0).get("keyword").as_string());
```

```cmake
find_package(VeroScriptJson 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(app PRIVATE VeroScript::Json)
```

`FSchemaOptions::MaxResources = 256`

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
