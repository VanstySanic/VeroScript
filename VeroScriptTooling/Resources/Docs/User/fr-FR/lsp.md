# Serveur de langage VeroScript

`veroscript-lsp` est le serveur de langage VeroScript. Il communique sur
l'entrée et la sortie standard avec des messages JSON-RPC encadrés par
`Content-Length`. Lancez le serveur appartenant à la même distribution
VeroScript que le projet.

Le serveur utilise directement le compilateur et le moteur Tooling. Les règles
du langage, les imports, le préprocesseur, les métadonnées de bindings hôte et
le catalogue des diagnostics sont donc partagés avec le CLI.

Le survol renvoie du Markdown structuré : titre du symbole, signatures VeroScript
dans un bloc de code avec une ligne par surcharge, documentation et emplacement
de définition. Les résumés de survol du Tooling natif et du CLI restent en texte brut.

## Cycle de vie et transport

L'ordre normal est le suivant : `initialize`, `initialized`, les notifications
de documents et de l'espace de travail, les requêtes, puis `shutdown` et enfin
`exit`. Attendez la réponse de `shutdown` avant d'envoyer `exit`. Quitter sans
shutdown réussi produit un résultat de processus non nul.

La notification `exit` annule les analyses sémantiques en attente ou en cours afin que l’arrêt du processus n’attende pas leur échéance.

Le serveur continue à lire les messages pendant l'exécution d'une requête, puis
applique les mises à jour des documents dans l'ordre de réception. Le client peut
envoyer `$/cancelRequest`; une requête annulée renvoie le code JSON-RPC
`-32800`. Les requêtes ont aussi une échéance serveur bornée ; les analyses de
symboles des fichiers fermés vérifient l'annulation pendant l'énumération et
le parsing. La file d'entrée est bornée et applique une contre-pression.

Si le client annonce l'enregistrement dynamique de `workspace/didChangeWatchedFiles`,
le serveur envoie `client/registerCapability` après `initialized` et demande de
surveiller `**/*.vs` et `**/*.[vV][sS][lL][iI][bB]`. Les clients qui ne prennent pas en charge
l'enregistrement dynamique peuvent configurer eux-mêmes ces motifs et envoyer
explicitement les notifications `workspace/didChangeWatchedFiles`.

`Content-Length` compte les octets UTF-8. Les en-têtes et le corps sont séparés
par une ligne vide CRLF. Les erreurs de tramage ou de JSON sont écrites sur la
sortie d'erreur.

Les URI de fichiers suivent les règles LSP standard. Une URI UNC telle que
`file://server/share/module.vs` reste un chemin réseau et est conservée sans
perdre son hôte ni son partage.

## Analyse sémantique et budgets

Les mises à jour de documents sont appliquées dans l’ordre reçu. Les tokens sémantiques sont analysés sur un worker distinct avec ses propres instantanés de documents et bindings, permettant au survol et à la complétion de répondre pendant la coloration. Les instantanés analysés valides et les flux complets sont réutilisés si le document est inchangé. Les modifications du document ou des dépendances invalident les résultats concernés ; les réglages d’analyse, fichiers surveillés et bindings déclenchent une nouvelle analyse. Les seuls changements de budgets, traces ou limites de résultats conservent les instantanés sémantiques ; les limites de découverte du workspace actualisent la recherche de symboles sans effacer les programmes analysés. Dates de modification et tailles disque sont vérifiées avant réutilisation. Une analyse remplacée renvoie `-32801` et ne publie aucun token périmé.

Fermer puis rouvrir la même URI commence un nouveau cycle de vie du document : les anciens ID de résultat des tokens sémantiques sont supprimés, même si la version redémarre. Une requête delta avec un ancien ID reçoit les données complètes ; utiliser le nouveau `resultId` pour les requêtes suivantes.

