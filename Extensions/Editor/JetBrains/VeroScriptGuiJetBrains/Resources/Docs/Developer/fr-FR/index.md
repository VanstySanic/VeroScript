# Développement de l’extension GUI pour Rider

## Propriété et dépendance

Le [descripteur de plugin](../../../META-INF/plugin.xml) définit l’identité et la dépendance obligatoire au plugin principal VeroScript. Celui-ci fournit Runtime, réglages du compilateur et coloration lexicale VS ; ses classes ne sont pas remballées. La [dépendance de projet localPlugin de Gradle](https://plugins.jetbrains.com/docs/intellij/tools-intellij-platform-gradle-plugin-dependencies-extension.html#local-plugin) sert au développement, aux tests et au bac à sable IDE. Les deux ZIP d’installation restent distincts.

Utilisez JDK/JVM 25, Gradle 9.5.0, Kotlin 2.4.0 et le plugin Gradle IntelliJ
Platform 2.19.0. SDK Rider 2026.2.2, minimum `262.10315.191`, limite stricte
`262.*`. Toute autre branche exige une vérification distincte. Aucun outil natif
ni fournisseur n’est inclus. MIT est embarqué dans `META-INF/LICENSE` ; les
mentions originales du Wrapper sont sous `Resources/Licenses`.

## Architecture et contrats

`gui-compile` produit le conteneur d’application standard `.vsapp`. Check et Preview
le transmettent au CLI natif, qui lit le conteneur et le bytecode Core.
Utilisez ensemble le Runtime, les bibliothèques VM et les fournisseurs GUI compatibles.

- `language/` fournit des lexeurs distincts sensibles à la casse et un PSI plat,
  sans inspections d’espaces de noms XML ni sémantique CSS de navigateur.
  Les blocs en ligne utilisent les lexeurs VS/GUI en couches.
- `GuiMarkupModel` met en cache les structures par fichier, sans typage. Aucune
  analyse récursive du projet, vérification native ou prévisualisation automatique
  pendant l’édition. Les aides de frappe sont omises au-delà de 1 MiB ou avec plusieurs curseurs.
- `Resources/Catalog/gui.json` contient les suggestions intégrées extraites des
  sources GUI. Régénérez avec `python Resources/Tools/update-catalog.py`, puis
  révisez le diff. Les empreintes signalent les changements. Le catalogue ne
  remplace pas `FGuiSchema`. Ne rejetez pas un Schema personnalisé absent de cette
  liste. Conservez Original/Mark, héritage, liaisons et événements.
  L’extraction suit les imports locaux de `VeroScriptGui.vs`, y compris les fichiers
  de support transitifs. Sans Mark, elle utilise le nom déclaré ; les limites de
  classe et le masquage des commentaires/littéraux préservent chaque déclaration.
  Le schéma 2 conserve le `qualifiedName` de chaque déclaration, y compris les
  portées `VS::Gui::Internal`. Recherche et documentation utilisent ce nom tout
  en conservant les noms courts et alias Mark. Générez et distribuez le catalogue
  avec son lecteur.
- `run/` hérite les réglages en conservant l’origine des chemins. Ordre CLI :
  options globales du compilateur/environnement dont `--path`, backends GUI,
  tâche `gui-*` et ses options, puis `--` et arguments du script. Préservez les
  répétitions de `--asset` et les limites `int64` du nombre d’images.
- Vérifier/compiler n’initialise pas les scripts. La prévisualisation possède
  un processus natif arrêté par Rider. Les ID de backend sont facultatifs.
  Aucun lancement DAP GUI n’est fourni ; l’attachement à un hôte préparé relève
  du plugin de base. Les liens de ligne ne confondent pas colonnes ML en points
  de code et décalages UTF-16.

L’extraction accepte les espaces du langage entre `@` et le nom d’annotation, notamment `@GuiWidget` et `@ GuiWidget`, avec les mêmes propriétés, événements et relations d’héritage. Une modification des empreintes source exige toujours une régénération et une revue du catalogue, même si les types exposés restent identiques.

## Vérification et publication

Chaque lexeur conserve les tokens de sa langue et respecte la plage de lecture,
même si un marqueur final est tronqué. Les blocs en ligne s’arrêtent à la prochaine
frontière raw/CDATA sans reparcourir le reste du fichier. Un cas de régression compte
les lectures de caractères pour contrôler le parcours linéaire sans seuil temporel.
La complétion consomme `=`/`:`, conserve
les valeurs existantes et remplace le nom entier avec Tab. Les `src` locaux
ordinaires utilisent `GuiStringLiteralManipulator` pour les renommages et éditions
de chemin : échappez les noms insérés sans fermer une citation inachevée.
Les chemins avec entités restent exclus de la navigation locale. Les chemins
absolus natifs utilisent la racine de l’OS et réécrivent tout le préfixe natif au
reliage, au lieu de la racine de projet du SDK. Les chemins relatifs gardent leur
base déclaratrice. Seuls les séparateurs natifs sont convertis ; les antislashs
des noms POSIX sont préservés. Le décodage conserve `?` dans les noms et la
normalisation NFC du SDK sous macOS. La conversion de chemins selon la racine de projet du SDK
est désactivée. Le reliage relatif conserve le comportement SDK : sans chemin
relatif possible, le texte reste inchangé. Les déplacements entre volumes/systèmes
de fichiers exigent une vérification distincte, sans promesse de migration automatique.
La documentation
rejette les instantanés modifiés et se limite aux tokens de propriété pour les styles.
Les actions effacent les options de modèle incompatibles avec la tâche/entrée et
conservent les sorties personnalisées applicables. Le vérificateur statique ne
parcourt que les entrées propres au module, sans caches SDK Gradle ni bacs à sable.

Depuis ce module :

```sh
python -B Resources/Tests/test_catalog.py
python -B Resources/Tools/check-static.py
./gradlew :test :verifyPluginProjectConfiguration :verifyPluginStructure :buildPlugin
./gradlew :veroscript:buildPlugin
./gradlew :verifyPlugin
```

Windows : `gradlew.bat` ; POSIX : `bash ./gradlew` au besoin.
`-PlocalIdePath=/absolute/path/to/Rider` choisit un SDK installé. Les tâches
racines explicites évitent de sélectionner tous les tests des sous-projets.
La sortie GUI est `VeroScriptGuiJetBrains-0.1.1.zip`, indépendante. L’empaquetage IDE
agrégé enregistre GuiJetBrains séparément, construit le plugin de base et vérifie
les deux ZIP locaux ensemble avec leurs versions respectives. Les commandes
ci-dessus restent disponibles pour les builds de développement autonomes.

Si le plugin de base n’est pas publié, `:verifyPlugin` seul ne peut pas résoudre
cette dépendance depuis Marketplace. Vérifiez les deux ZIP ensemble avec le CLI
Plugin Verifier officiel : placez leurs chemins absolus sur deux lignes de
`plugins.txt`, puis exécutez la commande suivante. Le plugin de base local sert
ainsi à résoudre la dépendance ; contrôlez les deux résultats avant publication.

```sh
java -jar /absolute/path/to/verifier-cli.jar check-plugin @/absolute/path/to/plugins.txt /absolute/path/to/Rider
```

Les tests couvrent redémarrage des lexeurs, saisie incomplète, héritage/restrictions
du catalogue, chemins déplaçables, ordre et séparation des arguments, entrées,
limites d’images, ressources/écrasement, persistance, enregistrement et six langues.
Avec un Runtime GUI réel, validez ML/VS/artefacts en Check/Compile/Preview,
déplacement, fournisseurs, chemins Unicode/espaces, arrêt et liens de diagnostic
sur chaque OS. Vérifiez dans Rider coloration en ligne, présélection, guillemets,
balises, navigation `src` et `.vs` ordinaires. Les fixtures SDK ne remplacent pas la recette UI.

Voir [Maintenance de l’intégration des éditeurs](../../../../../../../../Resources/Docs/Developer/fr-FR/editors.md#acceptance-evidence).

Les fixtures initialisent les solutions de dossier Rider par un module SDK réservé
aux tests et utilisent des fichiers locaux réels. ReSharper est désactivé uniquement
dans la JVM de test ; le contrôle d’altération des paramètres reste actif avec les
valeurs Rider initialisées comme référence.

L’interface Rider interactive, Preview/Stop avec une fenêtre réelle, la relocalisation,
les autres OS et l’intégration à un véritable hôte restent à valider séparément.
Ces résultats couvrent uniquement l’environnement testé.

Maintenez ensemble les six langues UI/User/Developer. Le manifeste est enregistré
au catalogue partagé ; exécutez les contrôles de publication lors des modifications documentaires.

[Guide utilisateur](../../User/fr-FR/index.md) · [Normes](../../../../../../../../Resources/Docs/Standards/fr-FR/GENERAL.md)

## Invocation de vérification/compilation et chemins enregistrés

`GuiCommand.arguments()` place `--backend=vm` en tête pour `check` et `compile`, avant que `--path` prépare un environnement de paquets, afin de supprimer la sélection JIT d’exécution pour ces tâches. L’aperçu n’émet aucune surcharge de backend d’exécution ou de JIT et conserve les valeurs natives. Les champs GUI restent des choix de services distincts. `GuiCommand.persist()` ne remplace que `java.io.File.separatorChar` par `/` dans les chemins relatifs au projet ; préservez les barres inverses littérales des noms POSIX. Conservez les régressions d’ordre des options, choix omis/auto/none/personnalisés après persistance et aller-retour des noms natifs. Vérifiez chaque OS cible ; Windows ne valide pas POSIX.

[Rider SDK](../../../../../VeroScriptJetBrains/Resources/Docs/Developer/fr-FR/index.md#rider-platform-sdk)

<!-- docs-navigation -->

---

[Documentation développeur](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
