# Module Tooling

`VeroScriptTooling` est la couche publique d'analyse autour du langage
VeroScript. Elle convient aux éditeurs, serveurs de langage, vérificateurs
statiques et débogueurs.

## Surfaces publiques

| Surface | Fonction |
| --- | --- |
| Analyseur | Analyse des sources ou fichiers et diagnostics structurés. |
| Formatage | Produit un remplacement complet ; le formatage est lexical et n'étend pas les macros. |
| Complétion et aide aux appels | Complétion contextuelle, surcharges groupées, aide à la signature, génération d'implémentations héritées/d'interface, métadonnées hôte et préprocesseur. |
| Survol et navigation | Déclarations résolues, commentaires de documentation, définitions et plages sensibles aux namespaces. |
| Outline et tokens | Symboles de document et coloration fondée sur les déclarations, y compris namespaces et macros. |
| Références et renommage | Références inter-fichiers, lectures/écritures, préparation et éditions de renommage. |
| Espace de travail | Recouvrements des tampons ouverts, analyse multi-fichier et recherche de symboles. |
| Transport LSP/DAP | `VeroScriptLspStdio::Run` pour LSP ; les surfaces DAP pour le débogage. LSP inclut resolve, navigation de types, plages, liens, delta de tokens, formatage et action de formatage. |

Les complétions de méthodes générées définissent `FCompletionItem::bOverrideMember` ;
`bImplementsInterfaceMember` vaut vrai lorsqu'une méthode générée satisfait une
exigence pure d'interface ; les redéfinitions de méthodes virtuelles par défaut
sont classées comme des redéfinitions héritées. `Documentation` suit la langue Tooling active.
`InsertText` contient le squelette de méthode et `bInsertTextSnippet` indique la
présence de tabulations snippet. Les surcharges de même nom sont regroupées dans
un seul élément, avec une signature par ligne de détail. Le LSP envoie ces
candidats sous forme d'éditions de texte et utilise du texte brut si le client
ne prend pas en charge les snippets.

Les positions de l’API C++ sont indexées à partir de 1. Les documents ouverts ont priorité sur les fichiers disque. `FWorkspaceOptions` porte les chemins d’import, racines de bibliothèque standard, définitions, macros prédéfinies, imports implicites, accès à l’ensemble du projet et métadonnées de bindings hôtes.

Les hôtes natifs peuvent définir `AnnotationProcessing` dans les options de requête ou d’espace de travail, `FLspStdioRunOptions` et `FDapStdioRunOptions`/`FDapLaunchArgs`. Utilisez le même registre de processeurs figé que pour la compilation. Les fabriques LSP et lecteurs de dépendances doivent supporter les appels concurrents des workers de protocole et d’analyse. Le contenu et l’existence des dépendances sont revérifiés avant réutilisation ; un résultat issu d’un processeur `Cacheable=false` reste disponible mais n’est pas mis en cache.

`WorkspaceRoots` définit séparément les racines utilisées pour indexer les
fichiers `.vs` fermés ; il ne transforme pas ces répertoires en chemins d'import.
`MaxWorkspaceFiles` et `MaxWorkspaceFileBytes` limitent respectivement le nombre
et la taille des fichiers parcourus. Le callback `ShouldCancel` permet d'annuler
coopérativement un scan long ; le résultat marqué `FWorkspaceSymbolsResult::bCancelled` ne doit pas être
mis en cache ni publié comme un nouvel index. `ImplementationDocument` trouve
les types dérivés et les implémentations de membres virtuels ou d'interface.
La collecte des symboles élimine les identités de déclaration en double : une
même déclaration observée par plusieurs imports n'est renvoyée qu'une fois.
`FormatRangeDocument` étend une sélection aux lignes physiques complètes.
La surcharge de `ImplementationDocument` qui accepte `FWorkspaceSymbolsOptions`
réutilise les mêmes limites et le même callback d'annulation ; si la recherche
est annulée, `FNavigationResult::bCancelled` est défini et les définitions
partielles ne doivent pas être publiées.
`FormatDocument` et `FormatRangeDocument` acceptent aussi une largeur
d'indentation et un choix espaces/tabulations pour les appelants de la bibliothèque ;
une largeur nulle conserve la valeur par défaut de quatre espaces, et
`bInsertSpaces=false` insère une tabulation par niveau. Le formateur sans état
accepte le même choix de style.
Le point d'entrée sans état `VeroScriptFormat::FormatRangeSource` applique le
même contrat de plage couvrant des lignes physiques entières sans espace de
travail ouvert.

