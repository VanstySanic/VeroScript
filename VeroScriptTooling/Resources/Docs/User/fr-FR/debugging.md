# Débogage

Le débogueur VeroScript utilise le Debug Adapter Protocol (DAP) et exécute des scripts via la VM. Les sessions de débogage et de rechargement à chaud n'utilisent pas le chemin d'exécution JIT.

## Mode d'entrée/sortie standard

```powershell
veroscript --debug script.vs
```

Ce mode transporte les messages DAP via une entrée et une sortie standard, ce qui convient à un éditeur ou à un autre client de débogage qui lance et gère le processus.

## Mode TCP

```powershell
veroscript --debug --dap-port=4711 script.vs
veroscript --debug --dap-port=4711 --dap-bind=127.0.0.1 script.vs
```

`--dap-port` démarre un service TCP DAP sur un port de `1` à `65535`. `--dap-bind` sélectionne l'adresse de liaison et prend par défaut la valeur `127.0.0.1`. Liez-vous à une adresse sans bouclage uniquement lorsqu'un client externe en a réellement besoin.

`veroscript-dap` est un alias léger pour le même hôte de débogage. Les sessions de lancement et d'attachement de l'IDE F6 utilisent également DAP ; lorsqu'une session se déconnecte, le client peut choisir de mettre fin au débogueur avec `terminateDebuggee`.

## Cycle de session et paramètres de lancement

Le client doit envoyer `initialize` avant `launch` ou `attach`. L'adaptateur
renvoie notamment la capacité `supportsTerminateRequest`, ainsi que les
métadonnées `name`, `version`, `platform` et `jitCapability`. L'hôte filaire
émet l'événement `initialized` après la réponse à `initialize`.

L'adaptateur annonce actuellement les capacités DAP suivantes :

| Capacité | Valeur |
| --- | --- |
| `supportsConfigurationDoneRequest` | `true` |
| `supportsStepInRequest` | `true` |
| `supportsStepOutRequest` | `true` |
| `supportsStepBackRequest` | `false` |
| `supportsConditionalBreakpoints` | `true` |
| `supportsEvaluateRequest` | `true` |
| `supportsSetVariable` | `true` |
| `supportsTerminateRequest` | `true` |

`jitCapability` identifie le contrat de services JIT du cœur (actuellement
`backend-services`), pas un backend natif installé ou sélectionné.

Avec `attachMode: "domain"` et `domainId`, un domaine persistant préparé par
l'hôte reste débogable entre plusieurs appels de script. Le client doit vérifier
que `initialize.body.veroscriptSupportsDomainAttach` vaut exactement `true` ;
sinon il doit refuser ce mode sans revenir au chargement d'un programme.
`configurationDone` active la connexion sans charger ni exécuter de script.
Les capacités figurent dans la réponse à l'attachement. La déconnexion par défaut
reprend l'exécution suspendue selon la politique de l'hôte et le laisse actif.
Ce mode ne prend pas le contrôle d'une VM déjà en exécution JIT. Voir le
[contrat du domaine](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/session-debugging.md).

Le chargement décrit ci-dessous concerne `launch` et `attach` en mode programme
(`attachMode: "program"` ou absent). Ils chargent le programme avec le même chargeur de
débogage. `attach` est la requête utilisée lorsqu'un IDE se connecte à un hôte
intégré ou TCP qui possède des bindings actifs ; elle ne s'attache pas à une VM
déjà en cours d'exécution. Après avoir défini les points d'arrêt, envoyez
`configurationDone` pour démarrer l'exécution.

Les paramètres pris en charge par `launch` et `attach` sont les suivants :

| Champ | Signification |
| --- | --- |
| `program` | Chemin du script. La commande de débogage CLI peut fournir la valeur par défaut. |
| `source` | Texte source intégré. S'il n'est pas vide, il est prioritaire sur `program`. |
| `fileLabel` | Libellé d'affichage et identité source de `source`. |
| `workspace` | Répertoire de base des chemins de lancement relatifs. |
| `cwd` | Répertoire de travail du script et de résolution des chemins relatifs ; l'alias `workingDirectory` est accepté. À défaut, `workspace` est utilisé s'il est disponible ; le répertoire doit exister. |
| `args` | Tableau de chaînes transmis à `main(string[] args)`, sans le chemin du script. |
| `input` | Entrée standard initiale du script, indépendante du flux du protocole DAP. |
| `env` | Surcharges de variables d'environnement pour le débogage. La forme canonique est un objet ; `environment` est un alias et un tableau d'éléments `{ "name", "value" }` est aussi accepté. |
| `stdlibSearchPaths` | Racines ordonnées de recherche de la bibliothèque standard ; `stdlibPath` est l'alias compatible pour une seule racine. |
| `importPaths`, `implicitImportPaths` | Chemins de recherche de modules ordinaires et implicites. |
| `defines`, `predefinedMacros` | Définitions du préprocesseur et macros prédéfinies de style hôte. |
| `sourceOverlays` | Fichiers source en mémoire utilisés pour résoudre les modules importés. |
| `bindingCatalogPaths` | Fichiers de catalogue de bindings chargés dans l'ordre comme métadonnées/stubs ; les `LiveBindings` d'un hôte intégré sont prioritaires. |
| `allowCircularImports`, `allowProjectWideAccess` | Politiques d'importation et de visibilité du projet. |
| `stopOnEntry` | Arrêt au point d'entrée avant la poursuite de l'exécution normale. |
| `backend`, `maxCallDepth` | Préférence de backend et limite de profondeur d'appel ; le débogage utilise toujours la VM. |