Les budgets commencent à la prise en charge par le worker protocole. L’attente dans la file d’analyse compte, celle dans la file d’entrée ne compte pas. Une annulation renvoie `-32800`, un dépassement `-32001`. Les requêtes expirées en attente sont libérées sans attendre l’analyse active. Par compatibilité, `lspRequestTimeoutMs` seul règle aussi le budget d’analyse ; renseignez `lspAnalysisTimeoutMs` pour les séparer. La trace détaille `lex`, `preprocess`, `parse`, la résolution des imports (`imports`), les symboles, les requêtes, l’encodage et la file. `load` inclut ses étapes imbriquées ; leurs durées ne doivent pas être additionnées.

Les délais du client doivent aussi tenir compte de l’attente dans la file protocole et des mises à jour/diagnostics des documents. Ces budgets d’analyse coopératifs ne garantissent pas une durée maximale aller-retour.

## Initialisation et réglages

`initialize.locale` est une préférence BCP 47 à définir avant l'ouverture des
documents. Les réglages sont placés sous `initializationOptions.veroscript` et
peuvent être mis à jour sous le même objet dans
`workspace/didChangeConfiguration` :

```json
{
	"locale": "fr-FR",
	"initializationOptions": {
		"veroscript": {
			"importPaths": ["src", "shared"],
			"stdlibPath": "stdlib",
			"defines": ["FEATURE_SCORE=1"],
			"implicitImportPaths": [],
			"allowCircularImports": true,
			"allowProjectWideAccess": false,
			"lspRequestTimeoutMs": 2000,
			"lspAnalysisTimeoutMs": 5000,
			"lspTraceAnalysis": false,
			"maxCompletionItems": 512,
			"maxWorkspaceSymbolResults": 2000,
			"maxReferenceResults": 5000,
			"maxWorkspaceFiles": 10000,
			"maxWorkspaceFileBytes": 2097152,
			"bindingCatalogPaths": ["bindings/core.json", "bindings/game.json"],
			"predefinedMacros": [
				{
					"name": "FEATURE",
					"parameters": ["value"],
					"variadic": false,
					"body": "value",
					"sourceName": "host"
				}
			]
		}
	}
}
```

| Réglage | Type | Signification |
| --- | --- | --- |
| `importPaths` | tableau de chaînes | Racines supplémentaires de recherche des modules. |
| `stdlibPath` | chaîne | Racine de recherche de la bibliothèque standard. Une valeur vide efface la racine personnalisée. |
| `defines` | tableau de chaînes | Définitions de préprocesseur objet, par exemple `FEATURE_SCORE=1`. |
| `implicitImportPaths` | tableau de chaînes | Fichiers disponibles pour chaque source selon les règles normales de visibilité. |
| `allowCircularImports` | booléen | Autorise les dépendances de modules cycliques. |
| `allowProjectWideAccess` | booléen | Autorise le partage dans le projet des déclarations non exportées. |
| `lspRequestTimeoutMs` | entier | Échéance interactive en millisecondes (50–30000), défaut 2000. |
| `lspAnalysisTimeoutMs` | entier | Budget full/delta en millisecondes (50–30000), défaut 5000. |
| `lspTraceAnalysis` | boolean | Émettre les durées via `window/logMessage`, défaut false. |
| `maxCompletionItems` | entier | Nombre maximal d'éléments de complétion par liste (32–5000) ; une liste tronquée est marquée incomplète. |
| `maxWorkspaceSymbolResults` | entier | Nombre maximal de symboles renvoyés par `workspace/symbol` (64–20000). |
| `maxReferenceResults` | entier | Nombre maximal de positions renvoyées par une requête de références (64–50000). |
| `maxWorkspaceFiles` | entier | Nombre maximal de fichiers `.vs` fermés indexés sous les racines LSP (1–100000). |
| `maxWorkspaceFileBytes` | entier | Taille maximale de chaque fichier indexé fermé (1024–67108864 octets). |
| `bindingCatalogPaths` | tableau de chaînes | Catalogues de bindings hôte ordonnés ; ils sont fusionnés et les doublons sont rejetés. |
| `predefinedMacros` | tableau d'objets | Macros hôte contenant `name`, `parameters`, `variadic`, `body` et éventuellement `sourceName`. |

