# Attributs et fonctions liées aux méthodes

`attribute.vs` · `callable.vs` · `core.vs`

`std::attribute<T>` conserve une valeur ou un getter synchrone. Chaque copie possède sa configuration ; les captures suivent les règles de propriété du langage. Chaque lecture réévalue le getter.

```VeroScript
import "attribute.vs";

int read_count()
{
	return 42;
}
void main()
{
	std::attribute<int> count = 10;
	count.bind(read_count);
	int snapshot = count;
	assert(snapshot == 42 && count() == 42);
	count.reset();
	assert(!count.try_get().has_value());
}
```

`get()`, `operator()` et la conversion vers `T` lisent une seule fois. `try_get()` retourne un optional ; le secours de `get_or(fallback)` est évalué avant l’appel. Les erreurs se propagent. Seuls un attribut vide, un reader vide ou une cible faible expirée indiquent une absence.

`set(value)`, `bind(fn()->T)`, `bind_optional(fn()->optional<T>)` et `reset()` remplacent la configuration entière. `is_set()` et `is_bound()` ne lancent pas le getter.

`try_get(ref output)` lit une fois dans une variable existante et indique si une valeur était disponible. Un attribut non configuré ou un lecteur optional vide laisse `output` inchangé ; les exceptions du getter se propagent. Utilisez cette surcharge pour écrire dans une variable existante ; `try_get()` reste disponible et renvoie un optional.

`has_same_configuration(ref other)` compare l’identité de configuration sans évaluer les getters. Une copie partage initialement cette identité ; remplacer une configuration les distingue même si les valeurs sont égales. Un résultat lié peut changer sans modification de configuration : cette comparaison ne permet pas d’omettre les getters dynamiques.

`std::make_attribute_by_name<T>(target, "read")` crée un getter sans argument avec référence forte. `T` est explicite ; le type du récepteur est déduit ou indiqué comme second argument de type. Le nom respecte la casse et est résolu à la création. Les règles normales d’accès et d’appel virtuel restent applicables.

`std::make_weak_attribute<T>(weak_target, getter)` verrouille la cible une fois par lecture ; le getter a la signature `fn(U)->T`. `std::make_weak_attribute_by_name<T>(weak_target, "read")` lie une méthode sans argument. Une liaison expirée reste configurée. Enveloppez les structures dans `shared<S>` ou `handle<S>`.

`std::bind_method<F>(target, "method")` produit une fonction typée ordinaire. La méthode doit être publique, non statique et correspondre exactement à la signature. La liaison conserve sa cible et la répartition virtuelle. Passez le résultat à `attribute.bind`, `delegate.bind` ou `multicast_delegate.add`. Conservez la fonction ajoutée pour la retirer. Le module minimal est `callable.vs`.

AOT autonome : C++ prend en charge valeurs, getters et cibles faibles ; C# et Python prennent en charge valeurs, getters nommés et noms de méthodes fortes constants. Les lambdas source Python et les liaisons faibles C#/Python gardent leurs limites existantes. Les noms dynamiques, méthodes hôtes et sélections génériques ambiguës exigent le runtime et sont refusés à l’export. VM, Mixed et JIT partagent le cœur.

Les exports C++ et C# acceptent `try_get()` et `try_get(ref output)`. Python accepte `try_get()` mais refuse la sortie `ref` modifiable. Les trois acceptent la comparaison en lecture seule `has_same_configuration(ref other)` ; cette adaptation spéciale n’ajoute pas de références modifiables générales à Python.

[`attribute_demo.vs`](../../../../Scripts/examples/attribute_demo.vs) · [`attribute_functions.vs`](../../../../Scripts/examples/attribute_functions.vs) · [`attribute_methods.vs`](../../../../Scripts/examples/attribute_methods.vs) · [`attribute_weak.vs`](../../../../Scripts/examples/attribute_weak.vs)

Conservez une identité de module unique pour chaque déclaration de la bibliothèque standard. Si un paquet inclut et réexporte attribute, utilisez ses déclarations ou une dépendance commune. Importer aussi une autre copie source de `core.vs` / `attribute.vs` provoque des exports dupliqués.

Pour plusieurs bibliothèques, créez un unique `shared-stdlib.vslib` depuis `export import "attribute.vs";`. Chaque bibliothèque importe ce composant ; l’application importe les bibliothèques et ce même composant au lieu de la copie source.

<!-- docs-navigation -->

---

[Documentation utilisateur](../index.md) · [VeroScript](../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../../en-US/standard-library/attribute.md) · [fr-FR](attribute.md) · [ja-JP](../../ja-JP/standard-library/attribute.md) · [ko-KR](../../ko-KR/standard-library/attribute.md) · [zh-CN](../../zh-CN/standard-library/attribute.md) · [zh-TW](../../zh-TW/standard-library/attribute.md)
