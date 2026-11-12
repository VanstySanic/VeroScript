# Flux d’octets bornés

SendStream donne Operation/Body/TryHead, attente de tête false sans erreur, tête immuable sans duplication dans response.Body ; fin donne BodyBytes/trailers. Tampon ≥16 Kio et ≤limite contexte, distinct du total. Videz pendant le transfert. Les blocs curl se suspendent/rejouent sans duplication ; directions indépendantes, autres connexions continuent. Annuler/abandonner reader annule ; octets déjà possédés lus avant erreur terminale. Pas de cap sur toutes les allocations TLS/curl/HTTP2/OS.

Upload est mono-consommateur, longueur facultative/framing backend, MaxUploadBytes=64 Mio. Court connu échoue, WouldBlock attend write/Finish. Source unique non réutilisable, incompatible avec corps tamponné/multipart/HEAD/redirection auto ; UploadFactory natif permet le replay. Content-Length/Transfer-Encoding gérés par backend. Script set_upload(length=-1), set_upload_limit, send_stream, operation/body/head_ready/head/body_bytes ; cédez en WouldBlock et pilotez HostDriven, budgets VM maintenus.

ReplyStream fige têtes/trailers et consomme un BodySource exclusif d’un Body non vide. États 0 Uncommitted/1 HeadersSent/2 BodyStreaming/3 Finished/4 Aborted ; BodyBytes compte blocs entièrement écrits. Fin = acceptation locale, pas traitement distant, pas de rollback envoyé. HTTP1.1 chunked si inconnu/trailers sinon longueur ; HTTP1.0 inconnu ferme, pas de trailers ; HEAD seulement têtes, 204/304 sans corps et source annulée. Longueur erronée Protocol, plafond Limit. Framing reconstruit, trailers sans champs de framing/connexion/routage. Arguments invalides précommit ne consomment pas exchange. Blocs ≤16 Kio distincts de pipe ; échec/cancel/délai ferme. ResponseTimeout inactivité, options.Timeout total ; SSE peut désactiver total, pas quota. reply_stream prend longueur/total -1, pipe même contexte, fournit operation/state/body_bytes ; trailers fixés avant soumission.

bStreamRequests=false par défaut. Activé : Accept après têtes, Request.Body vide, BodyStream/BodyOperation mono-consommateur ; résultat final body_bytes/trailers, tête précoce inchangée, 100-continue avant lecture. Tampon défaut 64 Kio, total MaxStreamBodyBytes, blocs ≤16 Kio/capacité. Videz ou annulez ; requête suivante après corps ET réponse, ancien exchange inutilisable pour elle. Options script valeur pour stream_requests/tampon/totaux/têtes/attentes/délais/TLS ; accès flux interdit en mode tamponné. Entrée/sortie progressent indépendamment.

Shutdown arrête admission/Accepts et connexions non livrées/inactives ; exchanges livrés finissent dans grace, sans requête suivante. Expiration force bForced, ClosedConnections compte les actifs initiaux, pas leur libération physique. Grace 0–365 jours et délai total restent actifs ; Cancel/timeout/Close conservent leur erreur. Deuxième shutdown actif : State, déjà fermé : succès/0. Gardez le handle, destruction=Close. Script shutdown(grace_ms,total_timeout_ms), shutdown_forced/shutdown_connections et requêtes header/trailer insensibles à la casse suivent les résultats typés.

## Exemples

```VeroScript
Network::byte_pipe output = Network::create_byte_pipe(context, 65536);
http::response_builder response = http::create_response(200, "");
response.set_header("Content-Type", "text/event-stream");
http::response_stream_operation sending = exchange.reply_stream(response, output.reader(), -1, -1);
Network::writable_stream writer = output.writer();
writer.write(http::encode_sse("hello", "", ""));
// On WouldBlock yield; retry the unaccepted suffix on a later tick.
writer.finish();
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
