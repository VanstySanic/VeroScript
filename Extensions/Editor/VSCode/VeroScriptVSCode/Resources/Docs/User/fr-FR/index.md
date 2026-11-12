# VeroScript pour VS Code

Certaines versions de l’éditeur limitent les processus de terminal sans dossier de travail ouvert. Si Exécuter/Vérifier refuse le répertoire du script, ouvrez ce répertoire comme espace de travail. L’attachement à un domaine n’utilise pas de terminal.

## Démarrage

Utilisez VS Code de bureau 1.106.0 ou ultérieur et installez le VSIX local. Extrayez le Runtime compatible pour le système et l’architecture de l’hôte d’extensions. Exécutez **VeroScript: Choisir le Runtime** et sélectionnez sa racine contenant les trois programmes, stdlib et localization.

Enregistrez un fichier .vs puis utilisez **Exécuter le fichier**, **Vérifier le fichier** ou **Déboguer le fichier**. La coloration de base et les extraits fonctionnent sans Runtime. WSL, SSH et les conteneurs nécessitent un Runtime sur l’hôte distant.

## Configuration

Dans un espace de travail approuvé, `veroscript.lsp.executablePath` sélectionne un serveur de langage stdio fourni par l’hôte. Le chemin suit les règles communes de résolution. `veroscript.lsp.arguments` transmet chaque élément comme un argument littéral, y compris les espaces et les chaînes vides. Les réglages de compilation non configurés conservent les valeurs par défaut de l’hôte. Un chemin vide sélectionne le serveur du Runtime. Un changement du chemin ou des arguments redémarre le serveur. Le serveur personnalisé hérite de l’environnement de l’hôte d’extensions et dispose de dix secondes pour s’initialiser.

Accepter une complétion d’espace de noms affiche ses membres, y compris les espaces imbriqués, sauf si editor.suggestOnTriggerCharacters est désactivé. VeroScript définit editor.wordBasedSuggestions à off par défaut ; modifiez ce réglage dans [veroscript] pour réactiver les mots textuels. Les modèles intégrés filtrent le préfixe et le contexte lexical de manière prudente : imports et points d’entrée au niveau du fichier, déclarations dans les zones prises en charge et boucles au début d’une instruction. Ils sont masqués dans les commentaires, chaînes et expressions incomplètes ; les extraits utilisateur restent gérés par VS Code. Les champs de filtrage et de tri natifs sont conservés ; VS Code tient aussi compte du préfixe saisi.

Les signatures au survol utilisent des blocs de code VeroScript ; les surcharges, descriptions et emplacements de définition sont séparés. Les détails de complétion évitent les signatures en double et conservent documentation, liens et exemples. Les couleurs suivent le thème VS Code ; le serveur natif détermine toujours le paramètre actif de l’aide à la signature.

Les paramètres veroscript.importPaths, implicitImportPaths, defines, predefinedMacros et bindingCatalogPaths appartiennent au dossier de travail propriétaire. Les chemins relatifs utilisent ce dossier, ou le répertoire source sans espace de travail. Les chemins partagés acceptent workspaceFolder et les variables d’environnement ; les contextes manquants produisent une erreur.

Chaque racine, y compris les racines imbriquées et les fichiers extérieurs, possède son propre contexte de langage. Consultez **Afficher la sortie** en cas d’erreur et redémarrez le serveur après avoir remplacé les binaires Runtime.

Les fichiers ouverts non enregistrés des répertoires d’importation/bibliothèque standard configurés et les imports implicites explicites sont synchronisés avec les serveurs qui les utilisent. Le contexte propriétaire conserve les fonctions d’édition et les diagnostics. Le Runtime actualise les diagnostics du fichier modifié ; ceux d’un autre fichier qui l’importe sont actualisés lors de sa réanalyse, par exemple après sa modification ou son enregistrement.

Les commandes proposent d’enregistrer les sources concernées. Les tâches directes respectent task.saveBeforeRun et conservent les arguments et codes de sortie natifs. Les listes et l’environnement explicite d’une tâche remplacent les valeurs héritées. Configurez les imports avec les paramètres de chemins plutôt que VERO_PATH ou VERO_STDLIB_PATH.

## Débogage

F5 lance DAP sur la VM. stopOnEntry, args, input, cwd, env et les options de compilation sont disponibles. La console évalue des expressions ; input fournit l’entrée initiale. Pour une saisie interactive ou une exécution sans débogage, utilisez une tâche d’exécution normale.

Le mode program utilise program, host et port pour un hôte TCP DAP déjà à l’écoute. Aucun Runtime local n’est requis et les paramètres de lancement, cwd et env ne sont pas injectés. Ce mode charge un programme dans l’hôte, sans s’attacher à une VM/PID en cours d’exécution. Les chemins doivent être valides des deux côtés. Les points d’arrêt de fonction, d’exception, de journalisation ou de compteur, l’exécution inverse, les inlay hints et CodeLens ne sont pas pris en charge.