`VeroScriptWorkspace::ImplementationDocument` trouve aussi les corps de fonctions ou méthodes définis séparément ou générés par annotation ; la navigation vers la déclaration reste distincte. `SourceImplementation` possède le nom du fichier du corps et sa position physique ; un nom vide indique qu’aucun corps séparé n’a été associé. La recherche directe d’une fonction libre ou d’une méthode non virtuelle ne parcourt pas tout l’espace de travail.

Le formateur utilise des accolades Allman pour les blocs. `else`, `else if`, `catch`, `finally`
et le `while` final de `do…while` commencent sur de nouvelles lignes alignées sur l’accolade fermante précédente.
Les initialiseurs conservent leur syntaxe ; les littéraux et directives sont préservés sans expansion.

Le formatage de plage ne renvoie aucune modification si une limite se trouve dans un commentaire de bloc,
une chaîne brute ou une directive multiligne. Sélectionnez la directive entière ou le code ordinaire suivant
pour poursuivre ; le texte des macros et leurs continuations sont préservés.

Les types sémantiques proviennent du programme analysé et des métadonnées hôte,
pas de la casse. Les namespaces utilisateur et l'arbre réservé `std` restent
cohérents dans la complétion, le survol, la navigation, les références,
le renommage, l'outline et les tokens. Les éléments de complétion peuvent
contenir des snippets LSP avec des placeholders d'arguments ou de types.
L'adaptateur LSP n'envoie le texte au format snippet que si le client annonce
`textDocument.completion.completionItem.snippetSupport` ; sinon, il envoie du
texte brut.

Les éléments de complétion d'un segment de namespace se distinguent des éléments
de membre : dans un chemin d'expression ou d'instruction, ils ajoutent `::` à
`InsertText` si aucun séparateur ne suit la plage de remplacement. Si `::` est
déjà présent ou si la cible est une directive `using namespace`, seul le nom
final est inséré.

Pour un membre d'un namespace qualifié, `FCompletionItem::Label` et
`InsertText` conservent le nom final et son éventuel snippet, tandis que
`LabelDescription` porte séparément le namespace qualifié. En LSP, il s'agit du
champ standard `labelDetails.description` ; le texte snippet n'est envoyé qu'aux
clients annonçant `textDocument.completion.completionItem.snippetSupport`, sinon
le texte brut est utilisé. Le serveur peut aussi envoyer
`$valencePresentation` pour le label/la déclaration rendus par Valence et les
plages sémantiques UTF-16 ; les autres clients LSP peuvent ignorer cette
extension. Ces champs d'affichage ne changent jamais le filtrage, le tri ni le
texte inséré.

La complétion classe d'abord la qualité de correspondance textuelle, puis la
priorité contextuelle : correspondance exacte, exacte sans casse, préfixe,
frontière de mot ou CamelCase, contenu, puis sous-séquence. `FCompletionItem::MatchQuality`
est la clé principale et `SortPriority` départage les égalités ; les intégrations
doivent donc préserver l'ordre renvoyé, que le LSP reproduit dans `sortText`.
Le LSP n'ajoute `preselect` au premier élément que si son niveau exact/exact sans
casse/préfixe est strictement meilleur que tous les autres. Les mots de types
primitifs et conteneurs sont des types intégrés : les écritures primitives restent
des entrées `keyword` dans la complétion, le survol et les tokens sémantiques,
tandis que les conteneurs sont des entrées `type`/alias de type. Aucun n'a de
position source et le renommage est refusé. Leur texte descriptif suit la locale
Tooling active ; les identifiants de type et champs de protocole ne changent pas.

L'adaptateur LSP fournit aussi `completionItem/resolve`, type-definition,
implementation, selection ranges, document links, folding ranges, tokens
complets et delta, formatage de plage étendu aux lignes complètes, formatage
on-type, `source.formatDocument`, `source.organizeImports` et les actions
`refactor.rewrite` qui génèrent des implémentations de méthodes. Les notifications
d'annulation et de fichiers surveillés invalident également l'espace de travail.
`VeroScriptLspStdio::Run` accepte `FLspStdioRunOptions`, notamment
`DefaultWorkspaceRoots` pour l'indexation des fichiers fermés lorsque le client
ne fournit ni `workspaceFolders` ni `rootUri` ; les racines fournies par le client
remplacent cette valeur par défaut.

