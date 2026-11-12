# Client SSE

FSseClient utilise le flux HTTP borné, un seul appelant logique Poll/accesseurs/Close non concurrents. Pas de worker par connexion/script I/O. Connect demande GET sans corps/upload/factory/longueur/multipart, fixe Accept:text/event-stream et gère Last-Event-ID ; CA/proxy/limites de requête restent.

Défauts : total illimité, inactivité 60 s têtes/corps (pas les 30 s HTTP implicitement), Request explicite garde ses limites ; corps total illimité, anneau 256 Kio, ligne 64 Kio, événement 1 Mio, Poll 64 Kio. Contexte facture buffers/parser, copies d’événement au client. FSseWork emprunté teste au début/avant blocs à coût zéro puis facture les consommés ≤8 Kio, sans double suffixe. false Cancelled, exception Callback ; budget/annulation script reste échec VM.

Poll 0 Pending/1 EventReady seul modifiant Out/2 Reconnecting/3 Closed/4 Failed. HTTP200+type requis, 204 arrête reconnexion ; mauvais statut/type garde ResponseHead, EOF ne complète pas un événement. Reconnexion illimitée par défaut, 3 s initial/60 s max ; erreurs réseau/délai réessayables, pas protocole/config/limite/allocation. MaxReconnects compte la vie, 0 désactive ; limite : EOF propre Closed, réseau Failed. retry serveur persistant plafonné ; ID validé seulement à ligne vide complète, même sans data, puis hérité. Close idempotent ; reconnexion démarre seulement dans Poll, sans replay métier. Script options limits/reconnect/idle(-1)/request, connect_sse[_with_options], poll/close/event_*/last_event_id/status/reconnect*/error ; event_* exige dernier poll=1. Réponse SSE continue via reply_stream, distincte de reply_response tamponné.

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/sse-client.md) · [fr-FR](sse-client.md) · [ja-JP](../../ja-JP/Reference/sse-client.md) · [ko-KR](../../ko-KR/Reference/sse-client.md) · [zh-CN](../../zh-CN/Reference/sse-client.md) · [zh-TW](../../zh-TW/Reference/sse-client.md)