Les chemins et les macros doivent correspondre à l'invocation CLI du projet.
Les catalogues sont contrôlés au démarrage et lorsqu'un fichier change. Un
catalogue modifié est rechargé et les documents ouverts sont réanalysés ; il ne
crée pas à lui seul les callbacks du runtime.

La réponse `initialize` contient aussi `serverInfo` avec le nom du serveur, la
version générée du produit, la plateforme hôte et la capacité JIT. Ces champs
décrivent la distribution en cours ; le client peut les afficher, mais ne doit
pas coder en dur une version ni déduire la sémantique du langage de la plateforme
ou de la valeur JIT.

## Capacités annoncées

| Capacité | Contrat |
| --- | --- |
| Synchronisation | Ouverture/fermeture, changements incrémentaux et sauvegarde avec texte. |
| Complétion | Déclencheurs `.`, `:` et `@`; les éléments incluent la documentation résolue lorsqu'elle existe. Resolve la conserve et ne transforme `detail` en Markdown que si la documentation manque. |
| Survol | Détails résolus du symbole et documentation source ou hôte contiguë. Les tags source `@param` et `@return(s)` utilisent des libellés localisés. |
| Navigation | Définition, déclaration, type-definition et implementation. |
| Symboles et plages | Symboles imbriqués, plus plages token-ligne-document. |
| Liens de document | Liens vers les chemins entre guillemets de `import` et `#include`. |
| Tokens sémantiques | Tokens complets et mises à jour delta ; les plages ne sont pas annoncées. |
| Plages de pliage | Pliage sûr pour les blocs, les commentaires contigus et les groupes d'import. |
| Aide à la signature | Déclencheurs `(` et `,`. |
| Références et surbrillances | Références d'espace de travail et surbrillances lecture/écriture. |
| Renommage | `prepareRename` est disponible avant `rename`. |
| Formatage | Formatage du document, formatage de plage étendu aux lignes physiques complètes et formatage on-type après `;`, `}` ou un retour à la ligne. |
| Actions de code | `source.formatDocument`, `source.organizeImports` pour supprimer sans risque les imports exactement dupliqués, `refactor.rewrite` pour générer des implémentations de méthodes, et `codeAction/resolve`. |
| Symboles de l'espace de travail | Recherche insensible à la casse dans les tampons ouverts, les racines configurées et les symboles source, hôte et soft disponibles. |

## Méthodes du protocole

Le serveur accepte les messages de cycle de vie et de synchronisation suivants :

- `initialize`, `initialized`, `shutdown` et `exit` ;
- `textDocument/didOpen`, `textDocument/didChange`, `textDocument/didSave` et
  `textDocument/didClose` ;
- `workspace/didChangeConfiguration`, `workspace/didChangeWorkspaceFolders` et
  `workspace/didChangeWatchedFiles` ; et
- `$/cancelRequest` pour annuler une requête en attente.

Les méthodes de fonctionnalité sont :

- `textDocument/completion` et `completionItem/resolve` ;
- `textDocument/hover`, `textDocument/definition`,
  `textDocument/declaration`, `textDocument/typeDefinition`,
  `textDocument/implementation` et la hiérarchie de types ;
- `textDocument/prepareTypeHierarchy`, `typeHierarchy/supertypes` et
  `typeHierarchy/subtypes` ;
- `textDocument/documentSymbol`, `textDocument/selectionRange`,
  `textDocument/documentLink` et `textDocument/foldingRange` ;
- `textDocument/semanticTokens/full` et
  `textDocument/semanticTokens/full/delta` ;
- `textDocument/signatureHelp`, `textDocument/references` et
  `textDocument/documentHighlight` ;
- `textDocument/prepareRename` et `textDocument/rename` ;
- `textDocument/formatting`, `textDocument/rangeFormatting`,
  `textDocument/onTypeFormatting`, `textDocument/codeAction` et
  `codeAction/resolve` ; et
- `workspace/symbol` et `workspaceSymbol/resolve`.

## Synchronisation des documents

