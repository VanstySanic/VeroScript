# VeroScript — Rider

## Installation

Installez le ZIP dans Paramètres → Plugins → Installer depuis le disque. Rider 2026.2.2 (262.10315.191) est la base SDK vérifiée et le build minimal déclaré ; les versions suivantes nécessitent une vérification. Pour édition, Run, Check et débogage local, extrayez le Runtime compatible et choisissez sa racine dans Langages et frameworks → VeroScript : `veroscript`, `veroscript-lsp`, `veroscript-dap`, `stdlib/`, `localization/`. Le Runtime personnel du projet prime sur le défaut global ; si les deux sont vides, recherche via `PATH`. La coloration simple fonctionne sans Runtime. Redémarrez le serveur après remplacement des binaires.

Lors d’une mise à jour ou d’un déplacement, conservez tout le Runtime extrait,
y compris `Vm/` et ses bibliothèques natives. Les outils fournissent la VM par
défaut ; aucun réglage Rider supplémentaire n’est nécessaire.

## Paramètres

Les options sémantiques sont un objet JSON validé : `importPaths`, `implicitImportPaths`, `stdlibPath` ou `stdlibSearchPaths`, `bindingCatalogPaths`, `defines`, `predefinedMacros`, `allowCircularImports`, `allowProjectWideAccess`, `lspTraceAnalysis`, `lspRequestTimeoutMs`, `lspAnalysisTimeoutMs`, `maxCompletionItems`, `maxWorkspaceSymbolResults`, `maxReferenceResults`, `maxWorkspaceFiles`, `maxWorkspaceFileBytes`. Les catalogues décrivent les symboles ; les callbacks exécutables sont enregistrés par l’hôte. Options partagées dans `.idea/veroscript.xml`, chemins Runtime personnels. Langue des outils : BCP 47, vide = langue IDE ; interface disponible dans les six langues.

## Édition

Lorsqu’un commentaire `//` se termine par une barre oblique inverse immédiatement avant LF ou CRLF, la ligne physique suivante reste un commentaire. La coloration locale et Entrée dans un bloc vide suivent cette règle, y compris pour VS intégré au balisage GUI. Un espace ou une tabulation après la barre empêche la continuation.

Le survol sépare la catégorie du symbole de son nom ; les types primitifs et conteneurs intégrés ont un seul titre de catégorie. Les alias de types définis dans les sources conservent leur catégorie et leur déclaration. Les signatures, surcharges, descriptions, exemples et emplacements de définition conservent le contenu fourni par Runtime.

L’option « Placer l’accolade ouvrante d’un bloc vide sur une nouvelle ligne avec Entrée » est activée par défaut dans les paramètres VeroScript. Dans un bloc de déclaration ou de contrôle vide sur une seule ligne, Entrée déplace l’accolade ouvrante sur sa propre ligne et place le curseur à l’intérieur avec indentation. Les commentaires, chaînes, sélections, curseurs multiples et en-têtes ambigus ou répartis sur plusieurs lignes utilisent l’action Entrée normale. Modifier cette option ne redémarre pas LSP ; le formatage complet ou partiel suit toujours le style du serveur.

Les diagnostics restent ceux de Runtime. `void t(){}` est une fonction complète valide ; `void t` est incomplet et nécessite `()`. Les diagnostics pendant la saisie disparaissent lorsque le serveur accepte le code complété.

## Chemins

Les chemins projet sont relatifs à sa racine. Le répertoire de travail part de cette racine ; programme, entrée et chemins de remplacement, dont JIT, partent du répertoire résolu. Les chemins hérités gardent leur base. `$PROJECT_DIR$` désigne toujours la racine. Les options omises héritent ; les listes explicites, même vides, remplacent pour le lancement. Ces remplacements ne modifient pas LSP.

## Run et Check

Clic droit sur un `.vs` enregistré ou créez une configuration VeroScript. Toute source peut être l’entrée, avec instructions globales sans `main`. `run` exécute, `check` compile sans arguments programme, `attach` se connecte avec Debug. Guillemets IDE usuels (`""` = argument vide), environnement JSON de chaînes. Entrée normale en octets ; entrée debug en texte UTF-8 préparé. Les opérations locales enregistrent les documents. Current File utilise le réglage Rider ; activez `run.current.file.item.in.run.configurations.combobox` dans Registry si absent.

Les actions contextuelles utilisent les fichiers locaux enregistrés, sélectionnent
Run/Check et effacent les champs d’attachement du modèle. Check efface aussi les
arguments, entrée et chemin JIT hérités ; répertoire, environnement et options
de compilation compatibles sont conservés.

## Débogage local et JIT

