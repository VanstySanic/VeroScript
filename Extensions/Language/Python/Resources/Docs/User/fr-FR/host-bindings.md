# Liaisons hôte Python

Les instances `Value` possédant un objet hôte et leurs vérifications de sous-type restent utilisables après la fermeture de la `Session` d’origine. La recherche de types utilise des références faibles aux classes Python. Les valeurs munies de protocoles de clonage, d’égalité ou de hachage conservent les rappels nécessaires, notamment pour les clés map/set, jusqu’à la libération de la dernière valeur associée. Les valeurs sans ces protocoles ne conservent ni les classes sans rapport ni les rappels de la session.

`HostClass` expose les méthodes `constructor(...)`, `method(...)`,
`static_method(...)`, `property(...)` et `field(...)` pour décrire les membres
visibles dans VeroScript.

Les bindings exposent explicitement des fonctions, classes et sérialiseurs Python avec un contrat de types VeroScript. `HostParameter` décrit `type_name`, le nom, `by_ref`, `const_ref` et la documentation.

```Python
from veroscript import HostParameter

session.register_function(
	"Add",
	add,
	parameters=(
		HostParameter("int", "left"),
		HostParameter("int", "right"),
	),
	return_type="int",
)
```

`register_function` et `host_function` doivent décrire la signature réellement exécutée. Une référence mutable doit être demandée explicitement ; `const_ref` doit rester en lecture seule.

`HostClass` déclare constructeur, méthodes d'instance ou statiques et propriétés. Seuls les membres enregistrés sont accessibles par `construct`, `call_method`, `get_field` et `set_field`.

Les signatures des membres doivent être explicites. Elles peuvent utiliser des
scalaires, des objets hôte enregistrés, `enum<Name>` et des conteneurs imbriqués
comme `array<int>` ou `map<string,PythonObject>` ; les valeurs non scalaires
sont représentées par `Value`. Les membres de classe acceptent actuellement
des paramètres par valeur ; les fonctions libres prennent aussi des références
mutables (`by_ref=True`) et des références en lecture seule (`const_ref=True`).

`field(...)` expose un attribut avec getter vivant et setter modifiable par
défaut ; utilisez `writable=False` pour le rendre en lecture seule.
`register_struct(...)` décrit une valeur hôte : `protocol(...)` doit fournir un
clone lorsque VeroScript copie cette valeur, et l'égalité ainsi que le hachage
sont nécessaires pour une clé de map ou de set. `register_enum(...)` attend un
`IntEnum` ou `IntFlag` ; un `enum.Enum` ordinaire est refusé. Les modèles génériques créés par `register_generic_class(...)` ou
`register_generic_struct(...)` doivent être instanciés avec `instantiate(...)`
avant le chargement du script qui les utilise. Pour une classe hôte héritable,
utilisez `inheritable=True` dans `register_class(...)` et fournissez un
`subclass_factory` via `protocol(...)` si les sous-classes du script doivent
créer des instances gérées.

`register_serializer`, `serialize_object` et `deserialize_object` forment une frontière de données qui doit valider le type et la version. `set_mutation_callback` reçoit des événements `MutationKind` ; le callback doit rester court, ne pas modifier l'objet en cours d'énumération et ne pas détruire la session. Une notification ne peut pas annuler une mutation déjà terminée. Les exceptions sont converties en `CallbackError`.

## Métadonnées et interception

Les noms d'annotations sont sensibles à la casse par défaut. Avec
`register_annotation("@GuiWidget", case_sensitive=False)`, les abonnements des
processeurs et la réflexion ignorent la casse ASCII du nom, tout en conservant
son orthographe source. Les noms d'arguments et les clés de métadonnées restent
exacts. Une politique ambiguë lève `Error` sans modifier l'enregistrement
existant. Cet enregistrement n'installe pas de processeur de compilation.

`register_annotation(...)` déclare un vocabulaire d'annotations et ses
arguments autorisés. `register_type_alias(...)` déclare un alias concret ou
générique. `set_instance_call_interceptor(...)` reçoit un
`InterceptContext` résolu : retournez `InterceptResult.UNHANDLED` pour l'implémentation normale,
appelez `context.proceed()` au plus une fois puis retournez `InterceptResult.HANDLED`, ou
retournez `(InterceptResult.HANDLED, remplacement)`. `InterceptResult.FAILED` fait échouer l'appel. Le contexte
expire au retour du callback ; `call_method_bypassing_interceptor(...)` permet
aux appels hôte de confiance de contourner l'intercepteur.

Lorsqu'une signature utilise un type hôte de base enregistré, une instance d'un
sous-type enregistré compatible peut être transmise. La compatibilité repose
sur la relation enregistrée ; elle n'est pas déduite d'une classe Python
arbitraire.

## Limites des callbacks

La session conserve les callbacks enregistrés jusqu'à sa fermeture. Les exceptions deviennent `CallbackError` et ne traversent pas l'ABI native. Un callback de script peut s'exécuter sur un worker VM : protégez l'état Python partagé. Un callback sur worker ne doit pas appeler les méthodes `Session` ou callable conservé confinées au thread créateur. Les types génériques doivent être instanciés avant le chargement de la source ; les structs hôtes doivent fournir les protocoles requis pour leur copie et leur usage comme clé.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/host-bindings.md) · [fr-FR](host-bindings.md) · [ja-JP](../ja-JP/host-bindings.md) · [ko-KR](../ko-KR/host-bindings.md) · [zh-CN](../zh-CN/host-bindings.md) · [zh-TW](../zh-TW/host-bindings.md)
