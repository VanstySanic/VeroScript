# Développer VeroScript GUI pour VS Code

## Architecture

La validation GUI native exige des versions cohérentes de Core, Tooling, VM et des fournisseurs ; une même version produit ne garantit pas la compatibilité ABI. Vérifiez séparément les métadonnées de l’éditeur, le contrôle natif, la compilation du conteneur applicatif et l’aperçu avec un nombre limité de trames.

Le test des artefacts valide le `.vsapp` avec `gui-check` et ses fournisseurs, avec `--check` seul, puis avec `--check`, l’environnement du projet source et ses arguments de compilation. Un chemin explicite de bibliothèque standard est combiné avec `--path` pour couvrir la déduplication native de ses racines. Le délai global couvre les tâches et les commandes de validation ; chaque commande garde son propre délai. Régénérez les environnements de test installés quand la cohorte Core/ABI native change.

Runtime, LSP et DAP restent dans l’extension principale. Le compagnon active sa dépendance et utilise l’API 1 de `Public/api.d.ts` uniquement pour une exécution explicite. L’URI source résout les variables, le dossier propriétaire choisit la configuration. Édition et énumération automatique ne lancent pas d’outils natifs ; la résolution d’une tâche configurée peut interroger la version CLI. Les sources natives restent indépendantes.

Les modèles sont mis en cache par version ; l’analyse est bornée. Annulation ou modification invalident les résultats asynchrones de métadonnées et chemins. Caches et observateurs sont isolés par configuration résolue. Respecter la casse et les alias Mark, omettre les alias ambigus et visiter une seule fois les ancêtres partagés. Les indications ne remplacent pas la sémantique native.

## Construction et validation

Node.js 22.18+ et VS Code bureau 1.106+ sont requis. Exécuter chaque bloc dans son module. Installer les dépendances des deux modules, car les déclarations partagées référencent les types SDK principaux. Les tests de développement chargent les deux extensions ; les tests VSIX installent les deux paquets locaux dans un profil isolé. Ne pas chercher cette dépendance non publiée sur Marketplace.

```sh
# VeroScriptVSCode
npm ci
npm run typecheck
npm run package
# VeroScriptGuiVSCode
npm ci
npm run typecheck
npm run test:unit
npm run test:integration
npm run package
npm run package:contents
node Resources/Tools/test.mjs --packaged --skip-build
```

Exécuter les suites Extension Host de bureau une par une. Des fenêtres de test VS Code simultanées peuvent se disputer le focus nécessaire à la validation des complétions, à l’annulation et aux cas de touche Entrée.

L’acceptation native accorde 600 secondes par tâche de vérification, compilation ou aperçu et adapte uniquement le délai global de ce test à la séquence. Les fournisseurs de l’éditeur conservent leurs délais courts. Une commande GUI explicite peut compiler tout le framework à froid et prendre plus d’une minute. Les tâches du produit restent asynchrones et peuvent être arrêtées dans VS Code ; un code de sortie non nul, un résultat absent ou un dépassement du budget de test fait toujours échouer l’acceptation.

Les tests unitaires couvrent structure, positions UTF-16, CSS, métadonnées, ambiguïtés/cycles, ordre CLI et TextMate. Extension Host couvre dépendances, langues, complétion, survol, liens, balises, diagnostics et absence de processus spontané. Définir `VEROSCRIPT_TEST_RUNTIME` vers le Runtime GUI correspondant, puis `VEROSCRIPT_GUI_TEST_NATIVE=1` pour exiger vérification/compilation natives. Ajouter `VEROSCRIPT_GUI_TEST_PREVIEW=1` sur un bureau équipé des fournisseurs compatibles pour un aperçu de deux images. Valider séparément Windows/Linux/macOS, chemins Unicode/espaces, mode sans dossier, espaces multi-racines, hôtes distants et vraie fenêtre native. Un test optionnel ignoré n’est pas une validation native.

`VEROSCRIPT_TEST_DIRECTORY` choisit éventuellement un parent absolu pour les espaces et profils de test isolés, comme dans l’extension principale. Pour valider un environnement de paquets natif, placez-le dans le projet sélectionné par `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH`, afin que les sources GUI générées appartiennent à cet environnement. Un dossier temporaire indépendant peut être rejeté par les contrôles natifs d’appartenance ; ne pas élargir les accès du compilateur pour contourner cette incohérence du test.

Identifier le snapshot source ou de distribution du Runtime à chaque validation native ; aucun ancien rapport ne valide les nouvelles sources. Un délai CLI dépassé est un échec de validation. Ne pas lancer automatiquement l’aperçu à l’édition.

