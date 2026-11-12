# Architecture et protocoles Tooling

`VeroScriptTooling` est le language-service layer commun aux command-line, LSP, DAP et autres hosts. Il consomme les public language-core contracts et sépare semantic query de protocol transport.

LSP et DAP partagent la valeur JSON, l’analyseur et le sérialiseur privés dans `Private/Internal/VeroScriptToolingJson.h` et `.cpp`. L’échappement des chaînes utilise `Shared/Private/VeroScriptJsonString.h`. Le cadrage, la corrélation des requêtes, l’ordonnancement, l’annulation et les champs propres au protocole restent dans leurs adaptateurs. Les modifications doivent préserver l’ordre déterministe des objets, la conversion numérique, la limite de profondeur existante et les messages d’erreur.

## Flux de données en couches

Core possède parsing, semantic analysis, module loading, runtime metadata, diagnostic et debugger semantics. `VeroScriptWorkspace` fournit project context, document overlay, source/import resolution, preprocessor context, binding catalog et cross-file state. Les public feature service query une source/workspace et renvoient stable result types ; les adapters LSP/DAP les sérialisent. Un service ne lit pas editor state ni n’écrit source file, et adapter ne reconstruit pas un résultat sémantique déjà fourni publiquement.

## Familles de fonctionnalités publiques

`VeroScriptAnalyzer` donne diagnostic workspace-aware, groupé/publication par URI avec empty list pour clear. `VeroScriptOutline`, `VeroScriptNavigation`, `VeroScriptHover`, `VeroScriptCallHelp`, `VeroScriptComplete`, `VeroScriptFindReferences`, `VeroScriptDocumentHighlight`, `VeroScriptRename`, `VeroScriptFormat` (`FFormatOptions`), `VeroScriptWorkspaceSymbols` couvrent structure, lookup, completion, call, reference, edit, format et recherche.

Les espaces de noms confirmés, y compris `std::`, sont traités sémantiquement. Le catalogue des liaisons hôte et les fonctions intégrées souples participent à la recherche ; les symboles propres à l’hôte utilisent un URI virtuel dédié si le protocole exige une URI. La complétion regroupe dans un seul élément les surcharges appelables de même nom visible et affiche chaque signature source sur une ligne distincte du détail. En contexte d’appel, cet élément utilise un arrêt d’argument vide au lieu de déduire les paramètres d’une surcharge. Un élément appelable sans surcharge ou un type générique peut inclure un snippet uniquement si le client annonce `textDocument.completion.completionItem.snippetSupport`. Sinon, l’adaptateur LSP envoie du texte brut. Le snippet d'un élément appelable est omis si un `(` suit déjà la cible, même avec des espaces ou des commentaires entre les deux, afin d'éviter de dupliquer la syntaxe d'appel. Le renommage valide la cible puis résout de nouveau chaque référence ; les espaces de noms de la bibliothèque standard ne sont pas renommables. Le formatage renvoie des éditions sans les appliquer.

`FCompletionItem::MatchQuality` est la clé principale de l'ordre textuel ;
`SortPriority` ne départage que les mêmes qualités. Les adapters préservent cet
ordre dans le `sortText` LSP et ne posent `preselect` que pour une correspondance
exacte, exacte sans casse ou par préfixe sans ambiguïté meilleure. Les mots
primitifs sont des entrées `Keyword` intégrées, tandis que les conteneurs sont
des entrées `TypeAlias`/`type`. Leur détail est localisé, l'identifiant de type
reste stable, aucune définition source n'existe et le renommage est refusé.

Pour une complétion dans un namespace qualifié, `label` et le texte inséré
(`insertText`, ainsi que `textEdit.newText` si une plage est remplacée) conservent
le nom final ; un snippet appelable ou générique est inclus s'il est disponible et si le client annonce
`textDocument.completion.completionItem.snippetSupport`, sinon le texte inséré est
le nom seul. `labelDetails.description` contient le namespace. Le champ facultatif `$valencePresentation` fournit à Valence le
texte affiché et ses plages sémantiques UTF-16 ; les autres clients LSP peuvent
l'ignorer. Ces données d'affichage ne changent ni le tri ni l'insertion.

