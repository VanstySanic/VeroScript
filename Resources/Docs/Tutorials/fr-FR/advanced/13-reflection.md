# 13 : Réflexion d'exécution

Ce didacticiel crée un petit inspecteur de type pour expliquer l'API de réflexion de VeroScript. La réflexion expose les métadonnées du système de types de script actuel ainsi que des opérations typées sur les champs et méthodes publics du script. Elle est utile pour les outils, les éditeurs et les adaptateurs de sérialisation ; elle ne transforme pas les membres arbitraires en interface d'appel dynamique par chaîne et ne contourne pas le contrôle d'accès.

## Étape 1 : ajouter des métadonnées aux déclarations

Les annotations peuvent être attachées aux types, champs, méthodes, fonctions libres, globales et membres enum :

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

	int score;

	int add_score(int amount)
	{
		score += amount;
		return score;
	}
};
```

Le langage stocke les noms d'annotations, les clés et les valeurs mais n'interprète pas leur signification spécifique à l'application. Ici, `entity`, `data` et `call` sont des conventions de métadonnées définies par l'utilisateur, et non des mots-clés qui génèrent automatiquement un comportement.

Les étapes suivantes se poursuivent dans le même fichier. Conservez cette déclaration `Record` et remplacez ou étendez `main` au fur et à mesure de votre progression.

## Étape 2 : obtenir les informations de type

`type<Record>` décrit `Record` lui-même :

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	print("name={}, qualified={}", info.name, info.qualified_name);
	print("kind={}, abstract={}", info.kind(), info.is_abstract());
	print("fields={}, methods={}", info.field_count(), info.method_count());
	return 0;
}
```

`name` est le nom court et `qualified_name` est le nom qualifié. Les objets d'informations de champ et de méthode sont construits avec ce nom qualifié et un index de base zéro. Il est donc courant de conserver en premier un objet `type<T>`.

## Étape 3 : énumérer les champs

Lisez chaque champ avec `field_info<Record>(info.qualified_name, i)` :

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> field = field_info<Record>(info.qualified_name, i);
		print("field {}: declared={}, runtime={}, static={}, mutable={}",
			field.name(), field.declared_type(), field.runtime_type(),
			field.is_static(), field.is_mutable());

		for (int j = 0; j < field.metadata_count(); j++)
		{
			print("  metadata {}={}", field.metadata_key(j), field.metadata_value(j));
		}
	}

	return 0;
}
```

Les informations sur le champ indiquent également si un champ est déclaré directement sur le type actuel et s'il utilise le modificateur contextuel `mutable`. `declared_type()` est utile pour afficher la déclaration source, tandis que `runtime_type()` affiche le type enregistré par le runtime.

## Étape 4 : énumérer les méthodes et les paramètres

`method_info<T>` fournit des modificateurs de méthode et des informations sur les paramètres :

```VeroScript
int main()
{
	type<Record> info = type<Record>();

	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> method = method_info<Record>(info.qualified_name, i);
		print("method {}: returns={}, static={}, virtual={}, const={}",
			method.name(), method.return_type(), method.is_static(),
			method.is_virtual(), method.is_const());

		for (int j = 0; j < method.parameter_count(); j++)
		{
			print("  param {}: name={}, type={}, ref={}, const={}",
				j, method.parameter_name(j), method.parameter_type(j),
				method.parameter_is_ref(j), method.parameter_is_const(j));
		}
	}

	return 0;
}
```

`parameter_type(j)` renvoie le type sous-jacent après la suppression de `ref` ou `const ref`. Vérifiez `parameter_is_ref(j)` et `parameter_is_const(j)` séparément lorsqu'un outil doit reconstruire la sémantique complète des paramètres.

## Étape 5 : inspecter la surface du module

`module_info` énumère les fonctions libres, les globaux de niveau supérieur et les types de script visibles depuis le module d'entrée. Construisez `function_info(i)` et `global_info(i)` avec des indices délimités par ses comptes :

```VeroScript
module_info module = module_info();
print("module={}, globals={}, types={}",
	module.path(), module.global_count(), module.type_count());

for (int i = 0; i < module.function_count(); i++)
{
	function_info function_meta = function_info(i);
	print("{} -> {}", function_meta.name(), function_meta.return_type());
}

for (int i = 0; i < module.global_count(); i++)
{
	global_info global_meta = global_info(i);
	print("global {}: declared={}, const={}",
		global_meta.name(), global_meta.declared_type(), global_meta.is_const());
}
```

Tous les descripteurs de déclaration fournissent `has_metadata(key)`, `metadata(key)` et `has_annotation(name)`. Pour les annotations de membre enum, construisez `enum_member_info<E>(type<E>().qualified_name, i)`.

Par exemple, lisez le premier membre de `RecordState` :

```VeroScript
type<RecordState> state_type = type<RecordState>();
enum_member_info<RecordState> state =
	enum_member_info<RecordState>(state_type.qualified_name, 0);

