# Documentation développeur — VS Code

Les responsabilités communes éditeur/natif et la maintenance des six langues sont décrites dans [Intégration commune des éditeurs](../../../../../../../../Resources/Docs/Developer/fr-FR/editors.md).

Utilisez Core, Tooling, VM, JIT et les fournisseurs GUI issus d’une configuration native cohérente, y compris le mode C++ ; une version produit identique ne prouve pas la compatibilité ABI. Les tests SDK vérifient la complétion et le survol des récepteurs génériques statiques, handle et de types imbriqués. Les instantanés facultatifs `GetLastJitDiagnostics` appartiennent à l’API hôte C++ et ne sont pas exposés par le protocole LSP/DAP actuel.

Utilisez `vscode.l10n.t` avec `Resources/Localization/Bundles` pour les messages UI. Les sources `Resources/Localization/<locale>/manifest.json` génèrent les fichiers `package.nls`. Préservez les indices des arguments et les références du manifeste.

Utilisez Node.js 22.18.0+ et VS Code bureau 1.106.0+. Exécutez ces commandes dans le module. Tests unitaires : configuration et UI ; intégration : Electron/VS Code bureau et Runtime actuel. Les fixtures domaine n’exécutent pas la VM. Vérifiez package:contents et licences VSIX ; validation native, hôtes distants et domaines réels nécessitent leurs rapports.

```sh
npm ci
npm run typecheck
npm run build
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
```

Exécuter les suites Extension Host de bureau une par une. Des fenêtres de test VS Code simultanées peuvent se disputer le focus nécessaire à la validation des complétions, à l’annulation et aux cas de touche Entrée.

Les tests natifs des arguments accordent 180 secondes par appel CLI et évitent les macros hôte inutilisées, pour isoler le transfert des arguments de la compilation GUI complète à froid. Les tests dédiés de configuration/protocole couvrent les macros ; la compilation GUI réelle possède ses propres validations. Ces budgets ne modifient pas les délais LSP ou de débogage. Normaliser les chemins du module JIT avant de comparer les arguments du processus.

[Standards](../../../../../../../../Resources/Docs/Standards/fr-FR/GENERAL.md)



## Intégration hôte et protocole domain

Les hôtes C++, UE ou autres doivent fournir TCP DAP et les vraies fonctions natives. RunDapTcp dans l’exemple d’intégration utilise VeroScriptDapTcp::Run et Stdio.LiveBindings. L’hôte gère les durées de vie, les threads et l’écoute après déconnexion. Le plugin débogue la VM du script ; le code natif utilise son propre débogueur. Le mode domain exige un pont préparé par l’hôte ; une VM déjà en JIT ne peut pas être reprise. Configurez bindingCatalogPaths pour l’édition et alignez imports, macros et bibliothèque standard ; les métadonnées ne chargent pas les callbacks natifs dans un Runtime local. La commande laisse les options de compilation à l’hôte ; les remplacements sont explicites dans launch.json. Enregistrez vous-même les dépendances privées hors des chemins configurés. L’adresse est résolue sur l’hôte de l’extension, y compris à distance.

L’hôte doit fournir IVeroScriptLiveDebugDomain et annoncer veroscriptSupportsDomainAttach: true dans initialize. Sans cette déclaration, la connexion échoue sans repli vers program. Le délai initial est de 10 secondes. La sortie contient les identités et révisions opaques ; aucune comparaison automatique des sources ni synchronisation LSP n’est fournie. L’édition suit veroscriptCanSetVariable ; pause/édition non prises en charge renvoient une erreur. Disconnect envoie terminateDebuggee: false ; une perte de connexion termine la session sans reconnexion automatique. L’hôte gère l’annulation, les durées de vie et les points sûrs, prépare le pont avant exécution et sérialise les appels débogués dans le contexte actif unique. L’intégration UE reste à valider.

Les indicateurs de l’hôte contrôlent aussi le pas à pas, l’évaluation des expressions et les points d’arrêt conditionnels ; un indicateur absent signifie non pris en charge. Le plugin actualise les capacités VS Code disponibles et refuse explicitement les requêtes non prises en charge. Les points d’arrêt ordinaires restent disponibles. Arrêter/déconnecter laisse l’annulation à l’hôte, même si celui-ci déclare veroscriptCanTerminate. Après un événement `terminated` de l’hôte, le client termine l’échange `disconnect` avec `terminateDebuggee: false`. Les opérations en attente échouent une seule fois ; les réponses tardives et événements de fin répétés sont ignorés. Si le transport est fermé, la déconnexion finale est confirmée localement.

[DAP](../../../../../../../../VeroScriptTooling/Resources/Docs/Developer/fr-FR/workspace-and-debug.md)

## Contrat d’inspection mémoire

Conserver les champs DAP `memoryReference` et `veroMemory` comme chaînes décimales opaques, y compris au-delà des entiers sûrs de JavaScript. Les enfants d’inspection sont en lecture seule et leurs références sont limitées à l’arrêt courant. Transmettre les capacités de l’hôte ; les métadonnées n’impliquent pas la prise en charge de `readMemory`/`writeMemory`. Les tests d’intégration couvrent l’édition, l’exécution, la vérification et le débogage mémoire natifs ainsi que le transfert des métadonnées de domaine ; les fixtures de protocole ne valident pas une VM hôte réelle.