La complétion d'un segment de namespace est distincte de celle d'un membre.
Dans un chemin d'expression ou d'instruction, un élément de namespace conserve
son nom final dans `label` et `filterText` et ajoute `::` au texte inséré
uniquement si aucun séparateur ne suit la plage remplacée. Après
`using namespace`, seul le nom est inséré. Si le client prend en charge les
snippets, les fonctions hôtes de formatage variadiques comme `print` utilisent,
dans un contexte d'appel, un point de tabulation vide placé entre parenthèses
(`print(${0})`), plutôt que des placeholders déduits de signatures fusionnées.

Pour les semantic tokens, l'adapter LSP combine les plages résolues avec une
base lexer pour les mots du langage/de type, directives, macros et
littéraux/commentaires. Cette base reste disponible avec une entrée incomplète
et les chevauchements sont supprimés avant l'encodage du flux LSP.

## Contrat protocole LSP

`VeroScriptLspStdio` gère le cadrage `Content-Length`, la corrélation JSON-RPC,
la distribution des requêtes, leur annulation, la publication des diagnostics
et le cycle de vie. Il annonce les méthodes suivantes :
`textDocument/completion`, `completionItem/resolve`, `textDocument/hover`,
`textDocument/definition`, `textDocument/declaration`,
`textDocument/typeDefinition`, `textDocument/implementation`,
`textDocument/prepareTypeHierarchy`, `typeHierarchy/supertypes`,
`typeHierarchy/subtypes`, `textDocument/documentSymbol`,
`textDocument/selectionRange`, `textDocument/documentLink`,
`textDocument/foldingRange`, `textDocument/semanticTokens/full`,
`textDocument/semanticTokens/full/delta`, `textDocument/signatureHelp`,
`textDocument/references`, `textDocument/documentHighlight`,
`textDocument/prepareRename`, `textDocument/rename`,
`textDocument/formatting`, `textDocument/rangeFormatting`,
`textDocument/onTypeFormatting`, `textDocument/codeAction` (types
`source.formatDocument`, `source.organizeImports` et `refactor.rewrite`), `workspace/symbol` et
`workspaceSymbol/resolve`.

Notifications : `initialize`, `initialized`, `textDocument/didOpen`, `textDocument/didChange`, `textDocument/didSave`, `textDocument/didClose`, `shutdown`, `exit`, `workspace/didChangeConfiguration`, plus folders/watched files et `$/cancelRequest` qui retourne `-32800`. Triggers completion `.`/`:`/`@`, signature `(`/`,`. LSP est zero-based UTF-16, C++ one-based byte ; adapter convertit une seule fois, et version non nouvelle est ignorée.

## Configuration et localisation

`veroscript` setting provient de `initializationOptions` et `workspace/didChangeConfiguration.settings` : import/implicit paths, `stdlibPath`, define/macro, policy et `bindingCatalogPaths`. Catalog merge en ordre, duplicate reject, reload au prochain non-lifecycle request puis reanalyze open document ; catalog décrit les host symbol, sans callback runtime.

`initialize.locale` est BCP 47 ; unsupported conserve catalogue environnement, absent fallback `en-US`. Diagnostic identifier/argument, binding identifier, type/member, protocol field, URI/path/source ne sont jamais traduits.

## Invariants

Préservez source position/range, document identity/version du result, cancellation/correlation/wire order, diagnostic ID/argument stables, différence entre empty result/protocol failure et règle que tooling produit edit sans apply. Voir la [référence du serveur de langage VeroScript](../../User/fr-FR/lsp.md).

## Limite Debug et DAP

`VeroScriptDebugSession` possède compilation, breakpoint, step, VM inspection, evaluation, variable write, output, JIT/backend ; `VeroScriptDapAdapter` le mappe en DAP et n’ajoute pas transport ni second engine. `FDapLaunchArgs` transporte source/path, overlay, binding, policy, stop-on-entry, call depth, backend, args, roots, environment, cwd et stdin ; debug attach force `Vm` même pour `Mixed`/`Jit`. `Initialize` précède `ConfigurationDone`, qui possède run synchronously ; stopped callback seul permet opérations debug, `Disconnect` resume/clear après run ; transport émet `initialized` après response, non dans `VeroScriptDapAdapter::Initialize()`.

