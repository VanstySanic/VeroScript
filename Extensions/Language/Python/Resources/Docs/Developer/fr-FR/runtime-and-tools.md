# Runtime et outils statiques Python

## Statistique de boucle typée

`native_typed_loop_executions` compte les invocations de noyaux natifs de boucles typées, y compris une invocation qui se termine après avoir signalé une erreur runtime visible dans le source. C’est un compteur d’observabilité d’exécution, pas un compteur de succès.

## Adaptateur runtime

`VeroScriptPython` expose `Session`, `Value`, `Package`, callables retenus, `HostParameter`, fonctions et classes hôte sur `VeroScriptInterop`. Les sessions/callables documentés sont confinés au thread créateur et fermés explicitement. Le travail natif libère le GIL lorsqu’autorisé et les callbacks le reprennent ; ils peuvent entrer en concurrence au fil du temps, donc leur état est synchronisé par le propriétaire.

La surface comprend `ExternalReference` et son type, `GenericHostClass`, `available_languages`, `get_language`, `set_language`, `merge_catalog_directory`. Les langues proviennent du catalogue déployé. La fusion valide la baseline et distingue diagnostics runtime du texte UI hôte ; wrappers ne traduisent ni API, ni clés Catalog, ni valeurs de type référence. Les containers `Value` ont des types d’élément explicites et l’énumération retourne un snapshot possédé. Le type signé `char`/`int8` est représenté par un `int` Python dans `-128..127` ; les clés de map et éléments de set caractère conservent cette représentation dans les snapshots, recherches et `to_python()`, et les entrées hors plage sont rejetées plutôt que tronquées. Une exception de callback mutation est signalée après mutation sans rollback.

## Frontend statique

`VeroScriptPythonFrontend` parse les déclarations Python typées sans importer ni exécuter les modules source, et conserve plages, fonctions, classes, enums, champs, paramètres, decorators, documentation, imports et annotations normalisées. Exécution top-level ou instruction imbriquée non prise en charge échoue avec diagnostic structuré.

`VeroScriptPythonBindGen` consomme cet AST et émet métadonnées Binding Catalog, référence API par input et manifeste projet. `VeroScriptPythonTranspiler` lower le sous-ensemble typé pris en charge vers VeroScript, résout imports relatifs du projet, émet source maps et valide l’arbre généré complet. BindGen et Transpiler rejettent aussi les noms générés qui ne sont pas des identifiants ASCII VeroScript ou qui sont des mots-clés du langage tels que `do`, même lorsque la validation du compilateur est désactivée. Le Transpiler applique également la limite de 32 niveaux d’imbrication d’instructions du core et émet Python `pass` sous forme d’un bloc VeroScript vide valide. Les classes et fonctions de niveau module dont le nom commence par `_` ne sont pas exportées et ne peuvent pas être référencées par un contrat public généré. Les constantes numériques émises doivent être finies et tenir dans la plage entière signée 32 bits sans suffixe du core. Les littéraux Python list, set et dictionary ne sont acceptés que comme initialiseurs de déclarations typées, conformément à la grammaire brace-initializer du core. Les annotations Optional et les comparaisons d’identité Python sont rejetées tant que le sous-ensemble typé ne définit pas de contrat de type nullable. `break` et `continue` exigent une boucle `while` émise englobante.

Les expressions entières `IntEnum` et `IntFlag` prises en charge sont évaluées de manière sûre en littéraux signés 64 bits exigés par la grammaire enum du core.

`self` n’est réécrit en `this` que lorsqu’il désigne le récepteur d’une méthode d’instance ; un identifiant ordinaire nommé `self` reste inchangé. `None` n’est émis que comme `return;` sans valeur dans une fonction déclarée `-> None` ou un constructeur.

Une fonction non `void` doit retourner une valeur sur chaque chemin pris en charge statiquement. Les variables locales annotées doivent être déclarées directement dans le corps de la fonction ; leur déclaration dans un bloc `if` ou `while` est rejetée afin d’éviter une divergence de portée entre Python et VeroScript.