Pour le protocole, les capacités, l'annulation et la configuration, voir
[Serveur de langage](lsp.md). Pour DAP, voir [Débogage](debugging.md).

## Protocole DAP

Lancez l’adaptateur en mode stdio ou TCP avec la CLI :

```powershell
veroscript --debug hello.vs
veroscript --debug --dap-port=4711 hello.vs
```

`--dap-port` exige `--debug` et un port de 1 à 65535. TCP écoute sur `127.0.0.1` par défaut ; `--dap-bind=<adresse>` modifie l’adresse d’écoute et exige le mode TCP. DAP `launch` ou `attach` fournit le programme et ses arguments. Le débogage utilise la VM ; ses performances ne représentent pas une exécution mixed ou JIT.

Les points d'arrêt de ligne source passent par `setBreakpoints`. Les points
d'arrêt d'exception et de fonction ne sont pas pris en charge : une requête
`setExceptionBreakpoints` vide sert seulement à effacer l'état de la séquence
de lancement standard ; des filtres non vides et toute requête
`setFunctionBreakpoints` échouent. Un client ne doit pas afficher ces catégories
comme actives.

Un point source défini avant `launch` ou `attach` est pending et non vérifié.
Après le chargement du code, l'adaptateur envoie l'événement standard
`breakpoint` avec `reason: "changed"` et le même ID DAP. Il ne vérifie qu'une
ligne appartenant à une unique source compilée, active après prétraitement et
avec une location exécutable ; il ne cherche jamais un basename correspondant.
Le client doit utiliser la ligne physique renvoyée, qui peut avancer dans une
fonction jusqu'à la prochaine ligne exécutable. Voir [Débogage](debugging.md)
pour les règles de liaison complètes.

Pour une intégration dans le processus, `VeroScriptDapAdapter` suit le cycle
`Initialize` → `Launch` ou `Attach` → `SetBreakpoints` → `ConfigurationDone`.
`ConfigurationDone` est synchrone dans l'adaptateur intégré ; les hôtes filaires
doivent l'appeler sur un worker d'exécution afin que le thread du protocole puisse
continuer à traiter `pause`, `terminate` et les requêtes pendant un arrêt.
`Attach` charge le programme avec le même chargeur que `Launch` et ne s'attache
pas à une VM déjà en cours d'exécution. `Pause` et `Terminate` sont des contrôles
coopératifs sûrs entre threads ; `IsStopped()` et `IsRunning()` indiquent l'état.

`VeroScriptDapStdio::Run` accepte `FDapStdioRunOptions`, notamment les valeurs par
défaut `DefaultProgram`, `DefaultScriptArgs`, `DefaultStdlibSearchPaths`,
`DefaultEnvironment`, `DefaultWorkingDirectory` et `DefaultInput`. Un hôte intégré
peut fournir `LiveBindings` pour exécuter ses fonctions hôte enregistrées ; le
catalogue non possédé doit rester valide pendant tout le débogage. Sans bindings
actifs, `bindingCatalogPaths` fournit les métadonnées et stubs nécessaires à
l'analyse et au lancement.

`VeroScriptDapTcp::Run` accepte `FDapTcpRunOptions`. `MaxSessions=0` continue
d'accepter les clients jusqu'à l'arrêt de l'hôte, `Port=0` choisit un port
éphémère signalé par `OnListening` et `BindAddress` devrait normalement rester
à `127.0.0.1`. `bAnnounceListen=false` désactive le message de disponibilité par
défaut sur le flux d'erreur. L'hôte stdio renvoie `0` pour une session normale,
`1` pour une erreur de protocole ou d'E/S et `2` lorsque le client demande l'arrêt
de l'hôte.

### Sessions de débogage indépendantes du protocole

`VeroScriptDebugSession` est la couche C++ réutilisable située sous
l'adaptateur DAP. Utilisez-la lorsqu'un hôte ou un adaptateur personnalisé doit
gérer le chargement, les points d'arrêt, les rappels d'arrêt synchrones,
l'inspection et les résultats d'exécution sans implémenter le protocole DAP.
`LoadSource` et `LoadFile` acceptent les mêmes chemins d'importation, définitions,
macros prédéfinies, superpositions de sources, catalogue de liaisons facultatif,
chemins d'importation implicites, politiques d'importation et racines de
bibliothèque standard que la couche workspace.

