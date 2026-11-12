# Compression WebSocket

CompressionSupported=capacité, bCompression=demande (false par défaut), CompressionInfo=résultat immuable survivant au close ; refus pair garde messages ordinaires. Fenêtres 15 par défaut, 9–15 ; takeover=false, Level=8 (0–9), MemoryLevel=4 (1–9). Client/server indique direction protocole ; sans accord désactivé/fenêtres zéro. Fenêtre 8 jamais convertie silencieusement : serveur saute l’offre incompatible, client refuse le choix. Config illégale InvalidArgument ; choix inconnu/dupliqué/manquant/non supporté Protocol avant allocation. Premier choix supporté en ordre, champs multiples/listes/guillemets admis.

Limites de payload décompressé, pas de contournement par compression ; Limit dépassement, Protocol DEFLATE/frame, Closed fin propre, Read I/O, TimedOut inactivité. Première cause terminale conservée après queue complète. Réserve contexte 1 Mio avant codecs, relâchée si refus sinon jusqu’à destruction réelle ; pas pic physique exact. Sans compilation compression, activer donne Unsupported mais ws/wss ordinaire reste. Descripteur websocketCompression. Script make_compression_options et setters fenêtres/niveaux/takeover, set_compression_options copie ; connection.compression donne l’accord. Texte/binaire/vide/fragments/contrôles inchangés.

## Exemples

```VeroScript
websocket::compression_options compression = websocket::make_compression_options();
compression.set_server_window_bits(12);
compression.set_client_no_context_takeover(true);
websocket::options options = websocket::make_options();
options.set_compression(true);
options.set_compression_options(compression);
// Query after a successful connection.
websocket::compression_info actual = connection.compression();
```

<!-- docs-navigation -->

---

[WebSocket](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/websocket-compression.md) · [fr-FR](websocket-compression.md) · [ja-JP](../../ja-JP/Reference/websocket-compression.md) · [ko-KR](../../ko-KR/Reference/websocket-compression.md) · [zh-CN](../../zh-CN/Reference/websocket-compression.md) · [zh-TW](../../zh-TW/Reference/websocket-compression.md)