Dans un projet multifichier, les collisions entre une variable de module exportée et une autre variable de module ou un type sont rejetées avant l’émission, conformément à l’espace de symboles du core.

## Contrat d’exécution de session

La surface `Session` reflète le cycle de vie natif : `load_source`, `load_file`, `load_package`, `reload_package`, `build_package_file`, `run`, `reload`, `diagnostics`, `set_input`. `set_hot_reload_enabled` contrôle reload conservant l’état ; `request_cancellation` demande une annulation coopérative depuis un autre thread sans interrompre un callback actif. `set_jit_policy` et `jit_stats` exposent politique adaptative et statistiques de dernier run. `RuntimeCapabilities` décode les services Core du masque `CAPABILITIES`. `session.get_jit_backend_info()` décrit le module sélectionné : `native_code` indique l’émission native et `capabilities` est un masque `JitBackendCapabilities` avec `NATIVE_CODE`, `PORTABLE_EXECUTION`, `NATIVE_TYPED_LOOPS` et `DIRECT_SCALAR_CALLS`, indépendamment de `HOST_PLATFORM`. `cost_model_vm_fallbacks` et `native_typed_loop_executions` comptent les décisions et exécutions correspondantes du dernier run.

L’adaptateur Python transmet `reuse_session_code`, `enable_controlled_mixed`, limites et statistiques à la Session native. L’éligibilité et la comptabilité par génération sont maintenues dans [C++ Session](../../../../../Cpp/Resources/Docs/Developer/fr-FR/session.md#session-jit-cache) ; API Python, valeurs par défaut et sens des quotas sont définis dans [Python Session](../../User/fr-FR/session.md#session-jit-cache). N’ajoutez pas d’exigence de comptes non nuls et ne désactivez pas la réutilisation gouvernée dans l’adaptateur.

## Valeurs, appels et channels

`Value.channel` crée un channel borné ou non. `send`, `try_send`, `send_for` sont les envois bloquant/non bloquant/temporisé, et `receive`, `try_receive`, `receive_for` les réceptions correspondantes ; `close_channel` ferme l’extrémité. Channel est le partage explicite inter-worker, les globales de script restant locales au worker.

La surface d’appel inclut `has_function`, `has_method`, `call`, `call_method`, `call_method_bypassing_interceptor`, `construct_object`, `retain_callable`, `get_field`, `set_field`, `get_global`, `set_global`, `apply_class_defaults`. Bypasser l’intercepteur d’entrée ne désactive pas ceux des appels imbriqués du corps.

## Bindings hôte, réflexion et persistance

Les entrées d’enregistrement comprennent `register_function`, `register_annotation`, `register_type_alias`, `register_generic_host_type`, `instantiate_generic_host_type`, `register_host_class`, `register_host_enum`, `register_host_protocol`, `register_host_member`. Les exports reflection/binding utilisent `export_reflection_json` et `export_bindings_json`.

`register_serializer`, `serialize_object`, `deserialize_object`, `enumerate_object_references` suivent les contrats Interop de sérialisation externe et référence empruntée. `set_mutation_callback` rapporte les mutations champ/conteneur ; `set_instance_call_interceptor` reçoit la cible résolue et peut poursuivre ou traiter l’appel. API, clés JSON, noms de type et diagnostics restent exacts.

## Frontière de threading Python

Les opérations channel bloquantes, temporisées et non bloquantes libèrent le GIL pendant l’opération native. Cela évite de bloquer des threads Python non liés, mais ne rend pas `Session` ou `Value` généralement thread-safe ; les règles natives de thread propriétaire restent applicables. Seul `request_cancellation` est explicitement sûr depuis un autre thread Python, et l’appelant conserve la Session jusqu’au retour natif. Les exceptions callback natives sont enregistrées puis reconverties après la frontière d’appel ; `direct_native_calls` est un compteur JIT séparé.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/runtime-and-tools.md) · [fr-FR](runtime-and-tools.md) · [ja-JP](../ja-JP/runtime-and-tools.md) · [ko-KR](../ko-KR/runtime-and-tools.md) · [zh-CN](../zh-CN/runtime-and-tools.md) · [zh-TW](../zh-TW/runtime-and-tools.md)
