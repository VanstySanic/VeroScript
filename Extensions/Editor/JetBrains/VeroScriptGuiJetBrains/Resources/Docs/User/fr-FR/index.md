# VeroScript GUI dans Rider

## Installation et configuration

Installez **VeroScript**, puis une version compatible de **VeroScript GUI** via **Settings → Plugins → Install Plugin from Disk**. Redémarrez si demandé. Les prérequis Rider et le choix du Runtime suivent le guide du plugin principal ci-dessous.

Configurez le Runtime dans **Langages et frameworks → VeroScript** selon le
[guide du plugin de base](../../../../../VeroScriptJetBrains/Resources/Docs/User/fr-FR/index.md).
Les tâches GUI nécessitent le CLI avec GUI correspondant, les bibliothèques de
scripts GUI et les fournisseurs natifs. Le plugin ne les installe pas. Sans
Runtime, la coloration locale et les suggestions intégrées restent disponibles.

## Éditer le balisage et les styles

Les suggestions intégrées comprennent `ListView`, `TreeView`, `TableView`,
`ComboBox`, `TabView`, `Menu` et `Dialog`, leurs propriétés héritées de sélection
et de popup, événements et styles. Configurez les données et la virtualisation
via l’API de script GUI. Utilisez un Runtime GUI compatible contenant ces contrôles.

`.vswml` utilise le balisage GUI natif et `.vswcss` son langage de styles. Il ne
s’agit ni de HTML ni de CSS complet. `bind:` et `on:` ne nécessitent pas de
déclaration `xmlns`. La complétion couvre les widgets intégrés, propriétés
héritées, liaisons de lecture, événements, propriétés de style et pseudo-états
pris en charge. Elle respecte la casse et la présélection de Rider. Les attributs
`src` des directives proposent navigation et complétion de fichiers. Les chemins
relatifs partent du fichier déclarant ; ceux reconnus comme absolus par l’OS
utilisent la racine du système de fichiers. Les séparateurs suivent cet OS et les
caractères littéraux du nom, comme `?`, sont préservés. Les identifiants opaques de l’hôte et les chemins avec
espaces périphériques supprimés par le SDK ou entités (`&...;`) sont exclus de la navigation locale. Les chemins ordinaires
participent aux renommages de fichiers/dossiers dans Rider ; les noms insérés
sont échappés pour le balisage.

Valider un attribut par `=` ou une propriété de style par `:` conduit à sa valeur
sans répéter la ponctuation existante, et préserve cette valeur. Tab remplace
le nom entier, y compris les traits d’union et préfixes `bind:`/`on:`.

Les blocs `<script>` / `<style>` et CDATA ont une coloration lexicale. Pour les
services sémantiques VS, utilisez un fichier `.vs` externe, référencé par
`<script src="logic.vs" type="App::Logic" />`. Il conserve le LSP du plugin de
base. L’extension n’analyse pas la sémantique des scripts/liaisons en ligne ni
les Schema personnalisés, et ne fournit pas de formatage ou de diagnostics de
compilation automatiques. Lancez **Vérifier GUI** pour la validation native.
Un widget personnalisé absent du catalogue intégré n’est pas déclaré invalide.
La résolution sémantique des API GUI et leur débogage dépendent du LSP/DAP et de
l’hôte configurés ; cette extension ne génère pas leurs liaisons exécutables.

`page.vswml` :

```xml
<Stack Width="100%" Height="100%">
	<style src="page.vswcss" />
	<Text Text="Hello, VeroScript GUI!" />
</Stack>
```

`page.vswcss` :

```css
:root {
    padding: 16px;
    gap: 12px;
    color: #edf4fc;
    background-color: #101927;
    font-size: 18px;
}
```

[Guide du framework](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/User/fr-FR/Reference/Framework.md) ·
[Showcase](../../../../../../../../Modules/Multimedia/VeroScriptGui/Resources/Examples/Showcase/page.vswml)

## Vérification, prévisualisation et compilation

