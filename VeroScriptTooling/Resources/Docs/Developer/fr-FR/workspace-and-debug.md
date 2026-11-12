# Espace de travail, LSP et DAP

## État workspace

`VeroScriptWorkspace` est la frontière stateful de project/module context : open-document overlay, source/import resolution, cross-file analysis, workspace symbol, preprocessor context et binding catalog. Overlay open prime fichier disque et sa lifetime finit à close. Option : import/stdlib/implicit paths, define/macro, policy, binding catalog ordonné. Catalog merge en ordre, duplicate reject et décrit host symbol, sans register runtime behavior.

`TryGetDocument` fournit une copie indépendante d'un seul buffer ouvert. Si le
chemin est absent, il retourne `false` sans modifier la sortie. `SnapshotDocuments`
sert aux requêtes sur tous les buffers ; une lecture LSP individuelle ne copie
pas les autres sources.

## Cycle de vie des documents et invalidation

LSP crée overlay à `textDocument/didOpen`, applique incremental change à `textDocument/didChange`, refresh optional `text` à `textDocument/didSave` et retire à `textDocument/didClose`. Diagnostic est publié après open/change accepted/save/configuration. Version est par URI ; une version non plus récente est ignorée et reopen crée une nouvelle overlay lifetime. `$/cancelRequest` peut annuler un pending request et retourne `-32800`.

## Flux des fonctionnalités de langage

Les services directement accessibles sont `VeroScriptAnalyzer`,
`VeroScriptComplete`, `VeroScriptCallHelp`, `VeroScriptHover`,
`VeroScriptNavigation`, `VeroScriptFindReferences`,
`VeroScriptDocumentHighlight`, `VeroScriptOutline`,
`VeroScriptWorkspaceSymbols`, `VeroScriptRename` et `VeroScriptFormat`.
Le serveur LSP expose la complétion et sa résolution, la navigation et la
hiérarchie de types, les symboles, les plages de sélection et de pliage, les
liens de document, les jetons sémantiques, l'aide à la signature, les références
et les surbrillances, le renommage, le formatage du document, d'une plage et à
la saisie, ainsi que les actions `source.formatDocument`, `source.organizeImports`
et `refactor.rewrite` ; les symboles d'espace de travail sont aussi disponibles
avec résolution.
L'action `source.organizeImports` n'est renvoyée que si les lignes d'import
exactement dupliquées peuvent être supprimées sans risque. `refactor.rewrite`
propose, à un curseur vide dans une classe ou une structure, des squelettes de
méthodes héritées ou d'interface à implémenter. Le
formatage d'une plage l'étend aux lignes physiques complètes et renvoie la
plage de remplacement effective. Les espaces de noms sont traités comme des
symboles sémantiques, y compris `std::` ; les emplacements issus uniquement de
l'hôte utilisent `veroscript:host-bindings`.

Pour la complétion, `FCompletionItem::MatchQuality` est le rang textuel
principal ; `SortPriority` ne départage que les égalités. Les adaptateurs doivent
préserver cet ordre : le `sortText` LSP le représente aussi, et `preselect` n'est
attribué qu'à une correspondance exacte, exacte sans casse ou par préfixe qui est
sans ambiguïté meilleure. Les mots primitifs restent `Keyword` dans completion,
hover et semantic tokens ; les conteneurs sont `TypeAlias`/`type`. Ce sont tous
des types intégrés avec détail localisé, aucune définition source et jamais cible
de renommage. Utilisez les clés de catalogue `tooling.builtin_type.detail` et
`tooling.builtin_type.documentation` pour l'affichage ; ne traduisez ni les
champs de protocole ni les identifiants de types.

Pour un membre d'un namespace qualifié, `FCompletionItem::Label` et `InsertText`
conservent le nom final et tout snippet produit par le tooling ; `LabelDescription`
contient le namespace qualifié. Lors de la sérialisation LSP, n'incluez le snippet
que si le client annonce `textDocument.completion.completionItem.snippetSupport` ;
sinon, envoyez le nom final en texte brut. Sérialisez le namespace dans le champ
LSP standard `labelDetails.description`. Le champ facultatif `$valencePresentation` ajoute
le label/la déclaration rendus par Valence et les plages sémantiques UTF-16,
que les autres clients LSP peuvent ignorer. Il ne doit modifier ni le filtrage,
ni le tri, ni l'insertion.

L'adaptateur LSP de tokens sémantiques fusionne les plages sémantiques résolues
avec une base lexicale. Ce secours couvre les mots du langage/de type, les
directives, les macros et les littéraux/commentaires sur chaque ligne physique
valide, reste utilisable pendant la récupération d'analyse et élimine les
chevauchements avant l'encodage du flux LSP.