`VeroScriptDebugSession::BindBreakpoint` possède la résolution physical-line/preprocessor-map et n’inscrit que des labels de source compilée exacts. Pour une demande invalide, ou non exécutable sans location admissible, il retourne un résultat non inscrit avec un texte localisé. L’adapter préserve l’ID DAP externe, expose la ligne réellement liée et publie après le rebind d’un breakpoint défini avant lancement un événement `Breakpoint`/wire `breakpoint` avec `reason: "changed"`.

### Contrat transport DAP

`VeroScriptDapStdio::Run` lit/écrit DAP Content-Length avec streams caller. `FDapStdioRunOptions` fournit live binding, catalog, args, stdlib, environment, cwd et input ; live binding emprunté jusqu’à `Run` prime catalog. Entrées : `program`/`source`, `fileLabel`, `workspace`, `cwd`/`workingDirectory`, `stopOnEntry`, `args`, `input`, `env`/`environment`, `stdlibSearchPaths`/`stdlibPath`, policy, `backend`, `maxCallDepth`. Args vont à `main(string[]
args)`; input est stdin script. Limites : 4096 args, 2048 paths, 1024 environment ; input invalide devient diagnostic localisé.

Le statut `bReadOnly` du débogueur Core passe par `FDebugVariable` et `FDapVariable` jusqu’à `presentationHint.attributes = ["readOnly"]` dans DAP. Les charges utiles immuables des variants imbriqués conservent ce statut, y compris dans les expressions surveillées. Core reste responsable du refus des modifications de valeurs immuables.

`VeroScriptDapTcp::Run` sert IPv4 séquentiel. `FDapTcpRunOptions::BindAddress` default `127.0.0.1`; empty/`0.0.0.0` écoute all interface. `Port == 0` est reported via `OnListening`; `MaxSessions == 1` un client, `0` unlimited, négatif un. Disconnect attach non-terminal permet client suivant, terminate arrête loop et seul host termination est success.

## Contrat workspace et requêtes LSP

`VeroScriptLspStdio::Run` analyse sur worker afin d’observer cancellation. Roots : array `workspaceFolders` a priorité même vide, sinon `rootUri`, sinon FLspStdioRunOptions::DefaultWorkspaceRoots ; ce sont index roots, non import paths. Scan seulement `.vs`, exclut .git/.verocache/build/dist/Binaries/Intermediate ; overlay open prime disk. Les features LSP comprennent completion/resolve, navigation, symbol, semantic token, formatting/action et workspace symbol, avec limite 2000 ms interactive / 5000 ms semantic, 512 completions, 2000 symbols, 5000 references, 10000 files, 2 MiB fichier, réglables par lspRequestTimeoutMs, maxCompletionItems, maxWorkspaceSymbolResults, maxReferenceResults, maxWorkspaceFiles, maxWorkspaceFileBytes et clampés sûrs.

`VeroScriptFormat` est lexical ; les options par défaut produisent quatre espaces par niveau, et `bInsertSpaces=false` permet les tabulations. Le formatage LSP respecte `options.insertSpaces` et `options.tabSize` : une tabulation par niveau si `insertSpaces=false`, sinon `tabSize` espaces par niveau (maximum 16, quatre par défaut si absent ou invalide). Le formatage par plage couvre les lignes physiques complètes et renvoie la plage de remplacement dans `FRangeFormatResult` ; un échec renvoie une édition LSP vide. L’organisation des imports ne retire que les doublons lexicaux sûrs. `VeroScriptWorkspace::WorkspaceSymbols` fusionne les documents ouverts, les overlays et les fichiers fermés ; `FWorkspaceSymbolsOptions::ShouldCancel` est emprunté pour une seule requête et n’est jamais conservé. Un résultat annulé n’est ni mis en cache ni publié. Le cache par défaut est invalidé après une modification du document ou de la configuration ; les options propres à une requête, les overlays, les racines, les limites et la fonction de vérification d’annulation n’y sont pas stockés.

## Commentaires de documentation source

Les complétions de macros préprocesseur définies dans le source attachent la documentation des commentaires `//`/`///` ou `/* ... */` contigus placés immédiatement avant la macro ; la documentation de complétion hôte vient des métadonnées de binding. `completionItem/resolve` conserve la documentation fournie et n'utilise `detail` comme Markdown que si elle est absente. Le survol extrait les commentaires contigus avant les déclarations source et formate `@param nom texte`, `@return texte` et `@returns texte` avec les libellés du catalogue actif ; les tags inconnus restent dans le commentaire. Cette convention de présentation Tooling ne modifie pas le traitement des commentaires par le compilateur.

