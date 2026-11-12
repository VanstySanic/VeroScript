# Fichiers et sortie atomique

FFileExecutor partage un pool borné par contexte ; les exécuteurs suivants exigent les mêmes options. Défauts : 2 workers, 32 tâches, blocs 64 Kio, vérification 2 ms. Bornes : 1–8 workers, 1–4096 tâches, bloc ≤limite contexte et 16 Mio. QueueFull au-delà, aucune occupation de worker en attente de flux.

Chemins UTF-8, relatifs seulement avec BaseDirectory absolu explicite ; pas de cwd implicite, création de parents ni fichiers spéciaux. La taille à l’ouverture fixe la lecture, raccourcir ensuite donne Read, sans snapshot de modifications concurrentes. Videz Body pendant Read avant d’attendre. Write prend un lecteur non consommé unique et le termine sur toute issue ; réemploi/annulé donne State. Offset/Length/MaxBytes, ExpectedLength/MaxBytes sont vérifiés ; natif uint64 et offset signé, script int64 non négatif, -1 réservé à l’inconnu.

Temporaire exclusif voisin, flush avant commit par défaut, remplacement seulement avec ReplaceExisting. Basculement visible atomique, sans promesse de durabilité du répertoire après panne. PrefixPath/PrefixLength copie un préfixe de longueur exacte par blocs sans le modifier ; même cible seulement avec remplacement autorisé. ExpectedLength inclut ce préfixe ; écart Protocol, dépassement Limit. Résultats/quota préparés avant commit.

Les temporaires non validés sont supprimés sauf KeepPartial/PartialPath. Annuler peut concourir avec I/O/rename et ne défait pas le fichier validé. Progress : Bytes/TotalBytes/Path/PartialPath/CleanupSystemCode/PhysicalComplete ; Commit=0 Uncommitted/1 Committing/2 Committed/3 Failed. Attendez PhysicalComplete puis combinez état/erreur. Close touche cet exécuteur ; Shutdown joint tous les workers et peut attendre un appel système. Pas de script sur ces workers ; quotas hors caches OS/copies de l’appelant. Script create_file_executor[_with_limits], options de lecture/écriture pour plages/limites/délai/longueur/préfixe/commit ; continuez à vider/piloter HostDriven.

## Valeurs par défaut

| API | Valeur |
| --- | --- |
| `FFileReadOptions::MaxBytes` | `256 MiB` |
| `FFileWriteOptions::MaxBytes` | `256 MiB` |
| `bReplaceExisting / bKeepPartial / bFlushToDisk` | `false / false / true` |

<!-- docs-navigation -->

---

[Network](../index.md) · [VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../../en-US/Reference/files.md) · [fr-FR](files.md) · [ja-JP](../../ja-JP/Reference/files.md) · [ko-KR](../../ko-KR/Reference/files.md) · [zh-CN](../../zh-CN/Reference/files.md) · [zh-TW](../../zh-TW/Reference/files.md)