Run propose `vm`, `mixed` et `jit`. Le champ fichier/répertoire JIT permet de fixer un module compatible. Vide, il omet le chemin CLI et hérite du choix de l’environnement de modules : sans choix configuré, la valeur par défaut est auto. Si JIT est désactivé ou sans candidat automatique utilisable, mixed utilise VM et jit forcé échoue avant l’exécution. Check et Debug local ne chargent pas ce module ; attach exige un chemin vide. Debug utilise VM. Pause initiale `inherit`/`true`/`false`, profondeur 0 = défaut ; préparation 1–3600 secondes, défaut 30. Points de ligne/conditionnels, pause, pas, pile, portées, variables paginées, évaluation et modification sont disponibles. Les références expirent à la reprise. Survol automatique limité aux chemins de noms ; autres expressions via évaluation explicite. En mode programme, les changements de points attendent la pause suivante.

Les variables sont affichées par lots de 100 enfants au maximum, même si l’hôte fournit une liste complète sans indiquer leur nombre. La reprise invalide les pages en attente.

Les variables marquées en lecture seule par le débogueur ne proposent pas de modification. L’édition des autres variables dépend toujours des capacités de l’hôte et du contexte de débogage.

## Attachement programme

Attach/program se connecte à un hôte TCP DAP, pas à un PID ou une VM en cours. Lancez `veroscript --debug --dap-port=4711 script.vs`, choisissez adresse et port. Programme vide = entrée hôte. Répertoire, entrée et environnement restent vides ; l’hôte choisit backend/profondeur. Seules les options sémantiques explicites sont envoyées. Arguments non vides remplacent ; l’option de remplacement vide efface. Certaines listes sémantiques vides héritent des valeurs hôte, sans les effacer.

L’attachement de programme et de domaine supprime les espaces autour de l’adresse de l’hôte et refuse les espaces ou caractères de contrôle à l’intérieur.

## Attachement domaine

Attach/domain accepte un ID attendu facultatif, sans Runtime ni fichier local. Gardez les remplacements vides : environnement `{}`, entrée `inherit`, profondeur 0, backend mixed, héritage d’environnement activé. L’hôte doit annoncer `veroscriptSupportsDomainAttach` ; sinon échec sans chargement de programme. Une connexion couvre plusieurs appels. Les capacités hôte contrôlent pause, pas, évaluation, conditions et modification. Identité/révisions opaques sont affichées ; ni comparaison des sources ni synchronisation LSP automatique. Les chemins des sources physiques doivent être accessibles localement ; pas de mappage de chemins. Les références source de l’adaptateur s’affichent comme instantanés en lecture seule. Connexion 5 secondes, initialisation 10, attachement selon budget de préparation. Déconnexion avec `terminateDebuggee=false` ; annulation et processus appartiennent à l’hôte. Perte de connexion sans reconnexion automatique.

## Dépannage

Consultez état LSP, console debug et journaux IDE. Installez ensemble les outils Runtime correspondants. Cibles : projets Rider locaux, transports Runtime Windows/Linux x64. Autres IDE, Remote Development, lancement Windows-vers-WSL, macOS/ARM64 et intégration réelle de domaine application/UE restent à valider.

```json
{"importPaths":["src"],"defines":["FEATURE=1"],"bindingCatalogPaths":["bindings/catalog.json"]}
```

[LSP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/lsp.md)

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/User/fr-FR/debugging.md)


## Mémoire gérée

Avec les outils Runtime actuels correspondants, LSP fournit la complétion, le survol et la coloration sémantique de `std::memory`. Pendant une pause du débogueur, développez un buffer ou une vue dans les variables ou expressions surveillées pour consulter `[byte_length]`, `[object_id]` du buffer et les adresses disponibles. Ces informations sont en lecture seule et les adresses sont temporaires. Le plugin ne fournit pas de lecture ou écriture arbitraire de mémoire. Les références expirent à la reprise ; développez de nouveau les valeurs lors de la pause suivante.

[std::memory](../../../../../../../../VeroScript/Resources/Docs/User/fr-FR/standard-library/memory.md)

## Extensions de compilation et sources générées

Les outils actuels prennent en charge les macros publiques (`#define_export`, `export import`) et les extensions de compilation source (`import compiler "extension.vs";`). Utilisez des outils CLI/LSP/DAP correspondants et les mêmes chemins d’importation. Les méthodes générées et modules virtuels avec un `sourceReference` DAP s’ouvrent comme instantanés de débogage en lecture seule. Run, Check, points d’arrêt et synchronisation LSP sont désactivés pour ces instantanés. Un échec de lecture est signalé sans terminer la session suspendue. Les registres de traitement natifs et environnements de modules restent configurés par l’hôte ; le plugin ne les expose pas comme champs JSON sémantiques supplémentaires.

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