La synchronisation est incrémentale (`textDocumentSync.change = 2`). Chaque
changement est appliqué à la superposition en mémoire dans l'ordre reçu. Le
serveur accepte les changements complets et les changements par plage. Les
positions LSP utilisent des lignes indexées à partir de zéro et des unités
UTF-16.

Les versions sont monotones pendant chaque cycle d’ouverture d’une URI. Un `didChange` dont la version
n'est pas supérieure à la dernière version acceptée est ignoré. `didSave` peut
contenir le texte complet et met à jour la superposition avant les diagnostics.
`didClose` supprime la superposition et efface les diagnostics de cette URI.
Les tampons ouverts ont priorité sur les fichiers du disque lors de la
résolution des imports.

`workspace/didChangeConfiguration` applique les nouveaux réglages et réanalyse
les documents ouverts. `workspace/didChangeWorkspaceFolders` réapplique les
options courantes. `workspace/didChangeWatchedFiles` invalide l'index des
symboles et réanalyse les documents ouverts après une modification sur disque.

`initialize.workspaceFolders` (ou `rootUri`) définit les racines explicites de
l'indexation des fichiers fermés. Les tampons ouverts restent prioritaires ;
les limites `maxWorkspaceFiles` et `maxWorkspaceFileBytes` s'appliquent aux
fichiers fermés sous ces racines.

## Sémantique Tooling

- La complétion regroupe les surcharges appelables ayant le même nom visible en
  un seul élément et affiche chaque signature sur une ligne distincte du détail.
  Dans un contexte d'appel, une surcharge groupée utilise un point d'arrêt
  d'argument vide, par exemple `Select(${0})`, plutôt que de choisir les
  paramètres d'une surcharge. Un élément appelable sans surcharge peut produire
  un snippet LSP tel que `Helper(${1:count}, ${2:name})` ; les types génériques peuvent produire
  `HostPair<${1}, ${2}>`. Si un `<` suit la plage de remplacement, éventuellement
  après des espaces ou des tabulations, le serveur omet le snippet générique et
  insère seulement le nom du type pour éviter un second `<`. La complétion d'appel
  omet son snippet si un `(` suit déjà la cible, pour éviter de dupliquer
  la syntaxe d'appel.
  Le texte au format snippet n'est envoyé que si le client annonce
  `textDocument.completion.completionItem.snippetSupport` ; sinon, l'insertion
  utilise du texte brut. Si le client annonce la prise en charge des snippets,
  les fonctions hôtes de formatage variadiques utilisent, dans un contexte
  d'appel, un point de tabulation vide placé entre parenthèses, par exemple
  `print(${0})`, plutôt
  que des placeholders déduits de signatures fusionnées. Sans cette prise en
  charge, seul le nom de la fonction est inséré.
- Une complétion issue d'un namespace qualifié conserve le nom final dans
  `label` ; si un snippet appelable ou générique est disponible et que le client
  annonce la prise en charge des snippets, `insertText` (et `textEdit.newText` si ce
  champ est présent) l'utilise ; sinon, le nom seul est inséré. Son conteneur est dans le champ standard
  `labelDetails.description`. Les métadonnées
  facultatives `$valencePresentation` fournissent à Valence le texte rendu du
  label/de la déclaration et les plages sémantiques UTF-16 ; les autres clients
  LSP peuvent les ignorer. Elles ne modifient ni le filtrage, ni le tri, ni le
  texte inséré.
- La complétion d'un segment de namespace conserve son nom final dans `label`
  et `filterText`. Dans un chemin d'expression ou d'instruction, accepter un
  namespace insère `::` si aucun séparateur ne suit la plage remplacée ; si le
  séparateur est déjà présent, seul le nom est inséré. Après `using namespace`,
  seul le nom est inséré.
- Elle classe d'abord la qualité de correspondance textuelle : correspondance
  exacte, exacte sans casse, préfixe, limite de mot/camel-case, contenu, puis
  sous-séquence. `sortText` porte cet ordre complet. Le premier élément reçoit
  `preselect` seulement si son niveau exact, exact sans casse ou préfixe est
  strictement meilleur que celui de tous les autres éléments renvoyés.