## Diagnostics et localisation

Les diagnostics sont groupés par source file : imported file reçoit sa propre `textDocument/publishDiagnostics`. Document clean publie aussi et ancien diagnostic absent est clear par empty list. `initialize.locale` est BCP 47 ; preference non supportée garde catalog environnement et missing file/key fallback `en-US`. Identifier, argument, protocol field, URI/path, type/source text ne sont pas traduits.

## Débogage

`VeroScriptDebugSession` traduit les données Core de chargement, exécution, arrêt, pile, portée, valeur et évaluation ; `VeroScriptDapAdapter` les expose en DAP. `VeroScriptDapStdio` et `VeroScriptDapTcp` assurent le transport sans réimplémenter VM, points d’arrêt ou durées de vie. `VeroScriptDebugSession::SetOutputHandler` installe un `VeroScriptDebugSession::FOutputHandler`, appelé pendant `Run()` avec le texte et un indicateur d’erreur. `FDebugRunResult` conserve les flux complets dans `Output` et `ErrorOutput` ; DAP émet `stdout`/`stderr` avant la terminaison. Voir la [référence du serveur de langage](../../User/fr-FR/lsp.md).

## Contrat de la session de débogage

`LoadSource`/`LoadFile` retournent `FDebugLoadResult` avec overlay/define/import/optional catalog ; `Clear` invalide module/debug state et `SetBreakpoint` retourne session-local ID. `SetRunMode`, `SetExecutionBackend`, `SetMaxCallDepth`, `SetJitPolicy` précèdent run ; `GetLastJitStats` est le dernier snapshot. Attach force `Vm` et invalide JIT cache. Stopped : `EDebugStopReason`, `GetStackTrace`, `GetLocals`, `GetChildren`, `Evaluate`, `SetLocal`, `SetChild` ; les erreurs restent structured. `FDebugRunResult` sépare diagnostics, streams et exit code de bSuccess.

`BindBreakpoint` est le chemin strict de l’adapter : il résout une ligne physique one-based dans la preprocessor map d’une unique source compilée, puis n’enregistre que la location prétraitée exacte avec `SetExactBreakpoint`. Une demande inactive, absente, ambiguë ou hors limite, ou une demande non exécutable sans location admissible, ne crée aucune inscription et reçoit un texte localisé. Dans une fonction, une demande peut avancer jusqu’à la prochaine ligne physique exécutable ; l’initialisation du module exige la ligne exacte.

## Cycle de vie de l’adaptateur DAP

`VeroScriptDapAdapter` est request/event mapper in-process. Host appelle `Initialize`, `Launch` ou `Attach`, breakpoint, `ConfigurationDone`; celui-ci bloque `Run` et émet `Stopped`, output et terminal event. `Next`, `StepIn`, `StepOut`, `StackTrace`, `Scopes`, `Variables`, `Evaluate`, `SetVariable` sont valides uniquement depuis stopped callback. `Attach` charge, ne joint pas VM en cours ; `Disconnect` resume puis cleanup. Live `IHostBindingCatalog` in-process ne passe pas out-of-process.

`setBreakpoints` conserve des ID DAP stables quand une source remplace ses entrées. Avant le chargement, les entrées sont pending et non vérifiées. Le chargement les rebind via `BindBreakpoint`, puis émet pour chacune un événement `Breakpoint` que le transport sérialise comme événement DAP standard `breakpoint` avec `reason: "changed"`. La ligne DAP renvoyée est la ligne physique effectivement liée, et peut différer de la ligne demandée.

## Sortie de débogage et fin d’exécution

`VeroScriptDebugSession::Run` envoie stdout line-buffered ; ligne complète, `std::console::flush` ou bounded buffer flush fragment et stderr est immédiat. Handler exception est isolée. Int main successful peut avoir ExitCode non zéro ; bSuccess est execution success. Source preprocessor line map remappe breakpoint/stack, et conditional evaluation utilise active define/macro.

## Transport DAP et contrat de lancement

`VeroScriptDapStdio::Run` utilise DAP Content-Length. `FDapStdioRunOptions` donne binding/catalog, args, stdlib, environment, cwd, input ; live binding est borrowed pendant `Run`. Launch/Attach acceptent `program`/`source`, `fileLabel`, `workspace`, `cwd`/`workingDirectory`, `stopOnEntry`, `args`, `input`, `env`/`environment`, `stdlibSearchPaths`/`stdlibPath`, policy, `backend`, `maxCallDepth`. `args` va à `main(string[] args)`, `input` est stdin script et environment est restored après run. Limites : 4096 args, 2048 paths, 1024 environments ; entrée invalide est reject avant run.

