# VeroScript GUI dans VS Code

Installez **VeroScript**, puis **VeroScript GUI**. Les versions locales doivent fournir l’API outils 1. Le compagnon gère `.vswml` et `.vswcss` ; l’extension principale gère `.vs`, LSP, DAP, Runtime et les options de compilation. Les couleurs suivent le thème de VS Code.

L’éditeur propose widgets, propriétés, `bind:`, `on:`, valeurs énumérées, chemins, survol, plan, pliage, paires de balises et couleurs hexadécimales à six/huit chiffres. Noms et alias Mark respectent la casse ; `bind:`/`on:` ne nécessitent pas `xmlns`. VSWCSS suit le sous-ensemble natif. Les messages structurels sont informatifs : le compilateur natif reste la référence. Le script intégré est coloré, sans LSP GUI ni analyse sémantique des expressions.

Les indications intégrées couvrent ListView/ListItem, TreeView/TreeItem, TableView/TableColumn/TableRow, ComboBox, TabView/TabItem, Menu/MenuItem et Dialog, avec propriétés et événements hérités. SelectionColor / selection-color permet de modifier les couleurs dans le balisage ; Popup Placement propose below, right et center. L’exécution exige des fournisseurs GUI et bibliothèques de scripts reconstruits ensemble ; ne pas mélanger d’anciens binaires 0.1.1 avec la nouvelle disposition native. [framework GUI](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/fr-FR/Reference/Framework.md).

Utilisez les commandes **VeroScript GUI** de vérification, compilation, aperçu, sélection du point d’entrée et affichage de la sortie. L’aperçu lance une application native. Configurez `veroscript.runtimePath` avec un Runtime compatible, compilé avec GUI et scripting GUI, et les fournisseurs natifs nécessaires. L’édition ne lance aucun processus natif.

Une commande GUI explicite peut compiler le framework complet à froid et prendre plus d’une minute. La tâche est asynchrone ; les commandes de tâches de VS Code permettent de l’arrêter.

## Configuration de l’espace de travail

`veroscriptGui.entry` choisit `.vs`/`.vswml` ou `.vsapp` pour vérification/aperçu. Vide : fichier sélectionné ; pour un style, fichier `.vswml` de même nom. Choisissez une entrée explicite pour les styles partagés. `assets` incorpore des fichiers à la compilation, `packageName` nomme le paquet, `schemaFiles` ajoute des indications. Les tableaux remplacent les valeurs par défaut.

`backends` accepte `platform`, `graphics`, `gui`, `image`, `font` et `text-layout`. Une valeur omise ou vide dans l’extension hérite de l’environnement natif ; un identifiant de backend le sélectionne explicitement, `auto` demande la sélection native des candidats et `none` désactive ce service. Désactiver un service requis pour l’aperçu produit une erreur native. `environmentPath` charge explicitement un environnement CLI avec `--path`, après les choix de moteurs. Il ne configure pas LSP/DAP. Arguments, environnement, répertoire et compilation héritent de l’extension principale. Vérification/compilation utilisent VM sans module JIT ; l’aperçu hérite du moteur d’exécution. `previewFrames: 0` omet la limite ; un entier positif sûr est réservé aux aperçus `.vswml`. Les entrées `.vs` gèrent leur boucle.

La fabrique Context native standard utilise `rmlui` uniquement si l’argument du constructeur, le choix de l’invocation et les valeurs par défaut de l’application/environnement restent non spécifiés ; `auto` et `none` explicites sont préservés. Un réglage vide dans l’extension omet la surcharge, tandis qu’une valeur de backend explicitement vide enregistrée dans l’environnement natif désactive le service. Les options d’exécution VM/JIT restent dans l’extension principale et ne sont pas des rôles de service GUI.

Chemins et processus sont résolus sur l’hôte d’extension de l’espace de travail, y compris à distance. Les chemins relatifs partent du dossier propriétaire ou, sans dossier, du répertoire du fichier choisi. Variables : `${workspaceFolder}`, `${file}`, `${fileDirname}`, `${env:NAME}` ; elles exigent leur contexte respectif. Les ressources des documents partent du fichier déclarant. Les identités opaques ne sont pas transformées en chemins.

## Tâches et métadonnées

Une tâche contient `type: "veroscript-gui"`, `action: "check" | "compile" | "preview"`, `input`, puis `output` différent et terminé par `.vsapp` pour compiler. Champs optionnels : `assets`, `name`, `frames`, `args`, `backends`, `environmentPath`. Ils remplacent les réglages correspondants ; les tableaux remplacent les valeurs héritées. `args` concerne l’aperçu. Les commandes proposent d’enregistrer les sources modifiées ; les tâches suivent la politique d’enregistrement de VS Code. Enregistrez séparément les dépendances hors périmètre. Les erreurs natives indiquent fichier et ligne, sans confondre colonne d’octets UTF-8 et UTF-16.

Ajoutez des fichiers `*.veroscript-gui.json` à `schemaFiles` pour étendre les indications avec validation JSON. Format : `schemaVersion: 1`, `types` ; chaque type a `name`, puis éventuellement `mark`, `bases`, `properties`, `events`. Chaque membre a `name`, `type`, puis éventuellement `mark`, `bindable`, `style`, `values`, `description`. Ces données n’enregistrent ni liaisons exécutables ni processeurs ; les alias ambigus sont supprimés.

```json
{
	"schemaVersion": 1,
	"types": [
		{
			"name": "Demo::Card",
			"mark": "card",
			"bases": ["VS::Gui::Widget"],
			"properties": [
				{ "name": "Title", "mark": "title", "type": "string", "bindable": true }
			]
		}
	]
}
```

La sémantique et le débogage GUI de `.vs` nécessitent un hôte et un catalogue compatibles via l’extension principale. Aucun démarrage automatique de processeurs GUI, débogueur GUI ou concepteur visuel. L’analyse locale s’arrête au-delà de 2 Mi unités UTF-16.

La complétion conserve les en-têtes et déclarations CSS inachevés en fin de fichier, les espaces avant `=`/`:` et remplace les dimensions numériques entières. Un en-tête fermé ne propose pas d’attributs ; un sélecteur classe/ID ne propose pas de types de widgets. Les liens URL suivent la casse et les échappements natifs. Les commentaires XML des corps script/style ne brisent pas leur structure.

La compilation GUI exige la prise en charge native Application et produit le conteneur `.vsapp` standard. Le même fichier convient aux tâches GUI et aux commandes CLI générales inspect, de vérification et d’exécution, avec un hôte GUI et les fournisseurs requis. Recompilez les anciens artefacts GUI `.vsapp` bruts. Les backends restent explicitement choisis ; l’extension ne sélectionne pas automatiquement RmlUi.

Utilisez un Runtime GUI complet et compatible, avec son module `Vm/`. La vérification et la compilation GUI peuvent exécuter des processeurs de compilation écrits en script : elles nécessitent la VM du Runtime et les fournisseurs GUI sélectionnés. Les fournisseurs restent sélectionnés explicitement ; les métadonnées de l’éditeur décrivent les déclarations éditables, pas l’implémentation des notifications.

La CLI ordinaire peut vérifier le `.vsapp` généré comme conteneur autonome avec `--check`, ou sélectionner un environnement de paquets compatible avec `--path`. La vérification GUI sélectionne cet environnement avec `environmentPath`. Utilisez le Runtime corrigé et un environnement installé compatible ; après une mise à jour de Core ou de l’ABI, régénérez les environnements ciblant une compilation antérieure.

<!-- docs-navigation -->

---

[VeroScript](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