Sur une entrée locale `.vswml`, `.vs` ou `.vsapp`, ouvrez **VeroScript GUI** dans
le menu contextuel. Un `.vswcss` se vérifie via son entrée de balisage, sans
exécution directe.

| Tâche | Comportement |
| --- | --- |
| Vérifier GUI | `gui-check` vérifie/compile sans appeler l’entrée de l’application ni ouvrir de fenêtre d’aperçu. |
| Prévisualiser GUI | `gui-preview` exécute dans une fenêtre native séparée. Arrêtez depuis Rider ou fermez la fenêtre. |
| Configurer la compilation de l’application GUI | Ouvre la configuration. Vérifiez sortie et ressources, puis utilisez Exécuter pour appeler `gui-compile`. |

La vérification ou la compilation des sources peut exécuter des processeurs de compilation écrits en script. Utilisez un Runtime complet et compatible avec sa VM et ses bibliothèques GUI ; ces tâches ne nécessitent pas de fournisseur JIT d’exécution.

Créez une configuration **VeroScript GUI** dans le sélecteur d’exécution. Les
`.vswml` proposent une configuration contextuelle de prévisualisation. Les `.vs`
ordinaires conservent l’exécution et le débogage du plugin de base. Les tâches
GUI proposent Exécuter, sans lancement DAP avec GUI. Pour un domaine préparé
par l’hôte, suivez la procédure d’attachement du plugin de base. La
prévisualisation est explicite, sans concepteur intégré ni rechargement automatique.

## Détails de configuration

- Les actions contextuelles héritent du modèle GUI et effacent les options
  inapplicables à la tâche/entrée. Une sortie de compilation personnalisée applicable est conservée.
- Runtime, langue et options du compilateur sont hérités. Les surcharges JSON
  d’exécution ne changent pas l’édition. Les chemins hérités gardent leur base
  projet ; les surcharges relatives utilisent le répertoire de travail.
- Les entrées contextuelles utilisent `$PROJECT_DIR$`. Un répertoire de travail
  vide désigne la racine du projet. Les références ML/CSS relatives utilisent
  le fichier source comme base.
- L’environnement de paquets correspond à `--path`. Un champ de backend vide
  omet la surcharge et hérite des valeurs du Runtime ou des paquets. `auto` confie
  la sélection au moteur natif ; `none` désactive le service. Désactiver un service
  requis par la prévisualisation produit une erreur. Une valeur de backend
  explicitement vide dans l’environnement natif signifie `none`, contrairement
  au champ vide ici. Les fournisseurs requis doivent toujours être installés
  ou fournis par le Runtime GUI.
- La fabrique `Context` standard utilise `rmlui` seulement si le constructeur,
  l’invocation et les valeurs de la fabrique ne choisissent aucun backend GUI.
  Les choix explicites `auto` et `none` sont conservés. Ces six champs concernent
  les services GUI ; les options d’exécution VM/JIT sont distinctes.
- Vérifier et compiler utilisent la VM ; la prévisualisation conserve les valeurs natives d’exécution.
- Les chemins d’entrée enregistrés normalisent uniquement les séparateurs du système ;
  les barres obliques inverses des noms de fichiers POSIX restent littérales.
- Les arguments de prévisualisation suivent `--`. La limite positive `int64`
  concerne uniquement `.vswml` ; vide ou `0` signifie illimité. Les scripts et
  `.vsapp` gèrent leur propre boucle.
- La compilation exige une source `.vswml` / `.vs` et une sortie `.vsapp`.
  Nom et tableau de ressources, par exemple `["assets/logo.png"]`, sont
  facultatifs. La sortie ne peut écraser l’entrée ni une ressource déclarée.
  La destination initiale est `.tmp/gui/` ; changez-la pour conserver l’artefact.
- L’environnement est un objet JSON à valeurs textuelles, avec héritage parent
  configurable. Les diagnostics locaux proposent des liens de ligne. Les
  commandes absentes du Runtime et les erreurs de fournisseur restent visibles.

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