## Métadonnées éditeur

Enregistrer l’exemple sous `demo.veroscript-gui.json`, puis l’ajouter à `veroscriptGui.schemaFiles`. Il fournit uniquement des indications. L’hôte doit enregistrer les types réels et processeurs. Un schéma ultérieur remplace le type complet de même nom canonique ; fournir tous ses propres membres. Les bases utilisent leur nom canonique ; `routedEvents` ajoute éventuellement les événements communs.

```json
{
	"schemaVersion": 1,
	"types": [{
		"name": "Demo::Card",
		"mark": "card",
		"bases": ["VS::Gui::Widget"],
		"properties": [{ "name": "Title", "mark": "title", "type": "string", "bindable": true }],
		"events": [{ "name": "Selected", "mark": "selected", "type": "std::multicast_delegate<void()>" }]
	}]
}
```

Les métadonnées intégrées suivent les imports locaux exportés depuis l’entrée native `VeroScriptGui.vs` ; `builtin.json` répertorie les sources. Les widgets annotés sans alias `Mark` sont conservés pour compléter l’héritage des contrôles composites. `npm run schema:update` actualise le snapshot ; contrôler les littéraux natifs et les indicateurs style/liaison. La construction exige un snapshot à jour et rejette types doublés ou bases absentes. Les régressions unitaires et Extension Host couvrent balises composites, propriétés/événements hérités, placement, CSS et couleurs de sélection. Cette validation ne valide pas l’exécution native. Les ancêtres qualifiés `VS::Gui::Internal` sont conservés pour les indications héritées après réorganisation des espaces de noms natifs.

L’extraction accepte les espaces du langage entre `@` et le nom d’annotation, notamment `@GuiWidget` et `@ GuiWidget`, avec les mêmes propriétés, événements et relations d’héritage. Après un reformatage des sources, relancez `npm run schema:check`. Utilisez `npm run schema:update` si les métadonnées extraites changent ; `builtin.json` contient les chemins source et les métadonnées, pas les empreintes du contenu source.

## Distribution

Le paquet fonctionnel contient bundle, manifestes, traductions, grammaires, schémas, icônes et notices MIT, sans binaire natif, outils, tests ni documentation développeur. Aucune dépendance JavaScript tierce à l’exécution GUI actuellement ; en ajouter exige de revoir les notices. LICENSE doit correspondre au dépôt. Maintenir les six langues. L’empaquetage VSIX autonome reste disponible ; l’empaquetage IDE agrégé enregistre GuiVSCode séparément, construit sa dépendance principale et valide les deux VSIX locaux ensemble.

La revue statique ajoute des cas pour fin de fichier/balises, dimensions numériques, affectations espacées, doublons après guillemet, commentaires XML intégrés et plans vides. Les URL CSS sont lues une fois dans le modèle de style mis en cache, avec casse et échappements natifs ; les liens réutilisent ces plages. La préparation d’une tâche configurée peut interroger la version CLI ; édition et découverte automatique ne lancent pas de commande GUI. La validation native obtient les tâches via le fournisseur enregistré dans VS Code, pour tester réellement l’extension installée. Si nécessaire, `VEROSCRIPT_GUI_TEST_ENVIRONMENT_PATH` désigne un environnement de paquets préparé par son chemin absolu. Les cas de régression couvrent aussi les attributs des balises personnalisées portant un nom de membre du prototype JavaScript.  Le profil isolé désactive les suggestions de mots pour éviter de fausser les résultats et utilise une commande de fermeture prise en charge. La validation native exige un Runtime correspondant et la réussite du contrôle, de la compilation et de l’aperçu.

Voir [Maintenance de l’intégration des éditeurs](../../../../../../../../Resources/Docs/Developer/fr-FR/editors.md#acceptance-evidence).
Utiliser le publisher déclaré dans `package.json` ; la dépendance GUI doit correspondre à l’identité de l’extension principale. Inclure le README racine comme page Détails, sans liens vers les fichiers exclus du dépôt. Seul `extension/readme.md` fait exception au filtre documentaire du paquet fonctionnel. Publier sur Marketplace exige l’accès à ce publisher enregistré et la validation complète ; modifier le manifeste ne crée pas le publisher.

La construction copie le [PNG produit partagé de 256 px](../../../../../../../../Resources/Assets/icons/veroscript-icon-256.png) comme icône de l’extension.


[fr-FR](../../User/fr-FR/index.md) · [Documentation](../../README.md)
