# Messages et contrôles WebSocket

`Send(FMessage)` exige message entier bFinal=true ; SendFragment construit un message, premier type fixe, dernier bFinal. Autre message entier durant fragmentation : State ; Ping/close permis. Annulation en queue ne change pas l’état, écriture active ferme. UTF-8 vérifié sur tout le texte, caractères séparables entre fragments ; total MaxMessageBytes, fragment rejeté ne consomme pas l’état achevé. Receive entier par défaut ; bReceiveFragments donne blocs bFirst/bFinal sans frontières frame/UTF-8 garanties. Il faut les deux vrais pour un message entier, jamais seulement le dernier bloc JSON. Snapshots immuables facturés tant que détenus.

Ping ≤125 octets dans queue série, Pong automatique. TryTakeControl non bloquant conserve out si vide ; drainez la queue bornée. Débordement ferme Limit, messages complets déjà reçus lisibles puis Limit, réceptions déjà annulées restent Cancelled. IdleTimeout=60 s/bKeepAlivePings=true, nullopt désactive, distinct du délai total.

Sous-protocoles ordonnés client/préférence serveur, tokens non vides sensibles à la casse, sans doublons, ≤128/16 Kio. Serveur premier choix commun ; bRequireSubprotocol=false sinon configuration/accord obligatoire. Client refuse choix double/multiple/non proposé. Résultat immuable ; prélecture conservée, réserve 64 Kio si préférences. Close annule les timers. Script options set_receive_fragments/compression/limits/timeout/idle_timeout/tls, send_*_fragment/ping/try_control, receive is_text/is_first/is_final/data, contrôle has_value/is_ping/payload ; sans événement payload vide et has_value=false. add/clear_subprotocols/set_require_subprotocol/subprotocol exposent l’accord.

## Valeurs par défaut

| API | Valeur |
| --- | --- |
| `FOptions::MaxMessageBytes` | `16 MiB` |
| `MaxQueuedBytes / MaxQueuedMessages` | `32 MiB / 128` |
| `Close(Code)` | `1000` |

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/websocket.md) · [fr-FR](websocket.md) · [ja-JP](../../ja-JP/Reference/websocket.md) · [ko-KR](../../ko-KR/Reference/websocket.md) · [zh-CN](../../zh-CN/Reference/websocket.md) · [zh-TW](../../zh-TW/Reference/websocket.md)
