# Réflexion

VeroScript fournit une API de réflexion pour interroger les modules, types, membres et métadonnées. Des opérations typées permettent aussi d'accéder aux champs d'instance publics et d'appeler les méthodes publiques déclarées dans les classes ou interfaces du script, sans contourner le contrôle d'accès.

## Objets de réflexion

Le langage fournit ces types d’informations publiques :

- `type<T>` décrit le type `T` lui-même.
- `field_info<T>` décrit un champ de `T`.
- `method_info<T>` décrit une méthode de `T`.
- `enum_member_info<T>` décrit un énumérateur de type enum `T`.
- `module_info` décrit la vue actuelle du module.
- `function_info` décrit une fonction libre visible.
- `global_info` décrit un global visible de niveau supérieur.

Ces types sont fournis par le compilateur et ne nécessitent aucune importation. Les requêtes utilisent un nom de propriétaire qualifié et un index de base zéro. Un index non valide est signalé comme une erreur d'exécution.

## Informations sur les types

Les membres publics de `type<T>` sont :

| Membre | Objectif |
| --- | --- |
| `name` | Nom du type court. |
| `qualified_name` | Nom de type qualifié, adapté à la construction de `field_info<T>` et `method_info<T>`. |
| `kind()` | Renvoie la catégorie de type. |
| `is_abstract()` | Indique si le type est abstrait. |
| `base_count()` / `base_name(i)` | Interroge le nombre et les noms des types de base directs. |
| `field_count()` | Renvoie le nombre de champs réfléchissants. |
| `method_count()` | Renvoie le nombre de méthodes réflectables. |
| `metadata_count()`, `metadata_key(i)`, `metadata_value(i)` | Interroge les métadonnées sur la déclaration de type. |
| `has_metadata(key)`, `metadata(key)` | Recherche les métadonnées sans les énumérer manuellement. |
| `has_annotation(name)` | Teste un nom d'annotation ; `name` et `@name` sont acceptés. |
| `enum_member_count()`, `enum_member_name(i)`, `enum_member_value(i)` | Les requêtes énumérent les membres. |
| `enum_underlying_type()` | Renvoie le type d'énumération sous-jacent. |
| `is_scoped_enum()` | Indique si l'énumération est étendue. |

Les requêtes spécifiques à l'énumération n'ont de sens que pour les types d'énumération. Vérifiez `kind()` avant de les utiliser.

## Informations sur le champ

Construisez une requête de champ avec `field_info<T>(owner, index)` :

```VeroScript
type<Record> info = type<Record>();
field_info<Record> field = field_info<Record>(info.qualified_name, 0);

print(field.name());
print(field.declared_type());
print(field.runtime_type());
print(field.is_static());
print(field.is_mutable());
```

Les informations sur le terrain fournissent :

| Membre | Objectif |
| --- | --- |
| `name()` | Nom du champ. |
| `declared_type()` | Type de champ tel que déclaré dans la source. |
| `runtime_type()` | Type enregistré par le runtime. |
| `is_static()` | Indique si le champ est statique. |
| `is_mutable()` | Indique si le champ utilise le modificateur contextuel `mutable`. |
| `is_declared_on_type()` | Indique si le champ est déclaré directement sur le type interrogé. |
| `metadata_count()`, `metadata_key(i)`, `metadata_value(i)` | Interroge les métadonnées sur la déclaration de champ. |
| `has_metadata(key)`, `metadata(key)`, `has_annotation(name)` | Recherche les métadonnées et les annotations de champ par nom. |

## Informations sur la méthode

Construisez une requête de méthode avec `method_info<T>(owner, index)` :

```VeroScript
method_info<Record> method = method_info<Record>(info.qualified_name, 0);

print(method.name());
print(method.return_type());
print(method.parameter_count());
print(method.parameter_type(0));
print(method.parameter_is_ref(0));
print(method.parameter_is_const(0));
```

Les informations sur la méthode fournissent :

