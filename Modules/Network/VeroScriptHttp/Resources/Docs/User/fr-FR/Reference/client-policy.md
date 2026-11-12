# Politique du client HTTP

`FClient(Context, Service, FClientOptions)` fige la politique du pool et des Cookies. Les Cookies et les retries automatiques sont désactivés par défaut. `MaxConnections=0` utilise la limite du contexte ; les limites par hôte, le nombre et l’âge des connexions en cache s’appliquent aussi. `MaxPendingRequests` compte les files de connexion et les attentes de retry dans le délai initial ; un dépassement produit `QueueFull`. Les proxys d’environnement sont ignorés sauf si `bInheritProxyEnvironment=true` ; un `Proxy` explicite non vide prime. L’environnement du processus n’est pas modifié.

`Send` conserve la réponse complète : `MaxBodyBytes` doit respecter le budget du contexte et `Network::FBytes::max_size()` sur la plateforme ; sinon la soumission échoue avec `InvalidArgument`. `SendStream` garde une limite totale sur 64 bits et borne séparément la mémoire en attente avec `BufferBytes`.

Store Cookie isolé par client, réserve conservatrice MaxCookieBytes. Dépassement Limit et store vidé ; pas un cap physique. Cookies/ClearCookies/ImportCookies utilisent le format Netscape, validation et snapshot/compte. Persistance explicite, pas de fichier cwd. Allocation irrécupérable d’import peut laisser des mises à jour appliquées.

Une tentative par défaut, maximum 32 ; plusieurs exigent bAllowReplay et méthodes non sûres bAllowUnsafeMethods. Flux unique non rejouable. UploadFactory natif rend immédiatement une nouvelle source/longueur par tentative/rewind sur I/O, sans script ni travail bloquant ; null/consommée/exception/longueur incohérente échoue. Statuts/erreurs/retard exponentiel plafonné/Retry-After partagent le délai total ; annulation retire les timers. curl se traduit en Resolve/Connect/Read/Write/TimedOut/Allocation, sinon Protocol, SystemCode garde curl et erreurs de source restent. Pas de retry après publication des têtes/corps du flux.

Attempts/FAttemptHistory gardent instantanés/error/bFinal/bTruncated ; final ne libère pas physiquement. Pas d’utilisation concurrente ; réemploi final démarre une histoire sans changer l’ancienne. Historique/résultats gardent Owners/quota ; diagnostic incomplet tronqué ne change pas l’erreur. MaxTotalBodyBytes compte corps décodés, redirections comprises, MaxTotalHeaderBytes les têtes, MaxTotalUploadBytes les sources sans framing multipart. Tampons préfacturés avant replay, flux à l’acceptation ; limites individuelles aussi actives. Réservation n’est pas trafic. set_redirect_limit(0) interdit suivi. make_client_options/make_retry_options/create_client_with_options/set_retry_options/total_*_limit et opérations Cookie/historique exposent ces contrats.

## Exemples

```VeroScript
http::client_options options = http::make_client_options();
options.set_cookies(true);
options.set_max_cookie_bytes(8192);
http::client client = http::create_client_with_options(context, options);

http::retry_options retry = http::make_retry_options();
retry.set_max_attempts(3);
retry.set_allow_replay(true);
retry.set_delay_ms(100);
request.set_retry_options(retry);
```

## Valeurs par défaut

| API | Valeur |
| --- | --- |
| `MaxConnectionsPerHost / MaxIdleConnections / MaxPendingRequests` | `8 / 64 / 128` |
| `MaxConnectionIdleTime / MaxCookieBytes` | `120 s / 1 MiB` |
| `FRetryOptions::Delay / MaxDelay` | `100 ms / 30000 ms` |
| `bHonorRetryAfter / bRetryTransportErrors` | `true / true` |
| `Statuses` | `408, 429, 500, 502, 503, 504` |

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/client-policy.md) · [fr-FR](client-policy.md) · [ja-JP](../../ja-JP/Reference/client-policy.md) · [ko-KR](../../ko-KR/Reference/client-policy.md) · [zh-CN](../../zh-CN/Reference/client-policy.md) · [zh-TW](../../zh-TW/Reference/client-policy.md)