## Planification des tokens sémantiques

Les tokens sémantiques utilisent un worker distinct. Le worker protocole ordonné possède mises à jour, état des requêtes et sortie ; les jobs possèdent leurs buffers/options/bindings et publient des instantanés immuables après validation des versions et dépendances. Full/delta réutilisent les flux inchangés ; hover, définition et completion réutilisent les AST valides. Les AST récupérés pour la coloration ne remplacent pas ceux des requêtes interactives. Les probes couvrent lexing, preprocessing, parsing, imports, symboles et encodage. Une annulation réveille le protocole ; les jobs expirés quittent la file sans attendre l’analyse active. Budgets par défaut : 2000 ms interactif, 5000 ms sémantique. `lspAnalysisTimeoutMs` sépare le budget, `lspTraceAnalysis` active les mesures. Voir les [budgets](../../User/fr-FR/lsp.md).

### Contrats des callbacks, du cache et du cycle de vie

`FSemanticTokensOptions::ShouldCancel` et `RecordStage` sont empruntés uniquement pendant l’appel synchrone et s’exécutent sur son thread d’analyse. Les captures doivent rester valides jusqu’au retour. L’annulation renvoie `bCancelled` sans tokens ; les résultats incomplets ne doivent pas être publiés ni mis en cache. Les exceptions des observateurs sont ignorées. `FSemanticTokensResult::Snapshot` possède le programme et le stockage source, mais reste un état opaque interne à Tooling, pas une API publique de cache ou de réutilisation.

La réutilisation exige la concordance du contenu/de la version, de la configuration et des dépendances. Les métadonnées disque sont relevées avant lecture des imports/packages, puis vérifiées avant publication ou réutilisation ; un relevé invalide interdit la réutilisation. Les modifications des overlays invalident aussi les imports concernés. Fermer puis rouvrir une URI efface l’état analysé, les tokens et les anciens ID de résultat, même si la version redémarre à une valeur inférieure. Les changements de configuration d’analyse invalident les tâches en cours. Des labels source identiques évitent une canonicalisation disque redondante ; les labels différents utilisent toujours la comparaison canonique des chemins.

`VeroScriptLspResponsivenessTest.py` teste le vrai processus stdio : réutilisation full/delta, hover pendant l’analyse, annulation, délais, modifications des dépendances et de configuration, fermeture/réouverture. Les tests lexer et workspace couvrent la reprise après annulation et les modifications disque entre chargement et création du snapshot. Conserver ces régressions dans les tests de leurs modules Tooling/core.

## Réutilisation locale à une requête

Les passes de références et de renommage utilisent `Internal/VeroScriptSourceQuerySession` pour conserver le programme analysé exact du fichier courant pendant les requêtes ponctuelles. Cet état reste local à la requête, vérifie les dépendances disque et ne réutilise pas les programmes récupérés ou traités par des processeurs. Les variables locales de bloc/fonction dont la déclaration appartient à la source courante n’exigent pas le balayage des autres modules ouverts homonymes. Un ensemble local déduplique les positions des tokens ; chaque graphie de chemin existant est résolue une seule fois par balayage.

Les réglages exclusivement protocole (budgets, traces, limites de résultats) conservent les instantanés sémantiques. Les changements d’analyse les invalident et invalident les jobs en cours ; les limites de découverte du workspace invalident l’index des symboles sans invalider les programmes analysés.

Sur Darwin, `Private/Internal/VeroScriptLspThread.h` crée les workers de protocole et d’analyse sémantique avec des piles pthread de 8 Mio. Les autres plateformes utilisent `std::thread`. Préservez join et propriété ; cette pile n’augmente pas les limites du parseur ou de l’analyse. Les flottants JSON utilisent le helper privé décrit dans [la conversion décimale Core](../../../../../VeroScript/Resources/Docs/Developer/fr-FR/architecture-and-pipeline.md#floating-formatting-support).

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/architecture-and-protocols.md) · [fr-FR](architecture-and-protocols.md) · [ja-JP](../ja-JP/architecture-and-protocols.md) · [ko-KR](../ko-KR/architecture-and-protocols.md) · [zh-CN](../zh-CN/architecture-and-protocols.md) · [zh-TW](../zh-TW/architecture-and-protocols.md)
