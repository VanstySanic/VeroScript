# Valeurs, callbacks et packages Interop

## Valeurs et conteneurs

L’ABI expose scalar fixed-width, string, enum, host object, array, map, ordered map, set, queue, stack, deque et channel. Les callbacks d’énumération de conteneur reçoivent des valeurs empruntées. Les types doivent respecter le contrat VeroScript : la validation du type des valeurs retourne `TYPE_MISMATCH`, les échecs d’exécution peuvent retourner `ENGINE_ERROR`. Aucune conversion implicite n’est effectuée. Le comptage des multisets additionne les longueurs des tableaux d’occurrences sans copier les éléments ni allouer de snapshot.

Les emplacements objet acceptent les objets de script dérivés assignables à la classe de base ou à l’interface déclarée, selon les métadonnées du module de la valeur source. Ainsi, un `Child` dérivé de `Base` et implémentant `IRead` peut entrer dans `array<Base>` ou `queue<IRead>`. Les déclarations de conteneurs restent invariantes : aucune conversion de `array<Child>` en `array<Base>`, ni conversion numérique, n’est ajoutée.

Les déclarations de clés map/set sont validées même pour les conteneurs vides, y compris multimap/multiset. Les types intégrés non pris en charge (`bool`, `float`, `double`, entiers non signés de 8 bits et entiers de 16 bits, conteneurs, fonctions et références) retournent `INVALID_ARGUMENT` sans remplacer la valeur précédente. Les clés primitives sont `char`, `int`, `unsigned int`, `int64`, `uint64`, `string` et `wstring`, avec leurs alias. Les identités enum/host nommées, y compris les types host instanciés, sont résolues par la session consommatrice.

La capacity channel se configure avant first use. Send, receive, try, timed, count, capacity, closed et close ont des status behavior séparés. Closing réveille les opérations bloquées ; error text est rendu dans un owned UTF-8 buffer lorsque l’API le fournit.

## Contextes callback

Native function, generic host-type factory, instance-call interceptor, mutation callback et external serializer/deserializer signalent un échec via leurs context setter et callback result value. Callback exception est convertie avant le retour via C. User data, host-object payload et managed root restent vivants pour toute callback entry possible.

Les descripteurs ApplyConfig invalides sont rejetés avant application. Un échec d’allocation ou une erreur interne pendant l’application Core peut laisser une configuration partielle ; réappliquez une configuration complète avant de vous fier aux paramètres. Le handle d’annulation C suit le contrôle réellement installé, en cas de succès comme d’échec. Les régressions Linux vérifient l’activation/désactivation, la cohérence de l’annulation et la reprise après un nouvel essai.

External serialization peut aussi enumerate les références avec le visitor contract ABI. Reference kind, visitor order et borrowed-value lifetime sont des données machine-facing ; visitor ne retient pas une borrowed value ni ne mute owning session en réentrance. Map-entry/set-element visitor suivent la même borrowed-view rule et propagent callback failure sans changer une traversal partielle en succès.

Reentrant destruction/session clearing n’est pas une fonctionnalité callback générale. Si implementation la refuse, ABI renvoie engine error et préserve owning session au lieu de la clear partiellement.

## Paquets et réflexion

Les API package lisent/détruisent handles package, exposent kind/name/version, metadata, canonical signed bytes et opérations load/reload/build. Reflection exporte JSON metadata pour loaded type, field, method, global, enum et interface. JSON field name/type name sont machine contract et non localisés.

`vs_interop_session_build_package_file` rapporte un build failure par structured diagnostic avec `message_id` `diag.failed_to_build_package`. Son premier diagnostic argument est la build reason sous-jacente ; les consumers utilisent `vs_interop_session_format_diagnostic` pour la locale sélectionnée, sans prendre `message` pour un remplacement du diagnostic contract.

## Canaux