| Membre | Objectif |
| --- | --- |
| `name()` | Nom de la méthode. |
| `return_type()` | Type de retour. |
| `parameter_count()` | Nombre de paramètres. |
| `parameter_type(i)` | Type de base du paramètre `i`. |
| `parameter_name(i)` | Nom du paramètre `i`. |
| `parameter_is_ref(i)` | Indique si le paramètre `i` est une référence. |
| `parameter_is_const(i)` | Indique si le paramètre `i` est const ou une référence en lecture seule. |
| `is_static()` | Indique si la méthode est statique. |
| `is_virtual()` | Indique si la méthode est virtuelle. |
| `is_abstract()` | Indique si la méthode est abstraite. |
| `is_const()` | Indique si la méthode membre est const. |
| `is_deleted()` | Indique si la méthode est supprimée avec `= delete`. |
| `is_constructor()` / `is_destructor()` | Indique si la méthode est un constructeur ou un destructeur. |
| `metadata_count()`, `metadata_key(i)`, `metadata_value(i)` | Interroge les métadonnées sur la déclaration de méthode. |
| `has_metadata(key)`, `metadata(key)`, `has_annotation(name)` | Recherche les métadonnées et les annotations de méthode par nom. |

`parameter_type(i)` renvoie le type sous-jacent après la suppression de `ref` ou `const ref`. Pour reconstruire la sémantique complète des paramètres, inspectez également `parameter_is_ref(i)` et `parameter_is_const(i)`.

## Réflexion exécutable

`set<V>` ne peut pas modifier un champ déclaré `const`, même si l'objet receveur est modifiable. Si le receveur est `const`, seul un champ `mutable` peut être modifié. `get<V>` peut toujours lire ces champs.

Après avoir choisi un descripteur, utilisez `field_info<T>.get<V>(objet)`, `set<V>(objet, valeur)`, `method_info<T>.invoke<R>(objet, arguments...)` ou `invoke_static<R>(arguments...)`. `V` et `R` sont explicites et doivent correspondre exactement au type du membre ; `R` peut être `void`. Les arguments sont évalués une seule fois dans l'ordre, puis vérifiés contre la signature fixe. Pour les surcharges, examinez `parameter_count()` et `parameter_type(i)`.

Les champs d'instance publics du script et les méthodes publiques déclarées sur des classes ou interfaces du script sont exécutables. Une méthode abstraite d'interface est résolue vers l'implémentation du récepteur ; les méthodes d'interface publiques avec corps et les méthodes statiques publiques sont également prises en charge. L'appel virtuel conserve sa répartition dynamique et l'écriture d'un champ de structure modifie l'objet d'origine. Un descripteur invalide, forgé, périmé, étranger ou inaccessible échoue à l'exécution. Les membres privés ou protégés, les membres hôtes, les champs statiques, les constructeurs et destructeurs, les méthodes supprimées restent consultables mais non exécutables ici. Les AOT C++, Python et C# rejettent actuellement ces opérations à la compilation. Les paquets de bytecode des anciennes versions de développement ne sont pas pris en charge. Reconstruisez-les depuis les sources avec le compilateur actuel . Voir l'[exemple complet](../../../../Scripts/examples/reflection_executable.vs).

`invoke` et `invoke_static` acceptent les paramètres et retours par référence. Passez explicitement `&value` ou `ref value` ; les types doivent correspondre exactement, et une référence modifiable ne peut pas cibler un stockage en lecture seule. Utilisez `invoke<int&>(receiver, ...)` ou `invoke<const int&>(receiver, ...)`, avec les mêmes types pour `invoke_static`. Les retours préservent l'alias et la qualification const ; une lecture par valeur déréférence le résultat. Le stockage utilise les cellules de référence gérées des appels ordinaires, sans copie simulant une réécriture. Tous les arguments sont vérifiés avant l'entrée dans la méthode ; les modifications précédant une exception suivent les règles des appels ordinaires.

Pour transmettre un retour par référence comme autre argument par référence, liez-le d’abord à une référence locale, puis passez `&alias` ou `ref alias`.

Les arguments objets sont également vérifiés selon leur type statique au site d’appel : les conversions ascendantes valides et les handles nuls sont acceptés, tandis que les conversions descendantes implicites sont rejetées avant le corps de la méthode. Utilisez explicitement `cast<T>` avant de transmettre une valeur convertie vers un type dérivé. `set<V>` vérifie à la compilation si le type source peut être affecté à `V`. Les expressions de type `void` ne peuvent pas servir d’arguments.

`set<V>` applique à la valeur la conversion contrôlée d’une affectation ordinaire : une valeur `int` peut ainsi être écrite dans un champ `int64` avec `set<int64>`. Un dépassement numérique est rejeté avant la modification du champ ; `V` doit toujours correspondre exactement au type déclaré du champ.

