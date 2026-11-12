# Chaînes de réponses et statistiques HTTP

FAttemptDetails immuable est partagé avec la dernière tentative réussie ; FAttemptHistory conserve aussi les têtes complètes après échec/annulation/délai/close. Données/code survivent au contexte, nouveaux snapshots restent facturés. Responses garde les têtes informationnelles/redirections/finale en ordre, sans têtes incomplètes/CONNECT. Status/Url/Method réels, Locations conserve répétitions/vides. bRedirectFollowed indique un suivi réel, RedirectUrl la cible résolue, RedirectMethod reste vide avant preuve d’une tête suivante.

Redirections désactivées par défaut. 301/302 peuvent POST→GET, 303→GET sauf HEAD, 307/308 gardent méthode/corps. Flux rejouables et délai/limites communs exigés ; suivi raté reste échec de transport/historique. Champs personnalisés suivent curl vers les destinations ; Authorization/Cookie/Host gardent ses règles spéciales, l’API de trace ne les contrôle pas. Le head précoce SendStream est un snapshot indépendant, Statistics.bFinal=false, jamais modifié ensuite.

Stats natives optional<uint64> : int64 script, -1 absent, dépassement erreur, zéro valide. Temps en µs : TotalTimeUs tentative seule ; QueueTimeUs attente curl après admission ; NameLookup/Connect/AppConnect/PreTransfer/StartTransfer sont jalons cumulés, RedirectTimeUs cumul avant transfert final. Réemploi/non-TLS peut donner AppConnect=0 ; les jalons avec redirections ne sont pas des phases isolées. NewConnections/FollowedRedirects comptent les événements réels.

LastTransferDownloadedBytes/UploadedBytes = payload curl final sans têtes, téléchargement avant décodage. ReceivedHeaderBytes inclut information/trailers sauf CONNECT supprimé. AcceptedBodyBytes inclut corps de redirection livré, sans double compte de replay suspendu. DownloadedBytes de tentative = corps accepté, UploadedBytes = payload final ; les quotas prépayés ne mesurent pas le fil. Pas de retransmission TCP, framing TLS ou mémoire totale. details offre response_count/response/statistics, head les champs status/url/method/location*/redirect*, stats en snake_case et is_final. Les réponses manuelles n’ont pas de détails client.

## Exemples

```cpp
request.follow_redirects(true);
request.set_redirect_limit(4);
http::transfer_details details = response.details();
http::response_head first = details.response(0);
http::transfer_statistics stats = details.statistics();
print(first.url());
print(to_string(stats.total_time_us()));
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/http-trace.md) · [fr-FR](http-trace.md) · [ja-JP](../../ja-JP/Reference/http-trace.md) · [ko-KR](../../ko-KR/Reference/http-trace.md) · [zh-CN](../../zh-CN/Reference/http-trace.md) · [zh-TW](../../zh-TW/Reference/http-trace.md)