Pour un point de terminaison arrêtable, fournissez un `FDapTcpRunControl` partagé dans `FDapTcpRunOptions::Control`. L’appel hôte à `RequestStop()` réveille les attentes socket et détache le domaine selon sa politique de reprise. Attendez la fin du thread `Run` avant de libérer le domaine ou de décharger le module. Créez un nouveau contrôle pour redémarrer après un arrêt.

`VeroScriptDapTcp::Run` sert IPv4 ; default `127.0.0.1`, empty/`0.0.0.0` all interfaces, `Port == 0`/`OnListening`, `MaxSessions == 1`/`0`/negative = one/unlimited/one. Terminate stoppe server, les autres erreurs restent non-zéro.

## Locale DAP et terminaison Attach

DAP stdio lit initialize.arguments.locale comme BCP 47 avant response, sans changer catalog environnement si unsupported. Launch/Attach ont mêmes source-load options, Attach n’est jamais mid-run VM attach. Après Attach, Disconnect default terminateDebuggee false ; Launch default true. False libère session sans demander terminate host, true le demande ; TCP conserve alors MaxSessions policy.

## Déboguer une Session existante

L’hôte fournit `IVeroScriptLiveDebugDomain` et le maintient en vie. `Info()` renvoie un instantané synchronisé. Les requêtes du thread protocole planifient la VM aux points sûrs ; une connexion expose un contexte actif (thread 1), donc sérialisez les appels débogués. `Connect()` n’émet pas de callback et ne démarre/recharge pas le code ; en cas d’échec il ne conserve aucun callback. `Activate()` termine la configuration. `SetBreakpoints` remplace atomiquement une source et renvoie un résultat par demande dans l’ordre avec les ID fournis, sans événement doublon pour cette mise à jour. `Disconnect()` retire les breakpoints, draine les callbacks et libère l’appel arrêté selon la politique hôte. L’annulation reste une opération hôte distincte.

Les callbacks et inspection, évaluation, mutation et reprise de la VM arrêtée appartiennent au thread de contrôle de la Session. Le callback d’arrêt traite les requêtes sur ce thread. Seuls `RequestPause()` et `FExecutionControl::RequestCancel()` partagé sont inter-threads. Les vues empruntées du programme, liaisons, sources et nom d’entrée expirent au retour du callback. Copiez les métadonnées utiles ; les overlays sont la configuration courante, pas une source figée. Publiez séparément les révisions source/liaison.