- Les mots de types primitifs et conteneurs sont des types intégrés. Une écriture
  primitive reste un `keyword` dans la complétion, le survol et les tokens
  sémantiques ; une écriture de conteneur est un `type`/alias de type. Les deux
  ont un survol localisé, aucune définition source et ne peuvent pas être renommés.
- `completionItem/resolve` conserve l'élément d'origine et la documentation déjà fournie. Si elle manque mais que `detail` existe, resolve expose ce détail en Markdown.
- Le survol lit les commentaires contigus avant les déclarations source. Les éléments de complétion des macros préprocesseur définies dans le source peuvent porter leur commentaire précédent ; la documentation de complétion hôte vient des métadonnées de binding. Le survol formate `@param`, `@return` et `@returns` avec les libellés du catalogue actif.
- Les espaces de noms utilisateur et l'arbre réservé `std::` sont traités comme
  des namespaces dans le survol, la navigation, les références, le renommage,
  l'outline et les tokens. Les namespaces de la bibliothèque standard ne sont
  pas renommables.
- `textDocument/declaration` utilise la même cible que definition. Le
  type-definition trouve les déclarations de classes, structs, interfaces,
  enums et alias de type, y compris avec une récupération sur un tampon
  incomplet. `textDocument/implementation` renvoie les types dérivés et les
  implémentations correspondantes des membres d'interface ou virtuels ; seuls les
  membres virtuels, `override` ou abstraits non `final`, avec des paramètres et un
  qualificatif `const` correspondants, sont retenus. Les noms de types ambigus sont
  ignorés ; la requête ne se comporte pas comme une simple recherche de définition.
- Les plages de sélection s'imbriquent du token à la ligne puis au document.
  Les liens de document reconnaissent `import` et `#include` entre guillemets,
  avec le préfixe facultatif `export`.
  Ils utilisent le résolveur de modules du cœur : répertoire courant, chemins
  configurés, bibliothèque standard et documents ouverts non enregistrés.
  Une cible non résolue conserve un lien local.
- Les tokens sémantiques delta renvoient toutes les données si l'identifiant
  précédent est inconnu ou obsolète ; sinon ils renvoient une modification de
  remplacement et un nouvel identifiant.
- La légende complète des tokens est `namespace`, `type`, `class`, `enum`,
  `interface`, `struct`, `variable`, `property`, `enumMember`, `function`,
  `method`, `macro`, `keyword`, `decorator`, `decoratorArgument`, `constant` et
  `label`, `string`, `number`, `comment` et `preprocessor`. Une base lexicale
  classe aussi les mots du langage/de type, les têtes de directives et les noms
  de macros. Elle couvre chaque ligne physique valide sans plages LSP qui se
  chevauchent et reste disponible durant la récupération d'une entrée incomplète.
- Le formatage par plage étend prudemment la sélection aux lignes physiques
  complètes et conserve l'indentation du bloc englobant. Le formatage complet,
  par plage et à la frappe respecte `insertSpaces` : `false` insère une tabulation
  par niveau ; sinon, `tabSize` définit les espaces par niveau (maximum 16,
  quatre par défaut si la valeur manque ou est invalide).
- Une action de code renvoie `source.formatDocument`, sauf si `context.only` la
  filtre. `source.organizeImports` est renvoyée uniquement si des lignes
  d'import exactement dupliquées peuvent être repérées et supprimées sans
  risque ; cette action ne supprime que ces doublons.
- À une position de curseur vide dans le corps d'une classe ou d'une structure,
  les actions `refactor.rewrite` peuvent générer les mêmes implémentations de
  méthodes que la complétion. `context.only` peut les filtrer avec `refactor`
  ou `refactor.rewrite`.
