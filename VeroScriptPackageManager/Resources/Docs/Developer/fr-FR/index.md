# Maintenance du gestionnaire

Le client possède acquisition, ZIP, résolution, reçus, transactions, publication et composition. Core possède les contrats résolus modules/ressources/natif, indépendants de HTTP, archives et résolution. Le serveur possède comptes, autorisations, stockage immuable, politiques et index.

## Compilation et responsabilités

VEROSCRIPT_BUILD_PACKAGE_MANAGER=ON est le défaut bureau ; embedded/mobile/Wasm/Python-only le désactive par défaut. Conservez curl statique épinglé, miniz, nlohmann/json, PicoSHA2 et leurs licences dans le module. VEROSCRIPT_PACKAGE_MANAGER_USE_SYSTEM_CURL ou une cible CURL existante exige CURL 7.85+ et ses dépendances d’exécution. La source curl intégrée est épinglée par SHA-256.

Le composant VeroScriptPackageManager installe les en-têtes, bibliothèques, schémas, identités des dépendances et notices légales. Les exemples et README ne sont pas installés ; publiez les documents en six langues via Resources. Avec curl intégré, installez aussi son composant de build ; le conditionnement Core SDK effectue les deux étapes. Il finalise ensuite la dépendance vers la version Core SDK publiée et le SHA-256 de la bibliothèque Core effectivement préparée, y compris après suppression des symboles ; la version native ou un ancien hash ne suffit pas. Le consommateur externe Core SDK crée aussi le transport HTTP pour exercer les dépendances de liaison ZIP/HTTP. Le diagnostic anglais et le code structuré de l’API source restent indépendants des textes affichés par les paquets.

## Transactions et durée de vie

Préparez instantanés et sorties générées, publiez le contenu immuable, puis validez exigences/verrou/état/propriété par journaux récupérables. Revalidez les entrées et l’identité du stockage avant commit ; préservez les modifications externes et refusez les plans périmés. Récupération et rollback n’héritent pas d’un jeton déjà annulé. Refusez les lectures d’une transaction incomplète.

Chaque génération garde un bail fichier interprocessus. clean contrôle reçus, toutes configurations cibles, propriété, contenu et baux ; préservez fichiers modifiés/étrangers. Ne collectez les orphelins publiés qu’avec reçu propriétaire et métadonnées de bail complets. L’hôte gère les préparations abandonnées. Les callbacks empruntés sont synchrones ; pas de réentrée dans le même environnement. Les implémentations natives retiennent modules et instantanés jusqu’aux destructeurs finaux.

Avant toute écriture, la reprise valide le journal entier : destinations autorisées uniques, données string/null, budgets et contenu actuel. Chaque fichier doit correspondre à `old` ou `new` ; sinon `recovery-conflict` conserve le journal et les modifications externes. Revérifiez à chaque écriture et ignorez les valeurs déjà restaurées. Un rollback bloqué conserve la transaction avec `recovery-required`. Sauvegardez les modifications, restaurez une valeur enregistrée, terminez la reprise puis réappliquez les modifications. `JournalRecoveryTests` couvre commits partiels, rollback, fichiers absents, journaux invalides, doublons et éditions pendant l’application.

## Compilation des hooks

Chaque compilation de prévalidation/exécution transmet `FLimits.HookConstantEvaluationLimits` à `FCompileOptions.ConstantEvaluationLimits` du Core. Cette politique appartient à l’hôte, sans champ JSON ni verrou. Travail, profondeurs et mémoire doivent être positifs ; un cache nul est permis. Préservez le délai global, l’annulation, l’observateur d’analyse et le diagnostic initial. Les limites VM restent indépendantes. `ConstantHookTests` couvre imports source/bibliothèque, branches génériques écartées, uint64, budgets, annulation ponctuelle, nouvelle tentative sans cache et transaction de désinstallation.

## Vérification

Construisez VeroScriptPackageManagerTests ; exécutez VeroScriptPackageManagerCliTests et VeroScriptPackageManagerSdkTests. Couvrez sources immuables, cache hors ligne, cibles/usages optionnels, archives/noms, portée des secrets, langues/formats, entrées typées/secrètes, budgets hooks, transactions interrompues, éditions concurrentes, baux, analyse/enregistrement natif, JIT explicite et déploiement. Le consommateur SDK doit rejeter un Core différent. Validez plateformes et appareils séparément.

```sh
cmake -S . -B build/package-manager -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVEROSCRIPT_BUILD_PACKAGE_MANAGER=ON
cmake --build build/package-manager --config Release --target VeroScriptPackageManagerTests VeroScriptCli
ctest --test-dir build/package-manager -C Release --output-on-failure -R VeroScriptPackageManager
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
```

[Protocole du registre](registry-protocol.md) · [Utilisateur](../../User/fr-FR/index.md) · [Schémas](../../../Schemas) · [Exemples](../../../Examples)

## Listes de dépendances