## Ordonnancement du client et DAP bidirectionnel

Une rafale d’événements de l’éditeur partage une seule synchronisation de l’état courant des documents. Les événements reçus pendant une passe et les redémarrages explicites restent en attente pour la suivante, même après un échec. Le DAP de domaine injecte des événements locaux et renumérote les messages sortants ; les réponses aux requêtes inverses doivent restaurer le `request_seq` d’origine de l’hôte, même hors ordre. Les fixtures vérifient cette correspondance sans lancer de terminal ni de session de débogage enfant.

## Intégration des sources de modules natifs

`ModuleSources` convertit les URI à la frontière du client et appelle le `moduleSourceMethod` annoncé. Les URI de l’éditeur conservent l’URI protocolaire d’origine, la durée de vie du serveur et l’empreinte de compilation ; seule l’URI d’origine est renvoyée. Les vues en lecture seule ne deviennent ni overlays ni cibles modifiables. Rejeter les instantanés absents, retirés, annulés ou modifiés, y compris pendant l’attente. Symboles et diagnostics restent associés au client propriétaire. Un redémarrage automatique crée une nouvelle durée de vie des sources, même si le client et l’empreinte de compilation restent identiques.

Définir `VEROSCRIPT_TEST_MODULE_LSP` avec le chemin absolu de `VeroScriptModuleEnvironmentToolingTests` pour exécuter la fixture native `--lsp` dans Extension Host. Sans cette fixture, le test est ignoré et ne valide pas l’environnement natif. Les serveurs Runtime conservent leurs options natives : aucun processeur natif n’est configuré par JSON et aucun environnement CLI n’est injecté automatiquement. `sourceReference` DAP et les sources figées suivent le traitement standard de VS Code.

Le serveur natif gère la comparaison des noms d’annotations, leur orthographe canonique et les paramètres des signatures, y compris les libellés multilignes, génériques imbriqués et littéraux contenant des virgules. Transmettre ces résultats via le SDK. Les tests modifient `caseSensitive` dans le même catalogue sans remplacer le client, puis appliquent les éditions `source.organizeImports` dans VS Code en vérifiant les plages exactes, CRLF et l’idempotence.

## Présentation des variables et types numériques exacts

Conserver l’objet `presentationHint` complet de chaque variable DAP dans le SDK et le pont de domaine, notamment `attributes: ["readOnly"]`. `supportsSetVariable` et `veroscriptCanSetVariable` décrivent les opérations de session, sans rendre toutes les variables modifiables. La validation native reste décisive. Les cas de régression couvrent les enfants d’un `variant` immuable, les valeurs imbriquées, les enfants de surveillance et les variables locales modifiables, ainsi que la transmission des indications avec les deux valeurs de la capacité de modification du domaine.

Les types numériques exacts conservent leurs noms natifs et leurs valeurs textuelles. Les cas TextMate contextuels couvrent `std::bigint`, `std::rational` et `std::numeric::rounding_mode` sans réserver leurs noms terminaux comme mots-clés. Les cas ajoutés ou modifiés restent non validés avant la vérification des types et les tests Extension Host/VSIX réels avec les outils correspondants ; les anciens rapports ne valident pas les nouvelles sources natives.

## API outils pour compagnons

`Public/api.d.ts` définit l’API 1. `getToolchain(scopeUri, sourceFile?)` résout Runtime, options de compilation, environnement, moteur/arguments et périmètres sources. L’URI choisit les réglages, le fichier optionnel développe les variables de fichier. La résolution peut sonder la version CLI : réserver aux exécutions explicites. `compileArguments` ne contient ni commande, ni entrée, ni moteur d’exécution. GUI vérification/compilation utilisent VM sans module JIT. Distribuer `Public` et conserver les exports `ready`/`snapshot` des tests. Valider cette API dans un Extension Host réel et un VSIX installé avec le Runtime correspondant ; l’exécution GUI native a ses propres contrôles.

L’interpréteur appartient au module BytecodeVm séparé. Les fixtures déplacées doivent conserver le Runtime complet, dont `Vm/`. Le démarrage LSP/DAP natif fournit sa fabrique par défaut ; l’éditeur n’ajoute aucun champ de protocole et ne déduit pas la fabrique d’un hôte personnalisé. Les régressions SDK couvrent survol, complétion, signatures et définitions des fonctions capturées, les plages UTF-16 et le survol du récepteur courant après une modification incomplète.

Lors du déplacement de la fixture de modules natifs, définissez aussi `VEROSCRIPT_TEST_VM_BACKEND` avec le chemin absolu du module VM natif compatible dans le répertoire `Vm/` du Runtime. Ce réglage remplace le chemin VM enregistré à la compilation de la fixture ; les programmes LSP/DAP ordinaires du Runtime conservent leur fabrique native par défaut.


Utiliser le publisher déclaré dans `package.json` ; la dépendance GUI doit correspondre à l’identité de l’extension principale. Inclure le README racine comme page Détails, sans liens vers les fichiers exclus du dépôt. Seul `extension/readme.md` fait exception au filtre documentaire du paquet fonctionnel. Publier sur Marketplace exige l’accès à ce publisher enregistré et la validation complète ; modifier le manifeste ne crée pas le publisher.

<!-- docs-navigation -->

---

[Documentation développeur](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
