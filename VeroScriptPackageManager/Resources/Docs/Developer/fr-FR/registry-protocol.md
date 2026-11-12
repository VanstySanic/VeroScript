# Protocole du registre

Le registre doit lire `veroscript-package.json` à la racine du ZIP. Une configuration de projet ne peut pas le remplacer. Les routes API, identifiants de format JSON et version `0.1.1` restent identiques.

Protocole client 0.1.1 : API configurée, HTTPS, JSON UTF-8 et sourceId concordant. Ce contrat décrit le serveur à implémenter, pas un service hébergé.

La base API de la CLI officielle est `https://veroscript.org/api`, avec le `sourceId` logique `veroscript.official`. Ajoutez les routes ci-dessous à cette base. Les sources du projet/verrou et les options explicites API ou miroir conservent leurs règles de sélection ; les hôtes intégrés configurent leur propre source.

| Méthode | Route | Contrat |
| --- | --- | --- |
| GET | `/search?q=...` | `sourceId + search JSON` |
| GET | `/packages/{name}` | `veroscript-registry-package` |
| GET | `/packages/{name}/versions/{version}` | `veroscript-registry-version` |
| GET | `/artifacts/{id}/download` | `ZIP` |
| POST | `/uploads` | `veroscript-upload` |
| PUT | `{upload.url}` | `ZIP` |
| POST | `/packages/{name}/versions` | `mode=append + sourceId/name/version/dependencies/conflicts/artifacts/idempotencyKey` |

veroscript-registry-version contient dependencies/conflicts communs et artifacts : id, size, sha256, manifestSha256, target, requires, blocked facultatif. veroscript-registry-package contient formatVersion/sourceId/name/versions et éventuellement withdrawn/blocked. La réponse veroscript-upload fournit uploadId/method=PUT/url, avec headers/expiresAt facultatifs. Les ID non vides utilisent lettres/chiffres ASCII et `.`, `_`, `-`, sans être `.` ou `..`. Les artefacts sont des archives ZIP sans champ `kind`.

Une version a un artefact portable ou des cibles mutuellement exclusives, jamais un chevauchement portable/native ambigu. Nom canonique, version, dépendances et conflits concordent. sha256 porte sur les octets ZIP exacts ; manifestSha256 sur veroscript-package.json racine original, espaces inclus. .vspkg et .zip partagent le protocole.

## Ajouter des plateformes à une version existante

La même commande `publish` crée une version ou ajoute des archives `.vspkg`/`.zip`. Un artefact déjà identique ne nécessite aucun nouvel envoi. Nom, version, dépendances communes et conflits restent identiques, ainsi que les ID, octets, empreintes, tailles, cibles et exigences existants. Une cible portable recouvre toutes les cibles. Des valeurs différentes pour OS, architecture, environment, coreBuild ou une clé ABI commune prouvent la séparation ; des versions minimales ou capacités différentes seules ne suffisent pas.

Modifications requises côté serveur :

1. Conserver `POST /packages/{name}/versions` et exiger `mode: "append"`. Créer la version absente ou ajouter atomiquement les références fournies. La liste est un ajout, jamais un remplacement. Voir le [schéma de publication](../../../Schemas/veroscript-publication.schema.json).
2. Vérifier propriété, octets/manifeste téléversés et métadonnées communes immuables. Conserver les ID et champs existants ; réutiliser un artefact exactement identique. Vérifier le recouvrement avec tous les artefacts existants, bloqués compris, et ceux du lot. Refuser modifications, suppressions et recouvrements ; une version bloquée/retirée refuse les ajouts. Tout conflit rejette le lot entier.
3. Vérifier et insérer dans une même transaction concurrente. Deux ajouts disjoints peuvent réussir ; le contrôle préalable du client ne remplace pas cette transaction.
4. Persister clé d’idempotence et requête canonique. Même clé/requête : résultat d’origine ; requête modifiée : refus. Même règle pour l’allocation. Conserver les références d’envoi pour reprise, sans demander au client de persister URL signées ou secrets. Une allocation peut renouveler une URL signée expirée en conservant le même uploadId ; répéter PUT avec les mêmes octets doit être sûr.
5. Retourner la version complète et les ID stables de tous les artefacts. Le client contrôle les identités observées et tous les artefacts demandés, tout en acceptant les ajouts concurrents valides et disjoints.
6. Le GET d’origine ne renvoie 404 que si la version est absente et retourne l’état complet après validation. Précontrôle et reprise utilisent directement cette origine avec `Cache-Control: no-cache`, sans miroir ni cache d’installation. Une réponse perdue est confirmée par un GET correspondant ; sinon références et requête/clé exactes sont conservées pour reprise.
7. Rafraîchir ETag et métadonnées CDN/miroirs, tout en conservant identités et octets des anciens téléchargements. Les verrous par cible restent valides. Retrait/blocage sont des états séparés, pas un remplacement d’artefact.

Le protocole reste `0.1.1`, sans changement des API cœur, Interop ou JIT. Le serveur doit implémenter ces règles avant la publication réelle avec le client mis à jour.

Erreurs : code/message/requestId et détails structurés facultatifs. GET utilise ETag/If-None-Match et des reprises bornées, jamais des reprises aveugles de POST upload/commit. Borner les secrets aux hôtes/chemins exacts ; isoler la récupération par source, API et artefacts. Le serveur gère autorisation, expiration, validation, indexation, stockage, retrait/blocage, miroirs et audit.

[Schémas](../../../Schemas) · [Maintenance](index.md)

Le transport HTTP intégré revalide Authorization pour chaque URL normalisée, y compris après redirection. Les implémentations de `IHttpTransport` doivent appliquer `FHttpRequest.Credential` à chaque requête : un résultat vide supprime Authorization et les restrictions de redirection par origine/chemin restent applicables. Les signatures explicites des uploads sont distinctes des identifiants du registre. Les paramètres de requête sur le chemin exact restent dans la portée autorisée.

Les identifiants résolus ne sont pas envoyés si le décodage du chemin révèle des segments autonomes `.` ou `..`, y compris avec des séparateurs encodés. Les noms de fichier encodés ordinaires et les chemins UTF-8 restent valides.

Le registre officiel doit réserver les noms commençant par `vs-` aux éditeurs officiels autorisés, lors de la création du paquet et des publications suivantes. Le nom canonique du paquet AsmJit est `vs-asmjit` ; son identifiant de backend est `asmjit`. Cette réservation relève du contrôle de propriété et des autorisations côté serveur. Les clients conservent la validation générique des noms ; un préfixe local ou privé ne prouve pas une origine officielle.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md) · [en-US](../en-US/registry-protocol.md) · [fr-FR](registry-protocol.md) · [ja-JP](../ja-JP/registry-protocol.md) · [ko-KR](../ko-KR/registry-protocol.md) · [zh-CN](../zh-CN/registry-protocol.md) · [zh-TW](../zh-TW/registry-protocol.md)