print("{}={}, annotated={}",
	state.name(), state.value(), state.has_annotation("display"));
```

## Étape 6 : gérer les décomptes et les erreurs

Les requêtes de réflexion utilisent des décomptes et des indices plutôt que de renvoyer un objet dynamique éventuellement vide. Vérifiez le nombre de champs, de méthodes, de fonctions, de membres globaux, de types ou d'énumérations avant l'indexation. Les index hors plage, les informations de type manquantes et les requêtes inapplicables sont des erreurs d'exécution.

La réflexion exécutable typée se limite aux champs d'instance publics et aux méthodes publiques d'instance ou statiques du script. Elle ne contourne pas le contrôle d'accès et ne fournit pas de recherche ni d'invocation sans restriction par chaîne. Les noms au format `__reflection_*` sont des symboles d'assistance internes, et non une API de script, et ne doivent pas être utilisés directement.

## Étape 7 : lire, écrire et appeler des membres sélectionnés

Sélectionnez les descripteurs à partir de leurs métadonnées, puis indiquez le type exact du champ ou du résultat. Les arguments de méthode sont positionnels :

```VeroScript
int main()
{
	type<Record> info = type<Record>();
	int score_index = -1;
	int add_index = -1;
	int identity_index = -1;
	for (int i = 0; i < info.field_count(); i++)
	{
		field_info<Record> candidate = field_info<Record>(info.qualified_name, i);
		if (candidate.name() == "score")
		{
			score_index = i;
		}
	}
	for (int i = 0; i < info.method_count(); i++)
	{
		method_info<Record> candidate = method_info<Record>(info.qualified_name, i);
		if (candidate.name() == "add_score")
		{
			add_index = i;
		}
		if (candidate.name() == "identity")
		{
			identity_index = i;
		}
	}
	if (score_index < 0 || add_index < 0 || identity_index < 0) return 1;

	field_info<Record> score = field_info<Record>(info.qualified_name, score_index);
	method_info<Record> add = method_info<Record>(info.qualified_name, add_index);
	method_info<Record> identity = method_info<Record>(info.qualified_name, identity_index);
	Record record = Record();
	score.set<int>(record, 10);
	print("score={}", score.get<int>(record));
	print("add_score={}", add.invoke<int>(record, 5));
	print("identity={}", identity.invoke_static<int>(21));
	return 0;
}
```

`get<V>` et `set<V>` accèdent à un champ d'instance ; `invoke<R>` appelle une méthode d'instance et `invoke_static<R>` une méthode statique. Le type de valeur ou de résultat du membre sélectionné doit correspondre exactement à `V` ou `R`. La référence complète décrit les signatures non prises en charge et les limites propres à chaque backend.

## Exercices

1. Ajoutez une méthode statique à `Record` et localisez-la avec `is_static()`.
2. Affichez les bases directes de `Record` avec `base_count()` et `base_name(i)`.
3. Affichez les membres de `RecordState` avec `enum_member_count()`, `enum_member_name(i)` et `enum_member_value(i)`.
4. Écrivez un outil qui imprime uniquement les champs contenant les métadonnées `@data`.

Voir [Réflexion](../../../../../VeroScript/Resources/Docs/User/fr-FR/language-basics/reflection.md) pour les règles complètes. L'exemple d'inspection est [`examples/reflection.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection.vs) et les opérations exécutables sont illustrées dans [`examples/reflection_executable.vs`](../../../../../VeroScript/Resources/Scripts/examples/reflection_executable.vs). Les descriptions de l'interface hôte et la réflexion du script sont des couches distinctes ; les catalogues hôtes sont traités dans [Catalogues de liaison](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/bindings.md).

Ceci termine le chemin principal du didacticiel. Consultez la [référence de la chaîne d'outils](../../../User/fr-FR/toolchain/index.md) ou la [référence de la bibliothèque standard](../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/index.md) lorsque vous en avez besoin.

<!-- docs-navigation -->

---

[Tutoriels](../index.md)

Langue: [en-US](../../en-US/advanced/13-reflection.md) · [fr-FR](13-reflection.md) · [ja-JP](../../ja-JP/advanced/13-reflection.md) · [ko-KR](../../ko-KR/advanced/13-reflection.md) · [zh-CN](../../zh-CN/advanced/13-reflection.md) · [zh-TW](../../zh-TW/advanced/13-reflection.md)