Voir [Déboguer une Session existante](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/session-debugging.md#debug-disconnect-lifetime).

## Responsabilités d’implémentation

Voir [Embedding et session C++ — Responsabilités d’implémentation](../../../../../Extensions/Language/Cpp/Resources/Docs/Developer/fr-FR/session.md#session-debug-implementation).

## Inspection mémoire

`FMemoryInspection` transporte l’inspection dans les variables du débogueur. Une adresse autorisée devient `memoryReference` en chaîne décimale ; `veroMemory` fournit `objectId`, `objectAddress`, `dataAddress`, `byteLength` également en chaînes. Préservez la précision uint64 et omettez les valeurs indisponibles. Les enfants sont en lecture seule, sans cible modifiable ni fausse adresse. `variablesReference` reste un handle DAP opaque indépendant de l’identité. N’annoncez pas de lecture/écriture mémoire arbitraire.

## Traitement de compilation et métadonnées source

Les hôtes transmettent `FCompileProcessingOptions` via `CompileProcessing` dans les options de requête, `FWorkspaceOptions`, `FLspStdioRunOptions`, `FDapStdioRunOptions` ou `FDapLaunchArgs`. Les processeurs généraux sont installés avant le chargement ; la préparation du cœur est partagée avec `AnnotationProcessing`. Employez la même configuration pour analyse, complétion, navigation, indexation et débogage. Les registres doivent être gelés et chaque compilation crée ses instances. Les callbacks natifs sont fournis au démarrage du service et ne se sérialisent pas dans la configuration JSON LSP/DAP.

Le lecteur général de dépendances précède l’ancien lecteur d’annotations. Les caches revérifient les dépendances effectivement lues et ne réutilisent pas un processeur non cacheable. Appelez `SetOptions` après changement de configuration. Les intervalles d’origine de `ActiveSource` ramènent les éditions aux coordonnées originales pour navigation, jetons, diagnostics et débogage. Les déclarations générées ont des sources distinctes en lecture seule. Survol et complétion utilisent les descriptions du cœur avec sa politique de conservation des commentaires ; les signatures de réflexion des métadonnées suivent également le cœur.

## Extensions de compilation script

`ScriptCompileExtensions` sur ces mêmes options définit racines, configuration et politique d’exécution. `import compiler "extension.vs";` suit les règles de préambule du cœur ; complétion de chemin, navigation et liens le reconnaissent sans exécuter ses processeurs.

Pour éditer une extension, fournissez `GetScriptCompileServiceCatalog()` via `Bindings` ou exportez le catalogue dans `bindingCatalogPaths`. Il décrit `std::compiler` avec des callbacks réservés à la compilation ; il n’accorde aucun service d’exécution au code ordinaire.

Les instantanés sémantiques vérifient les ressources lues par les callbacks, y compris celles initialement absentes. Révisions des overlays et code d’extension invalident les résultats. Avec un lecteur personnalisé, l’analyse script est refaite : le lecteur combiné d’une tâche fermée ne peut pas être conservé comme callback réutilisable. Les réponses full/delta comparent encore les jetons publiés. Appelez `SetOptions` après modification de la configuration.

## Éditions du protocole et coordonnées

Le formatage de plage ou à la frappe conserve les fragments de commentaires multilignes et de chaînes brutes ; formatez le littéral complet ou le document pour modifier son environnement.

Avec la surveillance dynamique, LSP surveille aussi les catalogues de liaisons. Un événement force leur rechargement même si taille et horodatage sont identiques ; sans événement, ces deux attributs sont vérifiés avant les requêtes. Un catalogue invalide conserve le dernier instantané valide et publie son état. Les changements de configuration remplacent les surveillances. LSP/DAP normalisent et dédupliquent les chemins selon l’identité des modules, y compris la casse sous Windows. DAP valide catalogue et lancement avant remplacement ; un refus de validation préserve programme et indices natifs.

Une annulation ou exception pendant le rechargement debug efface ancien bytecode, liaisons, sources et points d’arrêt avant propagation ; un chargement suivant reste possible. DAP renvoie une réponse d’échec sans fermer la connexion.

DAP expose le thread 1 uniquement. `stackTrace` respecte `startFrame` et `levels` ; zéro demande tous les frames restants, `totalFrames` donne le total et les identités restent stables pendant l’arrêt. `FDebugStackFrame.Column` compte les octets UTF-8 physiques ; `FDapStackFrame.Column` compte les unités UTF-16 à partir de 1, puis le transport applique la préférence zéro/un du client. Les corrections des sources importées/générées sont capturées avant suppression des métadonnées et ne changent pas après une édition disque. Les domaines live fournissent des colonnes DAP UTF-16.

## Environnements de modules résolus

`ModuleEnvironment` accepte l’immuable `FModuleEnvironment` du cœur dans analyse, requêtes, workspace et débogage, ainsi que `FLspStdioRunOptions` / `FDapStdioRunOptions`. Publiez un environnement complet ; un catalogue live incompatible produit un diagnostic. Un nouvel environnement invalide les index, et les dépendances des processeurs sont revérifiées même sans édition.

Le résolveur commun gère imports, overlays, propriété des sources, visibilité et ressources. Les candidats hôtes suivent leurs origines et les types accessibles par les signatures publiques. Si l’analyse ne restaure pas la portée, la complétion de secours n’énumère pas tout le catalogue hôte. Le catalogue standard inclut `std::i18n` ; les propriétés natives sans setter gardent `property` / `readonly`.

LSP annonce `capabilities.experimental.veroscriptModuleEnvironment` avec empreintes de compilation/exécution et `moduleSourceMethod: "veroscript/moduleSource"`. Une requête `{ "uri": "veroscript-module:/..." }` renvoie `uri`, `languageId`, `text`, `readOnly` et l’empreinte de compilation, ou `null` si inaccessible. Respectez la limite de trame ; le client fournit un fournisseur de contenu pour ce schéma. Les fichiers ordinaires gardent leurs URI de fichier. Une réexportation autorisée rend sa cible lisible sans révéler tous les modules transitifs. Ces documents restent en lecture seule, y compris pour rename.

Le débogueur conserve l’environnement et vérifie ses dépendances externes. `TryGetSource` et DAP `sourceReference` renvoient les sources compilées figées, y compris le tampon principal portant un label de fichier physique. Une édition disque ne les remplace pas ; reload/clear invalide les anciennes références. Les domaines live gardent leur callback source. Pour une identité de chemin répétée, le dernier overlay gagne et le tampon éditeur remplace l’instantané partout.

Les contrôles d’accès conservent l’identité qualifiée : des types homonymes dans deux espaces de noms n’accordent pas l’accès privé/protégé/friend. Un nom court doit être non ambigu et identifier la même déclaration ; une déclaration globale, y compris `::Same`, garde sa priorité.

L’organisation des imports ne supprime que les imports globaux entièrement dupliqués dans un bloc ininterrompu, confirmés par le prétraitement courant et les tampons. Elle conserve lignes réécrites par macros, branches inactives, dépendances incertaines, littéraux et instructions supplémentaires. Directives, commentaires, autres déclarations et limites de portée terminent un bloc. Continuations physiques ou erreurs lexicales empêchent l’édition ; les fins de ligne originales sont conservées.

## Attache au domaine fourni par l’hôte

Les règles précédentes de chargement `Attach` concernent le mode `program` ou l’absence de `attachMode`. Le mode persistant utilise un `IVeroScriptLiveDebugDomain` fourni aux surcharges de `VeroScriptDapStdio::Run` / `VeroScriptDapTcp::Run`, puis `attachMode: "domain"` et `domainId`. Exigez `initialize.body.veroscriptSupportsDomainAttach == true` avant la requête ; si absent ou faux, ne revenez pas à launch/program. Les champs invalides échouent sans chargement. La réponse attach fournit identités, révisions et capacités propres à ce domaine ; `configurationDone` active les points d’arrêt sans lancer de programme et la connexion reste ouverte pour les appels hôtes suivants. Gardez le domaine vivant et sérialisez les contextes débogués (thread 1). Terminer n’est permis que si la réponse annonce `veroscriptCanTerminate: true` ; sinon la requête échoue sans fermer la connexion.

Avant la déconnexion, l’adaptateur attend le retour des contrôles pause/annulation déjà transmis. Les nouveaux contrôles échouent pendant la fermeture ; les anciens ne visent jamais la connexion suivante. La transmission ne doit attendre ni la déconnexion ni du travail destiné au thread qui la draine. Déconnexion hôte et drainage des callbacks s’exécutent hors du mutex de contrôle. L’hôte sérialise toujours attach, reprise, déconnexion et accès à l’état arrêté.

Les complétions de méthodes générées exposent `FCompletionItem::bOverrideMember`, `bImplementsInterfaceMember`, `Documentation`, `InsertText` et `bInsertTextSnippet`. Une exigence d’interface pure diffère d’un override hérité, y compris une méthode virtuelle avec corps par défaut. La documentation suit la locale active. Les surcharges de même nom sont groupées ; LSP utilise des éditions explicites, et du texte ordinaire si le client ne prend pas en charge les snippets.

## Métadonnées JIT du protocole

`Initialize` renvoie les métadonnées du protocole et ses indicateurs de capacité. `jitCapability` décrit le contrat de service JIT de Core (`backend-services`), pas le backend sélectionné ni ses capacités de code natif ; l’hôte les obtient avec `IJitBackendFactory::GetDescription()`. L’hôte peut fournir une fabrique ou appeler `SetJitBackendPath` avec un chemin UTF-8 explicite vers une bibliothèque ou un dossier. Le chargeur contrôle l’ABI et maintient la bibliothèque en vie tant que ses fabriques ou artefacts sont conservés. Il ne cherche pas dans `PATH` ni dans des paramètres d’environnement implicites. Tooling ne charge pas de module JIT : le champ DAP `backend` ne fait qu’enregistrer une politique, et le débogage attaché utilise la VM.

## Configuration de la VM dans les adaptateurs

Conservez la propagation de `FDebugLoadOptions::VmBackendFactory` / `VmBackendPath` vers le chargement. `FDapLaunchArgs` et les options stdio/TCP portent les choix fournis par l’hôte ; la requête DAP ne peut pas transporter une fabrique native. Fabrique et chemin sont exclusifs. Le débogage s’exécute dans la VM et le champ `backend` n’en charge aucune. La sélection devient immuable à l’engagement de l’exécution et doit rester compatible avec le domaine conservé. Les extensions de compilation peuvent avoir une sélection explicite distincte dans `ScriptCompileExtensions` ; à défaut, le chargement de débogage transmet la sienne.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/workspace-and-debug.md) · [fr-FR](workspace-and-debug.md) · [ja-JP](../ja-JP/workspace-and-debug.md) · [ko-KR](../ko-KR/workspace-and-debug.md) · [zh-CN](../zh-CN/workspace-and-debug.md) · [zh-TW](../zh-TW/workspace-and-debug.md)
