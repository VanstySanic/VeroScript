# Conteneurs et exécution des applications

Distribuez une application exécutable avec un conteneur ZIP `.vsapp` standard : `veroscript-app.json`, `veroscript-app.files.json` et généralement l’entrée compilée `program/entry.bin`. Les entrées sources sont aussi acceptées. Un `.vslib` contient une bibliothèque compilée. Les distributions installables `.vspkg/.zip` utilisent le manifeste racine distinct `veroscript-package.json` et doivent être installées avant utilisation.

Exécutez `veroscript application.vsapp` ou `veroscript-runtime application.vsapp -- arguments`. `--check` vérifie sans exécuter l’entrée. Le même contenu peut former un répertoire, un ZIP ou un exécutable PE/ELF facultatif. Runtime, environnement privé et lanceur sont indépendants. Le chargement ne résout ni n’installe les dépendances et ne recherche pas dans PATH.

En C++, utilisez `VeroScriptSession::LoadApplication`, ou `CreateApplicationServices()`, `FApplication::Open` et `Prepare`. Pour les sources, `FPreparedApplication::CompileSource` conserve les macros et options enregistrées. C# expose `VeroScriptSession.LoadApplication` et Python `Session.load_application`. Les services C ABI ont des fonctions de destruction correspondantes. Les chemins des options JSON doivent être absolus ; consultez les schémas. `VeroScriptSession::LoadFile` et `VeroScriptEngine::RunFile` reconnaissent également les conteneurs et dossiers d’application. `ReadEntryPackage` inspecte l’image sans charger les fabriques natives.

Voir [SDK C++ — Ordre d’intégration](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/index.md#explicit-vm-selection).

L’inventaire vérifie taille, SHA-256 et permissions de chaque fichier. Les chemins dangereux, liens, fichiers supplémentaires, ZIP endommagés et dépassements de budget sont rejetés. Les chemins internes UTF-8 sont limités à 511 octets. Le cache inscriptible est séparé du contenu et protégé par des reçus de propriété et des verrous entre processus.

L’adaptateur libsodium facultatif fournit XChaCha20-Poly1305 IETF et Ed25519 ; le ZIP externe reste lisible. Le consommateur définit les clés fiables et le seuil de signataires indépendants. Un échec ne provoque aucun repli en clair. Les répertoires et exécutables autonomes utilisent une signature du contenu ; la signature native reste une étape distincte. Ne distribuez jamais les fichiers de clés.

La délégation au Runtime a lieu au plus une fois et transmet arguments, sorties et code de retour. `VeroScriptSession::ReloadApplication` remplace code et environnement ensemble, avec restauration en cas d’échec. Respectez le thread propriétaire et les durées de vie ; l’annulation ne peut interrompre un rappel natif bloquant. SDK : `find_package(VeroScriptApplication 0.1.1 EXACT CONFIG REQUIRED)`.

La recomposition d’une application signée exige une politique de signature explicite pour le nouveau contenu.

Les applications GUI utilisent le même conteneur `.vsapp`. `gui-compile` fournit l’image GUI et ses ressources ; les commandes GUI et les commandes CLI générales inspect, de vérification et d’exécution lisent le même fichier. L’exécution exige un hôte avec GUI et des fournisseurs compatibles explicitement choisis.

## Options de l’hôte et signatures

`--application-options /absolute/host-options.json` sélectionne la configuration hôte. Le C ABI expose `vs_interop_application_services_create`, `vs_interop_application_read` et `vs_interop_session_load_application` avec leurs fonctions de destruction. Les chemins `cacheRoot`, `stdlibSearchPaths`, `cryptoLibrary` et `keyring` doivent être absolus. La confiance utilise `requireSignature`, `minimumSignatures`, `requiredKeys`, `policyIdentity` ; elle vient du consommateur, jamais de l’application chargée. Les API de paquets en mémoire acceptent aussi les ZIP applicatifs ordinaires.

Le chiffrement XChaCha20-Poly1305 IETF couvre les images compilées, bibliothèques et ressources explicitement choisies ; ZIP et métadonnées restent lisibles. Les imports sources chiffrés et ZIP d’installation chiffrés ne sont pas acceptés. L’adaptateur libsodium utilise une bibliothèque native absolue. Les tableaux Ed25519 contiennent 1–16 enregistrements liant type, longueur exacte, SHA-256 et ID/version de clé. Les signatures internes sont dans `veroscript-app.files.json.sig.json`, celles d’un artefact dans `<filename>.sig.json`. Supprimer une signature ne désactive pas la politique requise ; les alias d’une même clé ne comptent pas comme signataires indépendants et les clés inconnues/non fiables ne satisfont pas le seuil.

La délégation ne transmet pas les callbacks natifs ni les secrets en mémoire. GUI, cycle de vie mobile et validation des boutiques ne sont pas fournis par ce service. Avant de remplacer le code, libérez les racines externes de valeurs script ; un catalogue natif modifié exige `LoadApplication` et une nouvelle durée de vie. Le SDK se lie à `VeroScript::VeroScriptApplication` ; le descripteur runtime identifie Core, OS, CPU, environnement et ABI, indépendamment du descripteur d’installation de paquets.

Les règles communes de sélection automatique, explicite et désactivée sont décrites dans [Sélection des backends de service](../../../../../VeroScript/Resources/Docs/User/fr-FR/introduction.md#backend-selection).

## Options de backend d’exécution du Runtime autonome

Placez les options hôtes avant le chemin de l’application et ses arguments après `--`. `--vm-backend` et `--jit-backend` choisissent les services d’exécution, séparément des fournisseurs platform/graphics/GUI/image/font/text-layout. Ils acceptent `auto`, `none`, des fichiers/dossiers compatibles ou des ID installés si un environnement fournit le résolveur. La VM automatique peut utiliser le repli produit livré ; une désactivation explicite n’est pas remplacée. Les commandes suivantes vérifient/préparent sans appeler l’entrée, puis exécutent avec JIT désactivé. Un conteneur GUI exige toujours un hôte compatible avec GUI et ses fournisseurs.

```sh
veroscript-runtime --check --vm-backend auto --jit-backend none /absolute/application.vsapp
veroscript-runtime --vm-backend auto --jit-backend none /absolute/application.vsapp -- argument1
```

Les chemins de contenu applicatif et de cache doivent utiliser des entrées physiques. Sur macOS, les alias système appartenant à root `/tmp`, `/var` et `/etc` sont acceptés seulement vers `/private/tmp`, `/private/var` et `/private/etc` respectivement. Les autres liens symboliques et entrées spéciales restent refusés ; cela n’autorise pas les liens dans le contenu ou le cache.

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
