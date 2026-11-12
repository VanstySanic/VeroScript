# Network — Référence réseau

Les règles communes d’installation des interfaces et de sélection des fournisseurs sont définies dans [Network](../index.md#backend-selection).

vs-network 0.1.1 fournit contexte/opérations, URL/DNS, TCP/UDP, flux, fichiers et TLS partagé. Http/WebSocket restent indépendants. ID natif network, namespace script Network, cible VeroScript::Network ; les Bindings exigent Core compatible, le SDK natif non. Les protocoles épinglent Network ; hors ligne, déclarez son archive locale.

FUrl::TryResolve suit RFC 3986 §5 pour URL réseau avec autorité : seuls les segments littéraux ./.. du chemin se normalisent. Doubles slashs, échappements et points dans query/fragment sont conservés. Requête vide distincte d’absente ; référence vide hérite de query et retire fragment. Chemin vide devient / ; URI opaque g:h refusée. Échec conserve la sortie. Encodage une fois par octet, plus/espace seulement en mode form, NUL conservé.

Un thread I/O natif par contexte en arrière-plan ; HostDriven exige PollIo borné sur le thread créateur. PollIo ne rappelle rien et ne se réentre pas. Dispatch exécute sur ce thread par lots ; une inscription/un appel d’achèvement par opération. État terminal et mise en queue sont distincts : continuez à piloter. Une inscription échouée par allocation se réessaie. Échec d’allocation de lot : 0/Allocation sans changer queue/quota ; les erreurs gardent leur code même sans texte.

Wait observe : 0 Completed, 1 WaitTimedOut sans annuler, 2 RequiresPoll en HostDriven inachevé. Délais en ms, -1 illimité, 0 immédiat, depuis soumission avec file d’attente. Un seul état terminal entre annulation/fin. Result<T> exige type/état réussi corrects. Shutdown annule et ferme les ressources ; résultats/Owners peuvent survivre sans reprendre I/O.

MaxRetainedBytes=256 Mio par défaut couvre buffers, pipes, queues, corps et résultats conservés. Fin logique/Shutdown ne libère pas les données encore détenues. MemoryUsage et retained_bytes/peak_retained_bytes/memory_limit donnent quantité/pic/limite, pas une borne RSS/OS/moteur/allocateur/copies/réallocations. Allocation/Internal de l’exécuteur ferme/réveille ; effectuez encore Shutdown/destruction. Le retrait n’exige pas de nouvelle liste ou chaîne d’erreur. DNS/fichiers non interruptibles peuvent retarder la fin physique, y compris un Close différé dans PollIo.

TCP : une lecture et une queue d’écritures bornée par MaxQueuedWrites/MaxBufferBytes. Annuler un élément en attente retire seulement celui-ci ; annuler I/O actif ferme socket/autres tâches. FTcpListener fournit accept asynchrone/close idempotent. UDP conserve datagrammes vides, tailles copiée/originale et troncature ; diffusion/multicast choisissent explicitement l’interface et dépendent du réseau réel.

parse_url/resolve_url exposent scheme/host/port/path/query/fragment/has_query/has_fragment/explicit_port/ipv6/authority/target/to_string/without_fragment. percent_encode/decode prend form. create_context/resolve/connect/listen/bind_udp donnent handles/opérations typés, puis send/receive/accept/close. done/state/cancel/error/error_code/message_id/wait et résultats vérifiés ; count est int64. Les handles retiennent contexte/code jusqu’à libération physique. RetainCodeOwner déduplique le contrôle partagé (alias null inclus), pas l’adresse brute ; sans bloc de contrôle l’Owner est invalide.

Chargez Network avant les protocoles choisis et déclarez leurs dépendances de paquet. Déployez le `VeroScriptBoostRuntime` correspondant avec les fournisseurs Boost, près du programme Windows ou dans le répertoire des modules natifs. iOS utilise l’enregistrement statique dans la même image hôte. Ces API natives ne fournissent ni transport navigateur/Node, ni await/future du langage.

- [Achèvement et durée de vie du programme](completion.md)
- [Diagnostics et localisation](diagnostics.md)
- [Fichiers et sortie atomique](files.md)
- [Flux d’octets bornés](streaming.md)
- [Configuration TLS](tls.md)

## Valeurs par défaut

| API | Valeur |
| --- | --- |
| `FContextOptions::MaxOperations` | `1024` |
| `MaxCallbacks` | `1024` |
| `MaxConnections` | `256` |
| `MaxQueuedWrites` | `64` |
| `MaxBufferBytes` | `16 MiB` |
| `MaxRetainedBytes` | `256 MiB` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/README.md) · [fr-FR](README.md) · [ja-JP](../../ja-JP/Reference/README.md) · [ko-KR](../../ko-KR/Reference/README.md) · [zh-CN](../../zh-CN/Reference/README.md) · [zh-TW](../../zh-TW/Reference/README.md)