Les channel values sont créées par `vs_interop_value_create_channel` ou `vs_interop_value_create_bounded_channel`. Une bounded capacity zéro signifie un channel unbounded. `vs_interop_value_channel_send` bloque jusqu’à acceptation, `vs_interop_value_channel_try_send` est non-blocking et `vs_interop_value_channel_send_for` applique timeout. La famille receive correspondante a les formes blocking/non-blocking/timed. Count, capacity, closed-state et close sont des ABI calls séparés. Closing réveille les waiters ; host traite closed comme terminal et ne réutilise pas une borrowed value après son callback scope.

## Contexte generic host-type

Les fonctions `vs_interop_generic_type_context_*` exposent argument count et arguments du generic host-type factory. Factory fournit result type, member, protocol declaration et error via le même borrowed context. Context accessor/setter ne sont valides que durant factory callback. Callback doit finir registration avant retour ; context n’est pas retained builder object.

## Appels et interception

`vs_interop_intercept_context_*` expose receiver, resolved target, argument, `proceed` et callback error channel. `proceed` est la continuation interceptor du call déjà résolu. Un host ayant besoin d’un inbound dispatch ultérieur peut utiliser session call API qui bypass entry interceptor ; les nested calls du method body restent interceptables.

`vs_interop_call_context_*` donne accès aux arguments empruntés ou modifiables, à `host_this`, à la génération du programme et au canal d’erreur. Les appels de fonctions et d’objets appelables respectent les règles de réentrance et de durée de vie de la session. Les interfaces de conservation sont `vs_interop_session_retain_callable`, `vs_interop_callable_destroy`, `vs_interop_callable_is_valid` et `vs_interop_callable_invoke`. La restriction au thread propriétaire et la capture faible de `this` définissent la durée de vie ; elles ne synchronisent pas les accès entre threads.

## Contextes de sérialisation externe

Le contexte serializer expose le type enregistré, la version de mutation et les champs empruntés. Le serializer copie ses octets via `vs_interop_external_serialize_context_set_bytes` ; un remplacement échoué conserve le payload déjà accepté. Le deserializer lit les octets et la version encodée, puis ajoute des affectations de champs nommés à une transaction temporaire. Les deux signalent les échecs par leurs error setters et callback results. L’énumération des références est un parcours séparé qui ne prolonge pas la durée de vie des valeurs empruntées. Les adapters copient les octets et chaînes conservés après le callback.

## Règles des noms d’annotation

Le vocabulaire enregistré avec `vs_interop_session_register_annotation` utilise des noms exacts par défaut. Définissez `name_flags` à `VS_INTEROP_ANNOTATION_NAME_IGNORE_CASE` pour ignorer la casse ASCII du nom de cette annotation. Les noms des arguments et les clés de métadonnées restent exacts ; la réflexion conserve la graphie source. Les inscriptions exactes et insensibles à la casse qui se chevauchent sont rejetées sans modifier le catalogue. Initialisez `struct_size` à `sizeof(vs_interop_annotation_desc)` ; les descripteurs incomplets et les indicateurs inconnus sont rejetés. Recompilez l’hôte avec l’en-tête Interop actuel et la bibliothèque correspondante.

## variant