`BindBreakpoint` est le chemin strict des points d'arrêt source pour un
adaptateur. Il résout une ligne physique one-based dans exactement une source
compilée et retourne `FBreakpointBinding` : un `Id` non nul et la `Line`
physique réellement liée, ou un `Message` localisé lorsqu'il ne peut pas lier le
point. Une ligne dans une fonction peut avancer jusqu'à la prochaine ligne
exécutable ; l'appelant doit utiliser la ligne renvoyée plutôt que supposer celle
qui était demandée.

Configurez les arguments, l'environnement, le répertoire de travail, l'entrée,
la profondeur d'appel maximale, le backend demandé ou la politique JIT avant
`Run`. `FDebugRunResult` contient `bSuccess`, les `Diagnostics` structurés,
`Output` et `ErrorOutput` séparés, ainsi que `ExitCode`. Le débogueur force
néanmoins le backend effectif à `Vm` ; le backend demandé et la politique JIT
restent configurés, et les statistiques JIT enregistrent principalement cette
invalidation.

`SetStopHandler` est appelé de manière synchrone pendant la pause. Le gestionnaire
peut utiliser `GetStackTrace`, `GetLocals`, `GetChildren`, `Evaluate`, `SetLocal`
et `SetChild`, puis choisir `Continue`, `StepIn`, `StepOver` ou `StepOut` avec
`SetRunMode`. `RequestPause()` et `RequestCancel()` sont des contrôles
coopératifs thread-safe pour un hôte asynchrone. Les autres configurations et
les appels à `Run` doivent rester sur le thread de contrôle ; gardez la session
valide jusqu'au retour de l'opération demandée.

## Limite publique et localisation

Utilisez les en-têtes publics distribués avec le SDK ; les fichiers internes ne
font pas partie de l'intégration prise en charge. Tooling partage les catalogues
de localisation du CLI et du runtime. Les codes de diagnostic et les arguments
restent stables entre les langues ; seule la présentation change. Voir
[Localisation des diagnostics](../../../../../VeroScript/Resources/Docs/User/fr-FR/localization.md).

## Couverture actuelle de l’éditeur sensible à la syntaxe

La complétion, le plan, le survol, l’aide de signature et les jetons sémantiques reconnaissent les
groupes de déclarations de même type, `do`/`while`, les clauses `for` classiques avec listes
d’expressions et les littéraux `char` signés sur 8 bits. Un initialiseur est résolu avant l’introduction
de sa propre déclaration ; un nom masqué ou une variable locale de lambda imbriquée ne fuit pas vers
l’initialiseur de déclaration suivant ni vers une expression d’étape de `for`. Une chaîne brute sur
plusieurs lignes reçoit un jeton de chaîne par ligne physique, et un texte ressemblant à un marqueur de
commentaire à l’intérieur de ce littéral n’est jamais classé comme commentaire.

## Observateurs d’analyse du workspace

VeroScriptWorkspace::AnalyzeAll(OnDocument) appelle l’observateur de façon synchrone une fois par document avec un chemin et un résultat empruntés. Copiez les données à conserver. L’observateur ne doit pas ouvrir, fermer ou modifier les documents de ce workspace pendant l’itération.

## Sélection VM pour l’analyse et le débogage intégrés

Un hôte Tooling direct sélectionne explicitement l’interpréteur. Utilisez `VeroScriptDebugSession::SetVmBackendFactory`/`SetVmBackendPath` avant l’exécution, ou `VmBackendFactory`/`VmBackendPath` dans `FDebugLoadOptions` ou `FDapStdioRunOptions`. Fabrique et chemin sont exclusifs. Le choix est figé à l’engagement de l’exécution ; effacer/recharger conserve le domaine sans autoriser un remplacement. Un échec conserve le choix précédent. Le lanceur Runtime correspondant fournit son backend ; lier Tooling seul ne le fait pas.

Les requêtes exécutant des extensions de compilation utilisent `ScriptCompileExtensions`, dont `VmBackendFactory` ou `VmBackendPath`, dans les options d’analyse/espace de travail. Un chargement de débogage hérite de son interpréteur sauf choix explicite pour ces extensions. Analyse syntaxique et analyse ordinaire sans exécution d’extensions ne nécessitent pas de VM. Conservez backend et ressources propriétaires pendant l’opération.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