Les chemins relatifs de `program`, des imports, de la bibliothèque standard, des
imports implicites, des overlays et des catalogues sont résolus depuis `cwd`
quand il est défini, sinon depuis `workspace`. Si `workspace` est fourni sans
`cwd`, il devient aussi le répertoire de travail du script. Les surcharges
d'environnement sont limitées à la session et restaurées ensuite. Une session
`attach` utilisant des bindings actifs ne peut pas combiner des surcharges
d'environnement avec une surcharge du répertoire de travail.

La CLI accepte des arguments après le chemin du script et les transmet comme
valeurs par défaut de la requête DAP :

```powershell
veroscript --debug hello.vs red blue
veroscript --debug --dap-port=4711 hello.vs red blue
```

Un tableau DAP `args` explicite remplace ces valeurs par défaut. Une session est
refusée si elle contient plus de 4096 arguments de script, plus de 2048 éléments
dans chacun des trois tableaux de chemins de modules, ou plus de 1024 variables
d'environnement. Un nom de variable ne doit pas être vide, contenir `=` ou NUL ;
une valeur ne doit pas contenir NUL.

## Exécution asynchrone et contrôles

Pour les hôtes filaires sur entrée/sortie standard et TCP, `configurationDone`
est acquitté avant la fin de la VM et celle-ci s'exécute sur un thread dédié. Le
lecteur du protocole reste donc disponible pour les contrôles et les requêtes
pendant un arrêt. L'événement `stopped` porte l'une des raisons `entry`,
`breakpoint`, `step` ou `pause` ; `pause` est produit lorsqu'une pause coopérative
interrompt une VM en cours d'exécution.

`continue`, `next`, `stepIn` et `stepOut` ne sont valides qu'après `stopped` et
reprennent l'exécution. `stackTrace`, `scopes`, `variables`, `evaluate` et
`setVariable` ne sont également valides que pendant un arrêt. Définissez les
points d'arrêt avant `configurationDone` ou pendant un arrêt ; une modification
pendant l'exécution est refusée. `pause` n'est valide que lorsque la VM s'exécute
et demande un arrêt coopératif ; `terminate` demande une annulation coopérative.
Le client doit attendre les événements `exited` et `terminated` qui suivent.

La requête `threads` expose un seul thread logique de VM, d'identifiant `1` et
de nom `main`. Il s'agit de la vue d'exécution du débogueur ; les threads du
protocole et du worker utilisés par l'hôte filaire ne sont pas exposés comme
des threads de script.

Pour `disconnect`, `terminateDebuggee` vaut par défaut `true` pour `launch` et
`false` pour `attach`. Dans un hôte TCP multi-session, `terminateDebuggee=false`
termine la session courante tout en conservant l'écoute ; `true` arrête l'écoute.
La requête `terminate` annule toujours l'exécution et demande l'arrêt de l'hôte.

## Hôte TCP et intégration

L'hôte TCP de la CLI sert une session par défaut. Un hôte SDK peut utiliser
`VeroScriptDapTcp::Run` avec `FDapTcpRunOptions` : `MaxSessions=0` sert des
clients jusqu'à l'arrêt de l'hôte, `Port=0` choisit un port éphémère et le signale
via `OnListening`, et `BindAddress` devrait normalement rester à `127.0.0.1`.
Un hôte intégré peut définir `FDapStdioRunOptions::LiveBindings` afin qu'une
session `attach` exécute les fonctions hôte enregistrées plutôt que des stubs de
catalogue. Le catalogue actif doit rester valide pendant l'exécution DAP. Un
hôte intégré qui publie lui-même son état de disponibilité peut définir
`bAnnounceListen=false` ; l'annonce d'écoute par défaut est écrite sur le flux
d'erreur de l'hôte. `VeroScriptDapStdio::Run` renvoie `0` pour une session normale,
`1` en cas d'erreur de protocole ou d'E/S, et `2` lorsque le client demande
l'arrêt de l'hôte.
Pour `VeroScriptDapTcp::Run`, un arrêt de l'hôte demandé par le client est un
retour normal (`0`) ; les échecs d'écoute, d'acceptation ou d'une session non
normale renvoient une valeur différente de zéro.

