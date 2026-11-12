# Flux d’octets bornés

VeroScriptNetworkStream.h fournit un anneau borné possédé, sans VM empruntée. Les copies partagent un curseur par producteur/consommateur logique. TryCreateBytePipe avec contexte facture toute la capacité ; sans contexte l’appelant natif gère les totaux. Quantités int64 vérifiées, capacité aussi dans size_t.

Progress=0 (jamais zéro octet sur requête non vide), WouldBlock=1 (rien consommé, cédez), End=2 (production finie et queue vide), Failed=3 (erreur structurée). Une requête vide ne teste pas EOF. Write accepte partiellement, WriteAll tout ou rien ; un bloc plus grand que la capacité échoue. Finish termine ; Fail vide les données avant erreur. Cancel/dernière référence lecteur jette les octets et notifie le producteur ; dernier écrivain non terminé donne Closed. Owner survit aux extrémités/stockage.

create_byte_pipe autorise 1 octet–16 Mio. reader/writer sont partagés ; read/write ≤64 Kio par appel, NUL conservé. state/error_code/error distingue attente/EOF, pas une chaîne vide. buffered/capacity sont int64 ; writer.finish/fail et reader.close terminent. En WouldBlock cédez puis pilotez poll propriétaire en HostDriven.

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/streaming.md) · [fr-FR](streaming.md) · [ja-JP](../../ja-JP/Reference/streaming.md) · [ko-KR](../../ko-KR/Reference/streaming.md) · [zh-CN](../../zh-CN/Reference/streaming.md) · [zh-TW](../../zh-TW/Reference/streaming.md)
