# Reconnexion WebSocket bornée

ConnectWithRetry crée une ronde explicite sans remplacer l’ancien handle ni rejouer de données. MaxAttempts=1 par défaut, ≤32 premier inclus ; Delay≥0, MaxDelay≥Delay, doublement plafonné. MaxElapsed positif ≤un an couvre queue/proxy/TLS/handshake/attente ; délai par tentative tronqué au restant, jamais réinitialisé par Wait. Resolve/Connect/Read/Write/Closed/TimedOut réessayables, Protocol seulement bRetryProtocolErrors ; autres InvalidArgument/Unsupported/QueueFull/Limit/Allocation/Cancelled non. Timers async demandent HostDriven PollIo, pas dispatch script. Un slot global et au plus un essai actif plus connexion réelle.

Annuler arrête essai/attente/futurs, sans prouver absence de demande distante ; nettoyage suit Shutdown. Identité génération/essai rejette fin tardive ancienne. Confirmations/ordre/déduplication à l’application. TrySnapshot copie possédée, Snapshot partage immuable facturé ; Generation/Attempts/bFinal/Error et Number/bComplete/bConnected/Error. Codes stables/système conservés, texte ≤4 Kio à frontière UTF-8 avec bMessageTruncated. Pré-admission peut manquer d’histoire, lisez Operation.Error. Snapshots inchangés/copieurs après close, sans nouvel I/O ni VM empruntée. Script policy/set_limits/set_backoff/set_retry_protocol_errors, connect_with_retry ; rapport generation/is_final/size/at/diagnostics et essai number/complete/connected retient son snapshot après libération du rapport.

## Exemples

```cpp
WebSocket::FReconnectPolicy policy;
policy.MaxAttempts = 3;
policy.Delay = std::chrono::milliseconds(100);
policy.MaxDelay = std::chrono::seconds(2);
policy.MaxElapsed = std::chrono::seconds(15);
auto task = WebSocket::ConnectWithRetry(context, url, policy, options);
auto operation = task.Operation();
```

```VeroScript
websocket::reconnect_policy policy=websocket::make_reconnect_policy();
policy.set_limits(3,15000);
policy.set_backoff(100,2000,true);
websocket::reconnect_task task=websocket::connect_with_retry(context,url,options,policy);
websocket::operation opening=task.operation();
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/websocket-reconnect.md) · [fr-FR](websocket-reconnect.md) · [ja-JP](../../ja-JP/Reference/websocket-reconnect.md) · [ko-KR](../../ko-KR/Reference/websocket-reconnect.md) · [zh-CN](../../zh-CN/Reference/websocket-reconnect.md) · [zh-TW](../../zh-TW/Reference/websocket-reconnect.md)
