# Fichiers et sortie atomique

MakeFileUploadFactory crée une source asynchrone neuve par tentative/rewind, sans stat/read bloquant ni nouveau délai. Length absent va de Offset à fin ouverte ; Length=0 vérifie encore le fichier/EOF. Annuler ferme la source. Replay autorisé explicitement ; set_upload_file remplace corps/framing, add_field_path/add_file_path garde répétitions/binaire.

DownloadFile exige client/files du même contexte et GET sans upload ; Range/If-Range/préfixe sont gérés par lui. Représentation identity, flux bornés, commit seulement après vérification ; plafonds HTTP et fichier tous actifs. Le délai de requête couvre queue/réseau/préfixe/commit, celui de File.Operation est ignoré. Complet : 200 sans Content-Range et intégrité/longueur. Reprise : préfixe de taille Offset, strong ETag ou IMF-fixdate ; 206 même validator/plage jusqu’à fin/longueur cohérente ; 200 refusé ; 416 ne valide le préfixe complet que si taille totale=Offset et validator égal, corps d’erreur vidé sans ajout. Autres états/compression/plages échouent en préservant cible/préfixe.

Operation termine, ResponseHead immuable. Succès Response/File, Body vide, BodyBytes réseau. Progress inclut File/ReceivedBytes/RepresentationBytes/Resumed/AlreadyComplete ; File.Bytes inclut préfixe, ReceivedBytes le corps courant, erreur 416 incluse. EOF réseau n’est pas commit : inspectez PhysicalComplete/Commit. Options script fichier/plage/limites/délai/préfixe/commit et download set_file_options/set_buffer_bytes/set_resume/clear_resume ; pilotez encore HostDriven.

## Exemples

```VeroScript
Network::file_executor files = Network::create_file_executor(context);
Network::file_read_options input = Network::make_file_read_options("payload.bin", absolute_base);
input.set_range(0, -1);
http::request request = http::create_request("POST", url);
request.set_upload_file(files, input);

http::download_options output = http::make_download_options("received.bin", absolute_base);
http::download_operation task = client.download_file(files, get_request, output);
http::operation operation = task.operation();
```

<!-- docs-navigation -->

---

[Http](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](../../zh-TW/Reference/files.md)
