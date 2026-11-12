# Valeurs Python, conteneurs et durée de vie

## Fabriques de conteneurs typés et références

Les fabriques publiques sont `Value.array`, `Value.map`, `Value.set`,
`Value.queue`, `Value.priority_queue`, `Value.stack`, `Value.deque` et
`Value.channel`. Elles exigent les noms de types des éléments ou des clés et
valeurs ; `ordered=True` conserve l'ordre de première insertion pour une map.
Les maps et sets ordinaires sont uniques ; les multi-maps et multi-sets exigent
le runtime ou un binding hôte correspondant. `to_python()` produit un snapshot
lorsque le type le permet, et les modifications de ce snapshot ne sont pas
répercutées dans le conteneur du script.

`HostParameter(..., by_ref=True)` décrit une référence mutable livrée sous
forme de `Value` ; `const_ref=True` décrit une référence en lecture seule. Ces
deux indicateurs ne peuvent pas être utilisés ensemble et le write-back ne se
produit pas après un appel échoué ou un `exit`.

## Surface publique des channels

Les opérations exactes sont `send`, `try_send`, `send_for`, `receive`,
`try_receive`, `receive_for` et `close_channel`. `try_send` et `send_for`
renvoient un booléen ; les lectures non bloquantes et temporisées renvoient une
valeur ou `None`. `count`, `channel_capacity` et `channel_closed` décrivent
l'état courant du channel.

`Value` représente une valeur gérée par l’ABI Interop et possède son propre handle natif. Les opérations de session et les objets appelables conservés dépendent aussi de la durée de vie de la session. Les valeurs possédant un objet hôte et leurs protocoles restent utilisables après la fermeture de la session d’origine ; voir les [liaisons hôte](host-bindings.md).

Les entiers, flottants, booléens, chaînes, chaînes larges, énumérations, tableaux, séquences, maps, ensembles et channels disposent d'accès typés. Les conteneurs Python obtenus sont des snapshots et leurs mutations locales ne sont pas répercutées dans le script.

`Session.retain_callable()` renvoie un `RetainedCallable` réutilisable :

```Python
from veroscript import Session

with Session() as session:
	session.load_source("""
int add(int left, int right) { return left + right; }
function(int, int)->int make_callback() { return add; }
void main() {}
""", "callback.vs")
	function_value = session.call("make_callback")
	with session.retain_callable(function_value) as callback:
		result = callback(20, 22)
		assert result == 42
```

Le callable conservé appartient à son thread de création et peut devenir invalide après un rechargement ou la fermeture de la session. Vérifiez `is_valid` et fermez-le rapidement.

L’itération des maps et sets, ainsi que `keys()`, `values()` et `items()`, renvoie des instantanés possédés et conservables. Les scalaires deviennent des valeurs Python ordinaires ; les autres éléments ont leur propre handle `Value` possédé. L’ordre des maps et sets ordinaires est indéfini ; `ordered=True` conserve l’ordre de première insertion. Ces lectures ne permettent pas de modifier les éléments en place ; passez par une fonction VeroScript ou un callback hôte. Les erreurs de type, de thread et de callback sont signalées par `TypeMismatchError`, `ThreadError` et `CallbackError`.

`Value(None)` crée une valeur VeroScript `null`. Affecter `None` à une valeur
dont le type déclaré n'est pas nullable lève `TypeError`. La fabrique
`Value.priority_queue("int", [3, 1, 2])` crée une file de priorité ; les
fabriques ordinaires `map` et `set` ne conservent pas les doublons.

`kind`, `type_name` et `value` exposent la représentation courante ; `clone()` renvoie un autre handle `Value` possédé par l'appelant. Il suit la sémantique de clonage du runtime et ne garantit pas une copie profonde. Chaque `Value` possède son handle natif, libéré lorsque l'objet Python est détruit ; l'API ne fournit pas de méthode `close()` pour `Value`. Les valeurs renvoyées par une opération de session appartiennent à l'appelant. Fermez les sessions et les callables conservés avec leur gestionnaire de contexte.

Une réception bloquante via `receive()` libère le GIL afin que les autres threads Python puissent continuer. Le type des éléments doit respecter le contrat de channels du runtime.

Les valeurs hôtes Python possédées conservent leurs callbacks de clonage, d'égalité et de hachage tant qu'elles restent vivantes, même en présence d'un cycle de références ou après la fermeture de la session d'origine. La finalisation automatique sur le thread créateur préserve ces protocoles. Utilisez un gestionnaire de contexte pour une libération déterministe ; fermez la session sur son thread créateur avant de transférer la dernière valeur à un autre thread.

Les lectures de conteneur comprennent `len`, l’itération, l’indexation, `get` et `contains`. `Session.construct` crée une valeur objet ; `get_field`, `set_field` et `call_method` accèdent à ses membres enregistrés. `weak_this=True` évite de conserver le récepteur d’une méthode ; il faut alors gérer son expiration. `closed` indique la fermeture du wrapper, `is_valid` la validité de la cible. L’exemple ci-dessus obtient `42`.

`count`, `channel_capacity` et `channel_closed` sont des propriétés en lecture seule, sans parenthèses. Cet exemple complet réussit sans produire de sortie.

```Python
from veroscript import Value

channel = Value.channel("int", 1)
assert channel.channel_capacity == 1 and not channel.channel_closed
assert channel.try_send(42)
assert channel.count == 1
assert channel.receive() == 42
channel.close_channel()
assert channel.channel_closed
```

## Nombres exacts

`RuntimeCapabilities.NUMERICS` indique la prise en charge de bigint/rational par
le cœur. Ces nombres restent des `Value` avec `kind == "host_object"`.
`NumericKind(value.numeric_kind)` vérifie `BIGINT`, `RATIONAL` ou `NONE`. Les
scripts et les callbacks hôtes échangent des `Value`, sans conversion implicite
en entiers ou flottants Python.

`session.numerics.bigint(text)`, `rational(text)` et
`parse(text, NumericKind.BIGINT)` analysent un texte explicite ; `to_text(value)`
produit sa forme canonique. `convert(value, type_name, exact_float=False)` renvoie
une `Value` conservant le type entier de largeur fixe, `float` ou `double` demandé.
Un dépassement ou une conversion inexacte avec `exact_float=True` provoque une erreur.

Les mots-clés de `Session.set_numeric_limits` sont `max_value_bits`,
`max_input_bytes`, `max_output_bytes`, `max_scratch_bytes` et `max_work_units`.
Zéro sélectionne les valeurs finies par défaut du cœur. Ces budgets par opération
ne limitent pas la mémoire totale du processus. La session doit être inactive et
utilisée sur son thread créateur. Dans un callback hôte, `current_numerics()`
emprunte le contexte actif ; ses méthodes échouent hors d'un callback actif.
Les opérations numériques natives libèrent le GIL. La sortie Python AOT autonome
ne prend pas en charge bigint/rational.

## Propriété des valeurs liées au module

Voir [SDK d'interopérabilité — Propriété des valeurs liées au module](../../../../../../../VeroScriptInterop/Resources/Docs/User/fr-FR/index.md#value-domain-confinement).

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/values.md) · [fr-FR](values.md) · [ja-JP](../ja-JP/values.md) · [ko-KR](../ko-KR/values.md) · [zh-CN](../zh-CN/values.md) · [zh-TW](../zh-TW/values.md)