- Dans le corps d'une classe ou d'une structure, à une position vide ou après
  un préfixe d'identifiant, la complétion propose les méthodes virtuelles
  héritées accessibles qui ne sont ni finales ni déjà implémentées, ainsi que
  les méthodes d'interface admissibles : membres purs à implémenter ou méthodes
  virtuelles par défaut pouvant être redéfinies. Les signatures génériques
  sont spécialisées pour le type dérivé. Le squelette inséré contient un corps
  `throw 0;` modifiable ; une implémentation d'interface pure n'ajoute pas
  `override`. Sans prise en charge des snippets, le texte de méthode est inséré
  normalement.
- Les recherches de symboles d'espace de travail ignorent la casse et découpent la
  requête sur les espaces. Elles portent sur les noms, noms qualifiés, conteneurs,
  détails et libellés de fichiers ; `::` et l'ancien séparateur `.` sont acceptés.
  Les correspondances exactes ou par préfixe sont classées avant les correspondances
  floues par sous-séquence. Chaque élément porte un `data.veroscriptSymbolId` stable ;
  `workspaceSymbol/resolve` actualise ses champs detail et conteneur, et conserve
  l'élément initial si l'identifiant est inconnu.
- Les symboles de document et d'espace de travail exposent les conteneurs namespace.
  Une même identité de déclaration n'est renvoyée qu'une fois, même si elle est
  rencontrée par plusieurs chemins importés. Les symboles hôte sans fichier utilisent
  l'URI `veroscript:host-bindings` et une plage nulle.

Après l'ouverture, une modification acceptée, une sauvegarde ou un changement de
configuration, le serveur publie `textDocument/publishDiagnostics`. Les messages
sont regroupés par fichier, y compris les imports, et les listes vides
suppriment les marqueurs obsolètes. Chaque diagnostic contient la gravité LSP,
le message, le code stable lorsqu'il existe et `source: "veroscript"`. Les
symboles hôte sans fichier utilisent l'URI `veroscript:host-bindings` et une
plage nulle.

Lorsqu'aucun résultat ne s'applique, le serveur renvoie le résultat nul ou vide
prévu par la méthode LSP. Les méthodes non prises en charge renvoient l'erreur
JSON-RPC `-32601` ; les paramètres mal formés renvoient `-32602` lorsque la
méthode exige un document ou une position précise.

## Localisation

Le serveur applique la même politique de localisation que le CLI et le runtime.
`initialize.locale` modifie l'affichage, jamais la sémantique du script. Les
clients doivent utiliser les identifiants de message plutôt que le texte traduit
pour leur logique. Les fichiers ou clés absents reviennent au catalogue `en-US`.
Voir [Localisation des diagnostics](../../../../../VeroScript/Resources/Docs/User/fr-FR/localization.md).

## Références associées

Voir aussi [Module Tooling](index.md), [Référence CLI](../../../../../VeroScriptCli/Resources/Docs/User/fr-FR/index.md),
[Bindings hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/bindings.md) et [Débogage](debugging.md).

## Sources de modules fournies par l’hôte

Un hôte peut fournir un environnement de modules résolu et des processeurs de compilation. Le serveur Runtime standard ne reprend pas automatiquement la sélection de paquets de la CLI ni les rappels natifs d’une application. Faites correspondre les imports, les catalogues et les services hôtes au programme édité ; un catalogue fournit des descriptions, pas des processeurs exécutables.

Lorsque `capabilities.experimental.veroscriptModuleEnvironment` annonce `moduleSourceMethod`, un client compatible peut utiliser cette méthode (`veroscript/moduleSource`) pour ouvrir les emplacements `veroscript-module:` en lecture seule. Ce sont des instantanés, pas des fichiers de travail modifiables. Naviguez à nouveau après un redémarrage du serveur ou une modification de son empreinte de compilation. Un module absent ou inaccessible renvoie `null`. Les sources sur disque conservent des URI de fichier.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/lsp.md) · [fr-FR](lsp.md) · [ja-JP](../ja-JP/lsp.md) · [ko-KR](../ko-KR/lsp.md) · [zh-CN](../zh-CN/lsp.md) · [zh-TW](../zh-TW/lsp.md)