Les structures et tableaux de taille fixe passés par valeur sont copiés dès leur évaluation, avant toute modification de la source par les arguments suivants. Les retours par référence sont aussi copiés pour initialiser une variable par valeur, fournir un argument par valeur ou produire un retour par valeur. Les handles de classes et interfaces gardent leur sémantique habituelle. Reconstruisez les anciens paquets de bytecode depuis les sources pour bénéficier de ces corrections du compilateur.

## Informations sur les modules, les fonctions libres, globales et les membres enum

`module_info()` expose les déclarations visibles depuis le module d'entrée. Utilisez `function_count()`, `global_count()` et `type_count()` comme limites, puis construisez `function_info(i)` ou `global_info(i)`. `type_name(i)` renvoie chaque nom de type de script visible. Les déclarations privées importées ne sont pas exposées ; les déclarations privées du module d'entrée et les déclarations importées exportées restent visibles selon les règles normales d'import/export.

`function_info` indique le nom, le nom de liaison stable, le chemin du module, la signature de retour et de paramètre, les indicateurs `ref`/`const`, les indicateurs supprimés/exportés et les métadonnées de déclaration. `global_info` indique le nom, le nom de liaison, le chemin du module, les types déclarés/d'exécution, les indicateurs const/exportés et les métadonnées de déclaration. Les données membres statiques restent des champs et ne sont pas dupliquées dans `global_info`.

Pour une énumération `E`, construisez `enum_member_info<E>(type<E>().qualified_name, i)` pour inspecter le nom d'un énumérateur, la valeur `int64` complète et les métadonnées de déclaration. Cela complète les requêtes d'énumération compactes déjà disponibles sur `type<E>`.

## Métadonnées de déclaration

Les déclarations peuvent porter des annotations. Le langage stocke les noms, les clés et les valeurs des annotations, mais ne leur attribue pas de signification spécifique à l'application :

```VeroScript
@entity(sample)
struct BaseRecord
{
	int id;
};

struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}
};
```

Un hôte, un éditeur ou un autre outil peut énumérer ces informations via `metadata_count()`, `metadata_key(i)` et `metadata_value(i)`, ou utiliser `has_metadata(key)`, `metadata(key)` et `has_annotation(name)` pour une recherche directe. Une clé manquante renvoie une chaîne vide de `metadata(key)` ; utilisez `has_metadata(key)` lorsqu'il faut distinguer une valeur vide et une clé manquante. Dans cet exemple, `@entity`, `@data` et `@call` sont des noms de métadonnées interrogeables, et non des comportements métier intégrés ; un script ou une intégration hôte peut définir ses propres conventions.

Les annotations dans un catalogue de liaisons d'hôte et la réflexion du script d'exécution sont deux couches distinctes. Un catalogue de liaison décrit les API hôtes, tandis que les types d'informations de réflexion interrogent le module de script compilé actuel.

## Exemple complet

L'exemple suivant interroge l'héritage, les champs, les méthodes, les méthodes statiques, les globales de niveau supérieur, les membres d'énumération et les métadonnées de déclaration :

```VeroScript
@setting(primary)
export const int ReflectionVersion = 1;

@utility(sample)
export int echo_value(const int& value)
{
	return value;
}

enum class RecordState
{
	@display(primary)
	Ready,
	Archived
};

struct BaseRecord
{
	int id;

	int read_id() const
	{
		return id;
	}
};

@entity(sample)
struct Record : BaseRecord
{
	@data(display)
	mutable string label;

	@call(sample)
	virtual int add(const int& value) const
	{
		return id + value;
	}

	static int identity(int value)
	{
		return value;
	}
};

int main()
{
	type<Record> info = type<Record>();
	print("{} ({})", info.name, info.kind());
	print("bases={}, fields={}, methods={}",
		info.base_count(), info.field_count(), info.method_count());

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}",
			field.name(), field.declared_type(), field.runtime_type(), field.is_static());
	}

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, params={}, static={}, virtual={}",
			method.name(), method.return_type(), method.parameter_count(),
			method.is_static(), method.is_virtual());
	}

	module_info module = module_info();
	print("module={}, functions={}, globals={}, types={}",
		module.path(), module.function_count(), module.global_count(), module.type_count());

	for (int i = 0; i < module.function_count(); i++)
	{
		function_info function_meta = function_info(i);
		print("function {}: returns={}, exported={}",
			function_meta.name(), function_meta.return_type(), function_meta.is_exported());
	}

	for (int i = 0; i < module.global_count(); i++)
	{
		global_info global_meta = global_info(i);
		print("global {}: declared={}, runtime={}, const={}",
			global_meta.name(), global_meta.declared_type(),
			global_meta.runtime_type(), global_meta.is_const());
	}

	type<RecordState> state_type = type<RecordState>();
	enum_member_info<RecordState> state =
		enum_member_info<RecordState>(state_type.qualified_name, 0);
	print("enum member {}={} annotated={}",
		state.name(), state.value(), state.has_annotation("display"));
	return 0;
}
```

