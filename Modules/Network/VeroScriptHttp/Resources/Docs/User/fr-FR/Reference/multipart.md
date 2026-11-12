# Envoi et analyse multipart

`FMultipartReader::TryCreate(content_type, out, error, options, context)` analyse la frontière et préserve `out` en cas d’échec. Le contexte natif est facultatif ; s’il est fourni, le stockage borné et les événements immuables restent comptabilisés tant qu’ils sont conservés, même après leur livraison ou la fermeture du contexte. Les copies partagent les données ; les événements restent lisibles après fermeture, mais le lecteur n’accepte plus de travail. `Feed(bytes, final, event, work)` rend les octets consommés et au plus un événement ; resoumettez le suffixe avec le même `final`. Seul `EventReady` modifie `event`. `final` signifie EOF réel ; le travail emprunté ne vérifie annulation et budgets que pendant l’appel.

États 0 NeedInput/1 EventReady/2 Done/3 Failed (nouveau reader). Événements 0 PartBegin(index/head)/1 Data(non vide ≤16 Kio/limite)/2 PartEnd/3 End ; End n’évite pas le drainage épilogue HTTP. Head conserve noms, nom de fichier absent ou vide, type, répétitions ordonnées. form-data exige un Content-Disposition form-data/name non ambigu (name vide permis), autres types peuvent omettre name. Multipart imbriqué reste brut. Noms de têtes insensibles à la casse, pliage espace, doubles paramètres/ambiguïtés refusés. NUL/encodage/ordre conservés, aucun décodage charset/percent/transfer. filename n’accède pas au disque ; limites/blocs/padding/préambule traités.

Défauts : 256 parties, têtes 64 Kio/part et 1 Mio total, corps 64 Mio/part et 256 Mio total, entrée 272 Mio framing inclus, pré/épilogue 64 Kio chacun. Limites indépendantes, aucun corps entier agrégé, événements conservés peuvent donner Limit. multipart_options expose les huit max_* ; reader feed/state/event/error_code/error, event kind/part_index/name/has_filename/filename/content_type/header_count/header/data avec type vérifié. Traitez événement/suffixe avant nouveau bloc, final après HTTP End ; annulation VM échoue, erreur de format reste état.

Upload par Body possédé, Upload unique ou UploadFactory exclusifs. Longueur connue borne l’envoi, source courte Protocol, excès non envoyé ; inconnue exige Finish, WouldBlock non EOF. Mélange/répétitions/binaires conservés. Chaque replay exige factory neuve sur I/O sans script/blocage et même longueur connue/inconnue ; sources uniques annulées en fin. MaxUploadBytes/MaxTotalUploadBytes hors framing, tampons prépayés et flux facturés à l’acceptation. Métadonnées/tampons au quota, flux par pipes. Backend génère Content-Type/Length/Transfer-Encoding. Script begin_multipart/add_field/add_file/*_stream, length=-1 inconnu, ≤256 parties/16 Mio metadata+tampons, pas de factory script I/O. Les adaptateurs de fichier sont disponibles ; filename seul ne lit/écrit rien.

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/multipart.md) · [fr-FR](multipart.md) · [ja-JP](../../ja-JP/Reference/multipart.md) · [ko-KR](../../ko-KR/Reference/multipart.md) · [zh-CN](../../zh-CN/Reference/multipart.md) · [zh-TW](../../zh-TW/Reference/multipart.md)