**VeroScript : Attacher à un hôte** est disponible dans la palette et le menu contextuel des fichiers .vs. La commande utilise le script enregistré sélectionné/actif, ou propose un sélecteur de fichier, puis demande l’adresse et le port (127.0.0.1:4711 par défaut). Annuler une saisie ne lance aucune session. Les sources concernées sont vérifiées avant le démarrage, avec arrêt au point d’entrée. Saisissez IPv6 sans crochets. F5 et la complétion launch.json proposent aussi un modèle attach ; enregistrez une configuration pour fixer le script, les arguments et les options explicites.

Connectez-vous au point TCP DAP fourni par l’application hôte. Pour l’édition, utilisez ses `bindingCatalogPaths`, imports, macros et version de bibliothèque standard. Les métadonnées ne rendent pas les API hôte exécutables dans un Runtime local. Définissez les remplacements de compilation dans `launch.json` si nécessaire et enregistrez les dépendances hors des chemins configurés. L’adresse est résolue sur l’hôte de l’extension, y compris à distance. Utilisez le débogueur natif pour le code natif ; ce plugin débogue les scripts.

### Attachement au domaine actif

Utilisez **VeroScript : Attacher à un domaine** ou son modèle launch.json : request attach, attachMode domain, host/port et domainId attendu facultatif. Vide ou absent, domainId sélectionne le domaine du point de connexion. Aucun fichier ouvert ni Runtime local n’est requis. Ce mode n’enregistre, ne charge ni ne redémarre les scripts ; program, les remplacements de compilation/exécution et debugServer sont refusés. Plusieurs appels hôte peuvent partager une connexion. Quand l’hôte termine la session de débogage, le plugin achève la déconnexion sans demander l’annulation du script.

Le point hôte doit prendre en charge l’attachement à un domaine actif ; sinon la connexion échoue sans charger de programme. Le délai initial de connexion et d’initialisation est de 10 secondes. Vérifiez les identités et révisions dans la sortie. Les révisions sont opaques : comparez manuellement les sources et métadonnées API, sans comparaison automatique ni synchronisation LSP. La déconnexion laisse l’exécution et l’annulation à l’hôte. Une perte de connexion termine le débogage sans reconnexion automatique. Une VM déjà en JIT ne peut pas être reprise.

Pause, pas à pas, évaluation, points conditionnels et modification des variables dépendent des fonctions de l’hôte. Une requête non prise en charge signale une erreur. Les points ordinaires restent disponibles. Arrêter/déconnecter termine la connexion de débogage ; demandez à l’application hôte d’annuler le script si nécessaire.


## Module JIT optionnel

Runtime et les backends JIT sont distribués séparément. veroscript.run.jitBackendPath permet de fixer un fichier de bibliothèque ou répertoire compatible pour l’exécution normale. Vide, il omet le chemin CLI et hérite du choix de l’environnement de modules : sans choix configuré, la valeur par défaut est auto. Si JIT est désactivé ou sans candidat automatique utilisable, mixed utilise VM et jit forcé échoue avant l’exécution. Une tâche peut remplacer jitBackendPath ; une chaîne vide efface la sélection héritée. Les chemins relatifs partent de l’espace de travail ou du dossier source, indépendamment de cwd, et acceptent les variables de chemin d’exécution. Vérification, LSP et débogage VM local ne chargent pas ce module ; launch/attach refusent jitBackendPath. Un domaine utilise le choix de son hôte. Le chargeur natif gère les répertoires et l’ABI ; jitCapability de LSP/DAP décrit le contrat du cœur, pas un moteur installé.

## Types mémoire et inspection de débogage

Avec un Runtime compatible, les outils natifs fournissent la complétion, le survol et l’aide aux signatures pour les types et appels génériques `std::memory`. Le panneau Variables permet de développer les champs d’inspection des buffers et vues. Ces champs sont en lecture seule et leurs références expirent après la reprise ou un pas. Les métadonnées mémoire de l’hôte sont transmises sans modification ; les requêtes arbitraires de lecture/écriture mémoire et un éditeur mémoire dédié ne sont pas pris en charge.

## Outils actuels et sources de modules

Les chaînes brutes `r"..."` acceptent plusieurs lignes ; les antislashs sont littéraux et le guillemet suivant ferme la chaîne. Coloration et filtrage des modèles suivent ces règles sans Runtime. Les changements des bibliothèques `.vslib`, y compris les extensions en majuscules, sont transmis au serveur.

Si le serveur annonce un environnement de modules et sa méthode source, la navigation ouvre les documents `veroscript-module:` comme instantanés en lecture seule. Les mêmes chemins natifs restent distincts entre espaces de travail. Après un redémarrage ou un changement d’instantané de compilation, accédez-y de nouveau depuis le fichier source d’origine ; les anciennes vues conservent leur texte. Ces vues affichent les sources et leur coloration, sans édition ni requêtes linguistiques supplémentaires dans l’instantané.

