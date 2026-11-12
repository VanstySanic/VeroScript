# Conversion des objets de script

json::converter<T> convertit les objets dans une VM active et génère les Schema d’entrée/sortie. Enregistrez l’extension native et transmettez bindings, processeurs, dépendances et Owners à l’environnement avant compilation. Le projet déclare sa dépendance au fournisseur json. Le catalogue seul ne fournit pas le processeur. L’API C++ native est indépendante.

write(ref value) prend explicitement une const ref. write_with_options/read_with_options/try_write/try_read et leurs variantes conservent le contrat : échec ordinaire false, json::error et sortie inchangée ; succès avec erreur effacée. Annulation, délai ou échec VM irrécupérable terminent l’exécution.

Bool/entier/flottant fini/string gardent leur type et leurs bornes sans convertir l’entier en double. Enums : noms, modifiables par JsonName. array/deque/queue/stack/priority_queue suivent values(), pile du bas vers le haut. set refuse les doublons, multisets les conservent. map/ordered_map à clé string donnent des objets ; les autres maps et tous multimaps exigent key_value_pairs. Champs d’instance publics des struct/class ; classes éventuellement null ; json::value inchangé. L’ordre hash suit son contrat, les conteneurs ordered conviennent à l’ordre d’insertion. Types hôtes particuliers et fonctions demandent un convertisseur.

JsonName(value) renomme ; JsonIgnore saute entrée/sortie ; JsonRequired impose la présence, sans décider la nullabilité ; JsonSerializeOnly interdit l’entrée ; JsonDeserializeOnly omet la sortie ; JsonProperty(name,setter) ajoute un getter const sans argument (sans setter : sortie seule) ; JsonFactory(type) utilise create(value,context) ; JsonConverter(type) choisit l’adaptateur. Les champs publics hérités accessibles sont inclus, statiques/inaccessibles exclus, const en sortie seule. Les champs absents non obligatoires gardent leurs valeurs construites. Doublons/conflits échouent, champs inconnus refusés sauf set_ignore_unknown(true).

mapping_options : ignore_unknown/numeric_enums/key_value_pairs/exact_floating et setters, set_limits(depth,nodes), profondeur 1–256 et nœuds positifs. Configuration copiée par valeur, contexte indépendant par conversion. instance_pointer suit JSON Pointer. Cycles de classes refusés ; références entre branches répétées par valeur. Le décodage valide un nouvel objet uniquement au succès ; ses effets externes ne sont pas annulés.

L’adaptateur fournit statiquement encode(const Target&,json::value&,mapping_context), decode(json::value,Target&,mapping_context), schema(mapping_context). custom_converter<Target,Adapter> exige aussi create(value,context) -> Target. Struct sans constructeur par défaut : fabrique/create ; class peut partir de null. context.fail/fail_range/adopt(error) transmettent les erreurs ; false sans diagnostic devient Callback. Les appels imbriqués converter::encode/decode/schema_nested partagent le contexte. La Schema personnalisée décrit la vraie conversion.

schema() produit l’entrée, schema_with_options(options,false) la sortie. Champs obligatoires/ignorés/inconnus/interdits, bornes, éléments, enums et nullabilité dépendent de la direction. $defs permet la récursion sans exécuter de constructeur. Le cache schema_nested distingue type et direction ; set_schema_input ne peut réutiliser la mauvaise définition.

La génération automatique vise les types directement déclarés dans module/namespace. Les types locaux/imbriqués n’obtiennent pas d’aides de module ; JsonFactory/JsonConverter y signale la restriction. Utilisez custom_converter dans une portée accessible. JsonPolymorphic génère ses méthodes dans une classe table non générique, éventuellement locale/imbriquée. Visibilité, propriété, accès et construction ordinaires restent applicables. Résolvez champs, alias, bases et adaptateurs dans leur module, suivant imports/réexports, arité/défauts et priorité des modules. Paramètres de type/valeur (int/bool/enum) et constexpr qualifiés sont conservés. Types fermés distincts : caches distincts ; statiques constexpr exclus.

Les .vslib compilées avec le processeur conservent les méthodes produites. Un consommateur peut générer des conversions de champs publics accessibles d’une bibliothèque ordinaire, sous les règles de construction/accès. Les budgets d’opérations, source générée et travail restent actifs. AOT et autres hôtes exigent leur chemin réellement pris en charge ; une couverture VM ne garantit pas les autres plateformes.

```cpp
Description.Bindings = Registry.GetBindings();
Description.CompileProcessing.Registry = Registry.GetProcessors();
Description.AvailableDependencies = Registry.GetDependencies();
Description.Owners = Registry.GetOwners();
```

```VeroScript
struct Record
{
public:
	@JsonName(value="record_id") @JsonRequired int64 id = 7;
	string name = "default";
	@JsonIgnore int transient = 0;
};

int main()
{
	Record source = Record();
	json::value document = json::converter<Record>::write(ref source);
	Record result = json::converter<Record>::read(document);
	json::schema shape = json::compile_schema(json::converter<Record>::schema());
	assert(result.id == 7 && shape.is_valid(document));
	return 0;
}
```

```cpp
static bool encode(const Target& input, json::value& output, json::mapping_context context);
static bool decode(json::value input, Target& output, json::mapping_context context);
static json::value schema(json::mapping_context context);
```

<!-- docs-navigation -->

---

[JSON](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/script-mapping.md) · [fr-FR](script-mapping.md) · [ja-JP](../../ja-JP/Reference/script-mapping.md) · [ko-KR](../../ko-KR/Reference/script-mapping.md) · [zh-CN](../../zh-CN/Reference/script-mapping.md) · [zh-TW](../../zh-TW/Reference/script-mapping.md)