Consultez l’entrée de réflexion dans [Index des exemples](../examples.md) pour une version exécutable. L'API de réflexion est indépendante du type de retour du point d'entrée ; l'exemple utilise `int main` et renvoie un code d'état de processus explicite.

## Limites et erreurs

- La réflexion permet l'inspection et les opérations exécutables typées décrites ci-dessus, mais pas l'accès dynamique sans restriction à tous les membres.
- `field_count()`, `method_count()` et le nombre de membres enum décrivent les informations de type d'exécution actuel ; vérifiez les limites avant l’indexation.
- Les informations de type manquantes, les index non valides et les requêtes inapplicables sont des erreurs d'exécution. Ne traitez pas un index invalide comme un résultat vide.

Pour les règles de type sous-jacentes, consultez [Types de données](data-types.md), [Structs](structs.md), [Classes](classes.md) et [Enums](enums.md). Les descriptions de l'interface côté hôte sont traitées dans [Catalogues de liaison](../../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/bindings.md).

## Réflexion structurée

<a id="reflection-metadata-contract"></a>

`type<T>`, field_info, method_info, module_info, function_info, global_info et enum_member_info exposent annotation_count/annotation/annotations, comment_count/comment/comments, documentation, comments_available, comments_retention, declaration et source_metadata. Vérifiez les bornes avant tout accès indexé. annotation_info et annotation_argument_info exposent argument_count/argument/arguments ; l’accesseur typé doit correspondre au type scalaire. definition() fournit la description enregistrée ; vérifiez is_valid() si elle peut manquer.

L’hôte choisit Full, Documentation ou NoComments ; Unavailable signifie que les informations source sont absentes. NoComments ne supprime ni annotations ni documentation enregistrée séparément. Un objet de métadonnées garde un instantané immuable malgré le rechargement. Ces requêtes ne contournent pas les contrôles d’accès et ne rendent pas un membre privé appelable.

[Annotations et métadonnées source](annotations.md)

## Réflexion des types imbriqués

Les méthodes `display_name()`, `is_nested()`, `is_local()`, `declaring_type_name()`, `declaring_function_name()` et `access()` décrivent l’appartenance. `access()` renvoie `none` hors d’un type. `qualified_name` conserve l’identité concrète ; un nom affiché ne sert pas de clé.

`nested_type_count()`, `nested_type_name(i)`, `nested_type_parameter_count(i)` et `nested_type_declaration(i)` énumèrent les déclarations directement possédées et accessibles, y compris les modèles non matérialisés. Le nombre de paramètres exclut ceux du type externe. Les alias ne créent pas de type d’exécution supplémentaire. Le contexte d’accès est fixé lors de la création de la requête ; les noms locaux/privés ne deviennent pas globaux.

## Métadonnées des modèles

`module_info.template_declaration_count()` énumère les déclarations ouvertes sans instanciation. `template_declaration_id/name/kind/owner` identifie chaque entrée par indice. `template_parameter_count`, `template_own_parameter_count` et `template_enclosing_parameter_count` distinguent les environnements. `template_parameter_id/name/kind/value_type/default/is_enclosing` prend `(indice_déclaration, indice_paramètre)` ; la liste combinée place les paramètres englobants en premier.

`type<T>`, `method_info<T>` et `function_info` exposent `template_declaration_id()`, `template_argument_count()` et les requêtes indexées `template_argument_encoding/name/kind/value_type`. Utilisez `template_enclosing_argument_count/encoding/name` pour l’environnement englobant. Une déclaration ordinaire renvoie une identité vide et zéro argument. Le codage conserve largeur, signe et identité d’énumération ; les noms affichés ne sont pas des clés d’identité. Ces requêtes ne créent aucun corps exécutable et les comptes de paramètres de type gardent leur sens.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/language-basics/reflection.md) · [fr-FR](reflection.md) · [ja-JP](../../ja-JP/language-basics/reflection.md) · [ko-KR](../../ko-KR/language-basics/reflection.md) · [zh-CN](../../zh-CN/language-basics/reflection.md) · [zh-TW](../../zh-TW/language-basics/reflection.md)