Les processeurs natifs de compilation/annotation et les environnements de modules doivent être fournis par l’hôte LSP/DAP. Les serveurs Runtime par défaut n’importent automatiquement ni les processeurs de l’application hôte ni l’environnement de paquets CLI. `bindingCatalogPaths` fournit des métadonnées, dont le catalogue du service de compilation exporté pour créer une extension ; il n’installe ni rappels ni processeurs. Utilisez des services hôtes correspondants lorsque le projet en dépend.

Les noms d’annotations suivent la politique `caseSensitive` du catalogue ou de l’hôte ; la comparaison insensible à la casse ne concerne que l’ASCII. Le plugin conserve les noms canoniques et les libellés des paramètres natifs ; les noms des arguments restent sensibles à la casse. Modifier un fichier `bindingCatalogPaths` recharge ses métadonnées sans redémarrer le serveur. Organiser les imports applique les modifications précises du serveur et conserve commentaires, chaînes brutes, branches inactives et fins de ligne.

## Accolades et survol des types

Dans un bloc vide de déclaration ou de contrôle, Entrée place l’accolade ouvrante sur sa propre ligne et indente le curseur, même pendant la saisie de `void a{}`. Les accolades déjà séparées, commentaires, chaînes et initialisations ambiguës utilisent Entrée normalement. L’indentation, les fins de ligne, l’annulation et les curseurs multiples compatibles suivent VS Code ; des contextes mixtes utilisent Entrée normalement. Les suggestions et extraits conservent leurs raccourcis. Désactivez `veroscript.bracesOnNewLine` pour conserver Entrée partout. Ce comportement local fonctionne sans Runtime et ne change pas le formatage du document. Le survol d’un type intégré réunit sa catégorie localisée et son nom dans un titre, en conservant la description.

L’option est activée par défaut et se règle dans les paramètres Utilisateur ou Espace de travail. Les déclarations sur plusieurs lignes et les listes d’initialisation des constructeurs utilisent Entrée normalement pour préserver l’indentation de la déclaration.

Les fichiers VeroScript proposent une icône V compacte pour les thèmes clairs et sombres dans les onglets et l’Explorateur. Le thème d’icônes de fichiers choisi contrôle son affichage et peut fournir sa propre icône ; l’extension ne change pas votre thème.

Les types primitifs et conteneurs intégrés, comme int et array, utilisent le même titre compact. Les descriptions et métadonnées intégrées des complétions sont du texte ; les déclarations réelles conservent les blocs de code VeroScript. Les alias du code source, surcharges, liens et exemples sont conservés.

Les modèles intégrés restent masqués dans les directives de préprocesseur et les commentaires de ligne prolongés par une barre oblique inverse, puis réapparaissent à leur fin. Ces commentaires conservent aussi leur coloration et le comportement normal de la touche Entrée.

Avec des outils de langage compatibles, la complétion dans une classe ou une structure propose les implémentations admissibles de méthodes virtuelles héritées et de membres d’interface. À un point d’insertion vide, refactor.rewrite propose l’action d’implémentation correspondante. L’extension préserve les signatures natives, les substitutions génériques, les plages d’insertion et les corps modifiables des extraits ; le serveur filtre les membres déjà implémentés, final ou inaccessibles.

La navigation par réexportation préserve aussi les cibles des dépendances transitives et ouvre le code source de module en lecture seule fourni par le serveur propriétaire.

## Valeurs et modification des variables

Avec un Runtime ou un hôte compatible, VS Code utilise le champ DAP `presentationHint.attributes` de chaque variable pour signaler les valeurs en lecture seule, y compris les enfants d’un `variant` immuable, les valeurs imbriquées et les résultats de surveillance. L’extension conserve ces indications indépendamment de la capacité de modification de la session. Le débogueur natif valide les écritures ; l’extension ne déduit pas les indications absentes.

Les types numériques tels que `std::bigint`, `std::rational` et `std::numeric::rounding_mode` conservent leur nom natif dans les résultats du service de langage. Les valeurs du débogueur restent des textes natifs, sans conversion en nombres JavaScript. Les API disponibles dépendent du Runtime et de sa bibliothèque standard correspondante.

## Compagnon GUI

L’[extension VeroScript GUI](../../../../../VeroScriptGuiVSCode/Resources/Docs/User/fr-FR/index.md), optionnelle, ajoute `.vswml`/`.vswcss` et les tâches natives explicites de vérification, compilation et aperçu. Elle dépend de l’API outils 1 ; installez les versions locales correspondantes ensemble. Runtime GUI et fournisseurs sont séparés ; `.vs`, LSP et DAP restent ici.

Conservez le Runtime extrait au complet, y compris le module interpréteur `Vm/` et ses dépendances. CLI, LSP et DAP utilisent la VM assemblée par défaut du Runtime ; `run.backend: "vm"` sélectionne une politique d’exécution. Déplacez ou mettez à jour ensemble les fichiers correspondants, puis redémarrez le serveur de langage.

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