Voir [SDK d'interopérabilité — variant](../../User/fr-FR/index.md#exported-value-lifetime).

`VS_INTEROP_VALUE_VARIANT = 33`; `VS_INTEROP_NATIVE_VARIANT = 29`; `VS_INTEROP_CAP_VARIANT = 0x01000000`.

```c
vs_interop_value_set_variant_empty(box);
vs_interop_value_set_variant(session, box, type_name, payload);
vs_interop_variant_has_value(box, &present);
vs_interop_variant_get_type_name(box, &type_name);
vs_interop_variant_get(box, output);
```

Une destination variant connue permet un boxing implicite. La lecture exige cast<T> ou try_cast<T> ; variant_is<T> teste le type exact sans conversion. optional<T> pour try_cast nécessite core.vs. Une extraction vide échoue ; une conversion incompatible produit un optional vide. Les échecs de copie, annulations et limites restent des erreurs.
## Appartenance aux sessions et instantanés des rappels

Les conteneurs mutables du module exportés vers C conservent leur session d’origine, même lorsqu’ils sont vides. Tant que la session reste ouverte, les alias C et les clones partagés maintiennent le graphe sous-jacent en vie ; libérer un handle ne détruit pas les objets encore retenus par un autre. Le nettoyage devient possible à un point sûr de la session propriétaire après libération de la dernière référence qui les retient. Cela n’ajoute aucune restriction de session aux scalaires, valeurs hôtes ou valeurs enum indépendants.

Les conteneurs mutables créés en C et susceptibles de contenir des graphes dépendant du module sont liés à une session dès leur premier passage à celle-ci, même vides. Les workers ne peuvent exporter que des valeurs indépendantes vers des handles C propriétaires ; les conteneurs avec des emplacements `variant` ou fonction sont exclus même vides, et ce stockage C externe ne peut pas être transmis au worker pour modification.

Les graphes d’objets et de conteneurs dépendant du module conservent leur session d’origine après copie. Les appels, champs, variables globales, hooks et sérialiseurs rejettent un graphe étranger avec `ENGINE_ERROR` avant tout accès aux métadonnées ou mutation. Les scalaires et valeurs hôtes indépendants restent transférables. Les contextes empruntés d’appel natif et de désérialisation gardent leur session d’origine pendant un travail synchrone dans une autre session ; leur durée de vie se termine toujours avec le rappel initial.

Tant que l’initialisation du module de remplacement n’est pas validée, les résultats du script dépendant du module ne peuvent pas être conservés dans des handles de valeur C propriétaires. Les tentatives d’affectation et de clonage renvoient `ENGINE_ERROR` ; une affectation refusée conserve la valeur précédente du handle de destination. Un emplacement de retour emprunté par un rappel peut transmettre le résultat au même appel du script, notamment avec `vs_interop_intercept_context_proceed(context, out_return)`. Les résultats temporaires restent nettoyés aux points sûrs de leur session propriétaire.

`vs_interop_value_reset` refuse les arguments et emplacements de retour empruntés d’un rappel avec `INVALID_ARGUMENT`. Réinitialiser un handle propriétaire ou remplacer sa valeur par un scalaire indépendant supprime son ancienne appartenance à une session et permet de l’utiliser indépendamment.

Un visiteur de map/set peut réinitialiser ou détruire le handle propriétaire de la source. L’énumération conserve le graphe enregistré jusqu’à la disparition de toutes les vues de l’instantané ; chaque vue empruntée n’est valable que pendant son appel au visiteur. Clonez-la pour la conserver. Les restrictions de réentrée d’exécution et de configuration restent applicables.

Une configuration complète conserve les limites numériques, mémoire et de bibliothèque standard définies par leurs setters dédiés. L’enregistrement de mémoire externe n’adopte son propriétaire qu’après publication réussie de la sortie ; tout échec laisse cette responsabilité à l’appelant. Les exceptions d’exécution réinitialisent les résultats valides en échec et les erreurs d’allocation conservent `OUT_OF_MEMORY`.

Le nettoyage d’une valeur retirée peut échouer après validation de son remplacement ou de sa réinitialisation. La valeur publiée reste en place et l’erreur de nettoyage est conservée dans le canal d’erreur de la session précédente ; la publication n’est pas déclarée en échec rétroactivement.

Les valeurs fonction C et les callables conservés suivent les règles de fermeture de Core : les clones partagent un environnement de capture mutable, chaque nouvelle fermeture crée un environnement indépendant et les captures par référence peuvent survivre à leur portée source. Les variables statiques locales appartiennent à leur déclaration dans l’état du programme et sont partagées dans cet état. Utilisez `vs_interop_session_set_hot_reload_enabled` pour conserver cet état entre appels C ; chaque exécution ordinaire initialise et libère son propre état. Leurs emplacements privés ne sont pas des globales réfléchies. Ces fonctions utilisent la VM ou le JIT sélectionné via l’API C existante.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/values-and-callbacks.md) · [fr-FR](values-and-callbacks.md) · [ja-JP](../ja-JP/values-and-callbacks.md) · [ko-KR](../ko-KR/values-and-callbacks.md) · [zh-CN](../zh-CN/values-and-callbacks.md) · [zh-TW](../zh-TW/values-and-callbacks.md)