`PackageRequirements.cpp` développe les arguments d’installation et `FRequest.RequirementFiles` avant mutation. Préservez validation UTF-8, détection de cycles, fichier/ligne initiale et budgets MaxManifestBytes/MaxMetadataBytes/MaxFiles/MaxDependencyDepth/MaxEdges. Ne fusionnez les contraintes que pour des sources/configurations compatibles. Conservez les hashes et revérifiez avant commit ; une édition concurrente renvoie requirements-changed. Les fichiers ne peuvent autoriser des hooks ni modifier la politique des registres. Couvrez options CLI courtes/longues, C++, chemins imbriqués et cités, résolution contrainte et répétition locked/offline. Champs de protocole et codes diagnostiques restent stables ; les listes sont des entrées utilisateur, pas des catalogues de traduction.

## Service de listes de dépendances

`FRequest.RequirementFiles` expose le même service réservé à l’installation que `-r` / `--requirement`. Les chemins relatifs initiaux exigent `Environment.WorkingDirectory` ; les chemins imbriqués partent du fichier contenant. `Private/PackageRequirements.cpp` analyse avant toute écriture de gestion, borne lectures UTF-8, inclusions, entrées et métadonnées avec `FLimits` / `FWork`, puis vérifie les empreintes avant validation. Les dépendances et sources normalisées sont dans `veroscript-project.json` / `veroscript.lock` : un projet verrouillé peut être distribué sans les listes originales, mais un nouvel appel explicite à `-r` exige les listes et les archives locales. Aucun changement du cœur, des routes du registre ni de version de format. Couvrir les [listes](../../User/fr-FR/index.md) dans les tests bibliothèque, CLI et SDK installé.

## Intégrité des environnements et des sorties

La sélection en lecture seule refuse une première installation interrompue même sans projet ni état. Un journal `committed` résiduel n’est lisible qu’après validation de son identité, de ses destinations et de chaque fichier courant par rapport aux octets `new` ; la lecture ne supprime jamais le journal. Le budget doit couvrir la phase la plus longue avant toute écriture.

La mise à jour en ligne considère les `signatures` comme des métadonnées verrouillées immuables. `pack` et `deploy` publient les sorties préparées de façon atomique sans remplacer une destination existante. Un fichier peut utiliser un lien physique sûr ; un répertoire exige un renommage natif sans remplacement. Une opération non prise en charge échoue sans repli vers un écrasement.

La composition conserve les arêtes validées du cliché, y compris les déploiements sans dépendances réservées à la compilation. Un paquet traité exige la même configuration et tout le contexte des dépendances. Un chargeur natif sans traitement d’installation exige seulement l’identité immuable des fournisseurs externes de bibliothèques effectivement référencés. Sinon, renvoyer `combine-processing` et installer le graphe souhaité dans un même environnement. Une dépendance script sans rapport n’invalide pas un chargeur.

Les déploiements conservent la configuration de chaque paquet dans sa valeur et dans le cliché du projet, y compris comme overlay. Le répertoire de sortie accepte un séparateur final. `list` et `show` refusent une première installation interrompue même avant la création de l’état. Avant le commit, tous les enregistrements écrits ou conservés sont revérifiés sans autre rappel de l’hôte, en gardant l’échéance initiale. Le rollback préserve les modifications externes ; un conflit sur un enregistrement écrit conserve le journal de récupération.

## Intégration des applications et de la construction

`VeroScriptPackageRuntime` charge les environnements figés et les déploiements, ressources et services natifs, sans HTTP, résolution, installation, hooks ni publication. `FClientOptions.ArtifactDecoder` vérifie les archives ; `FCoreEnvironmentOptions.ArtifactDecoder` lit les bibliothèques protégées. Aucune clé privée ni rappel natif ne figure dans les verrous. La politique de signature s’applique aussi au cache.

Un déploiement conserve runtime par défaut, et runtime＋compile pour les sources. Les consommateurs gardent un bail partagé `AcquireDeploymentDirectoryLease` ; le remplacement exige un bail exclusif. `Build::FServices` fournit les rappels `Deploy` et `LeaseDeployment`.

Les URL du registre restent inchangées. Ajoutez `signatures`, tableau facultatif non vide de 1 à 16 enregistrements liant distribution, taille réelle, SHA-256, algorithme et identité de clé. Allocation, publication, détails, confirmation, reprise, verrou et miroir doivent conserver les mêmes enregistrements. Le serveur recalcule taille et empreinte. Les artefacts existants restent immuables ; l’ajout de plateformes conserve les métadonnées, refuse les cibles communes et reste atomique et idempotent. Aucune clé privée n’est envoyée. Un serveur incompatible doit refuser clairement le champ.

`veroscript install name --application-options /absolute/trust-options.json` configure le consommateur. Les archives locales utilisent `<archive>.sig.json`. Aucun URL de signature n’est deviné pour un téléchargement direct : utilisez les métadonnées du registre ou des fichiers locaux explicitement téléchargés. Le ZIP installable reste lisible et distinct du conteneur applicatif.

<!-- docs-navigation -->

---

[VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md) · [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