## Prise en charge des points d'arrêt

L'adaptateur prend en charge les points d'arrêt de ligne source via
`setBreakpoints`. Les points d'arrêt d'exception et de fonction ne sont pas
pris en charge. Une requête `setExceptionBreakpoints` sans filtre réussit
uniquement comme opération d'effacement pendant le handshake de lancement
standard ; les requêtes contenant des filtres d'exception et toutes les requêtes
`setFunctionBreakpoints` renvoient une réponse DAP en échec. Le client ne doit
donc pas afficher ces catégories comme actives.

`setBreakpoints` emploie des lignes physiques de source indexées à partir de 1.
Avant que `launch` ou `attach` ait chargé le programme à déboguer, le point
renvoyé est `verified: false` et en attente. Après le chargement, l'adaptateur
le lie puis émet un événement DAP standard `breakpoint` de raison `"changed"`
avec le même identifiant DAP. Un point n'est vérifié que si son chemin résout une
unique source compilée, si sa ligne reste active après prétraitement et si une
instruction exécutable est trouvée. Aucun rapprochement par seul nom de fichier
n'est effectué. Un échec reste non vérifié et porte un `message` localisé.

Pour une ligne physique non exécutable située dans une fonction, l'adaptateur
peut renvoyer comme ligne liée l'emplacement exécutable suivant de cette
fonction : le client doit utiliser la valeur `line` retournée. Un point dans
l'initialiseur de module exige une ligne physique exacte. Les lignes retirées
par le prétraitement, hors plage, ou issues d'une source absente ou ambiguë ne
sont pas liées.

Les autres commandes DAP non prises en charge renvoient également une réponse
en échec avec un message localisé. Définissez une valeur BCP 47 dans `locale`
des arguments DAP `initialize` pour choisir la langue d'affichage. Le client
doit se baser sur les champs de succès/erreur DAP et non sur le texte traduit.

## Sortie pendant le débogage

`std::console::write` et `std::console::error` écrivent sans ajouter de
nouvelle ligne. `std::console::flush()` rend immédiatement visible la sortie
standard en attente. L'adaptateur DAP transmet la sortie standard et la sortie
d'erreur comme événements `output` distincts pendant l'exécution ; le résultat
final conserve également les deux flux séparés.

## Contraintes d'exécution

Le débogueur utilise le chemin de débogage complet de la VM pour les points d'arrêt, l'étape et l'inspection des variables. La demande de `--backend=mixed` ou `--backend=jit` ne fait pas basculer l’exécution de débogage actuelle vers JIT ; les exécutions ordinaires peuvent restaurer le backend demandé une fois le débogage terminé.

Voir [Intégration de l'hôte](../../../../../Extensions/Language/Cpp/Resources/Docs/User/fr-FR/embedding.md) pour une intégration plus fine du débogueur et de la liaison d'hôte.

## Inspection mémoire

En pause, développez un objet ou une région pour consulter les métadonnées en lecture seule telles que `[object_id]` et `[byte_length]`. Les adresses apparaissent seulement si elles sont autorisées. Les ID identifient des objets vivants et les adresses sont transitoires. Ces métadonnées ne sont pas modifiables. `variablesReference` est un handle DAP ; les requêtes arbitraires `readMemory` / `writeMemory` restent non prises en charge.

## Instantanés des sources dans les piles de débogage

Lorsqu’une trame fournit un `sourceReference` positif, le client doit demander son texte avec la requête DAP `source`. Le texte correspond à l’instantané compilé du code en ligne, généré ou en mémoire, y compris une superposition dont le fichier sur disque est ancien ou absent. Affichez-le en lecture seule ; modifier un fichier de même nom ne change pas le programme en cours. Le texte capturé reste lisible après la reprise, alors que les identifiants de trames et de variables expirent. Recharger ou effacer le programme invalide les anciennes références de sources ; récupérez la trame actuelle.

## Cycle de vie du débogueur natif

Sérialisez les opérations sur le thread propriétaire. Pendant Run, rappels compris, LoadSource/LoadFile et un Run imbriqué sont rejetés sans remplacer le programme ; Clear ne fait rien. Effacez après le retour ou le déroulement d’exception de Run. Run fige les arguments : un changement dans un rappel concerne l’exécution suivante. Remplacer/effacer SetStopHandler ou le rappel d’événement DAP affecte les notifications suivantes ; le rappel actif et ses captures restent vivants jusqu’à son retour. Cela n’autorise pas les accès concurrents.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/debugging.md) · [fr-FR](debugging.md) · [ja-JP](../ja-JP/debugging.md) · [ko-KR](../ko-KR/debugging.md) · [zh-CN](../zh-CN/debugging.md) · [zh-TW](../zh-TW/debugging.md)
