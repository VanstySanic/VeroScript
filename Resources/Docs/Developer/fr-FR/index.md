# Maintenance documentaire

[Code source sur GitHub](https://github.com/VanstySanic/VeroScript)

<a id="documentation-ownership"></a>

## Sources de référence et responsabilités

| Contenu | Original maintenu |
| --- | --- |
| Intégration commune des éditeurs | [Maintenance de l’intégration des éditeurs](editors.md) |
| Parcours communs, relations entre modules, tutoriels et règles de développement et de contribution au projet | `Resources/Docs` à la racine |
| Comportement, API publiques, valeurs par défaut et limites ; architecture, compilation et maintenance du module | `Resources/Docs/User/<locale>` et `Developer/<locale>` du module propriétaire |
| Maintenance commune des fournisseurs multimédias | [Maintenance multimédia](multimedia.md) |
| Arborescences All/User exportées et archives ZIP | Sorties générées ; modifier l’original référencé puis republier |

Maintenez un original par sujet et par langue. Les pages d’entrée résument et renvoient au responsable ; contrats complets, procédures et valeurs variables restent à cette destination. Actualisez les six originaux parallèles. Les procédures communes sont ici, les détails SDK/protocoles dans leur module. Les anciennes révisions restent dans le contrôle de version.

`documentation.json` enregistre les langues de la collection, les manifestes et les ressources associées. Le `manifest.json` de chaque module associe ses originaux User à des chemins de publication stables ; les pages Developer restent rattachées à ce module. Une source est publiée une seule fois dans All. Le paquet User en projette le même contenu User/Tutorials. Les copies publiées ne sont pas d’autres sources à maintenir.

## Localisation et chemins

Alignez noms de fichiers et sens dans les six langues, Overview compris. Conservez API, ID de diagnostics, clés JSON, commandes et code exécutable. Les résultats antérieurs ne concernent que les sources, artefacts et environnements enregistrés.

Les manifestes mappent les originaux User aux chemins publiés stables. Réécrivez en URL relatives les liens vers pages/ressources incluses, en préservant langues, ancres, encodage, requêtes, images et références ; ne modifiez pas code et commentaires HTML. Les références source ouvrent le dépôt en ligne. Les anciennes documentations se retrouvent dans le contrôle de version, sans copie publiée.

Pour le format des documents et fichiers de localisation maintenus, suivez [les règles communes de fin de ligne](../../Standards/fr-FR/GENERAL.md). Ces règles du dépôt ne changent pas la prise en charge des entrées CRLF par le langage et les outils. La publication produit des copies normalisées sans réécrire les originaux maintenus ni les ressources binaires.

## Vérification et publication

Exécutez les commandes suivantes depuis la racine du dépôt. Chaque publication reconstruit la collection depuis les originaux actuels ; modifier une page source ne nécessite pas de synchronisation. Validez uniquement les originaux maintenus dans Git. L’assemblage refuse les changements source intervenus pendant la génération et nettoie son dossier temporaire après succès ou échec. CI vérifie la parité linguistique, les inventaires, les octets publiés et les liens. Les textes Markdown/JSON publiés utilisent LF ; les avis de licence originaux, les ressources binaires et les originaux conservent leurs octets.

```powershell
python Resources/Tools/Packaging/veroscript_docs.py check
python Resources/Tools/Packaging/veroscript_docs.py check-collection
python Resources/Tools/Packaging/veroscript_docs.py publish --audience All --output .tmp/docs-preview
python Resources/Tools/Packaging/veroscript_docs.py verify-publication --audience All --output .tmp/docs-preview
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -OutRoot .tmp/docs-collection-package
./Resources/Tools/Packaging/package-veroscript-docs.ps1 -Audience User -OutRoot .tmp/docs-user-package
```

`build` et `publish` exigent un sous-dossier de sortie dédié et vide dans `.tmp` ou `dist` du dépôt. Toute sortie directe sous le `Release` source ou une collection gérée existante est refusée ; la publication Resources passe par le producteur transactionnel. Utilisez un nouveau dossier pour chaque prévisualisation. Pour réexporter un ZIP indépendant, utilisez `package-veroscript-docs.ps1`, qui prépare et vérifie le nouveau paquet avant de remplacer le précédent.

<a id="integrated-publication-views"></a>

`package-veroscript-docs.ps1` choisit All par défaut : Overview, User, Developer, Standards actuels, Tutorials complets et ressources liées. `-Audience User` publie les références dans `docs/<locale>/...`, les tutoriels complets dans `docs/Tutorials/<locale>/...` et leurs dépendances. Aucun public ne contient History.

Ce script exporte une documentation indépendante sans publier dans la collection de produits gérée. Sa destination par défaut est `dist/docs/<Audience>/<version>/`, ou `dist/docs/Custom/<version>/` avec `-SourceRoot`. Utilisez `-OutRoot` pour un autre répertoire dédié. Les publications complètes Resources assemblent la vue User par le producteur de produits.

Les versions complètes publient la vue User via Resources dans `Release/Resources/any/<version>/Resources/Docs/`, à côté de `Resources/Scripts` et `Resources/Assets`. Les archives et installateurs actuels Runtime/SDK/extensions omettent les documents et exemples rédigés. La politique de contenu figure dans [release-layout.md](../../../../VeroScriptInstaller/Resources/Docs/Developer/fr-FR/release-layout.md) du guide Developer de l’installateur.

Les [règles de développement et de contribution au projet](../../Standards/fr-FR/README.md) s’appliquent uniquement au travail sur le dépôt VeroScript, pas à l’utilisation du SDK, et n’ajoutent aucune condition à la licence MIT. Dans `documentation.json`, `standardsAudience` doit être `Developer` : Standards figure uniquement dans `All` comme documentation de développement, pas dans `User`, la documentation utilisateur de Resources, les archives Runtime/SDK/extensions ou les installateurs. Les mentions de copyright et de licence des sources sont conservées.

Les règles de développement et de contribution au projet sont maintenues dans `Resources/Docs/Standards/<locale>` : `zh-CN` définit les règles et les cinq autres langues restent équivalentes. La publication lit directement ces originaux. Ce dépôt autonome n’a pas de miroir IDE. `sync-standards` ne met à jour une référence gérée que si un consommateur ValenceStudio est identifié par son catalogue documentaire ; sinon aucun miroir n’est créé.

Python 3.11+ est requis. `check` vérifie les sources et les consommateurs de normes découverts ; `check-collection` vérifie une collection temporaire complète ; `build/verify` prévisualisent l’extraction User ; `publish/verify-publication` publient et vérifient All ou User depuis les originaux actuels. `-SourceRoot` exporte l’arborescence explicitement fournie et ne peut pas être combiné avec `-Audience`. Liens absents ou sortants, ancres absentes, arbres vides, liens de système de fichiers et entrées spéciales sont refusés ; source et destination ne peuvent pas se chevaucher. ZIP inclut les ressources cachées et dossiers vides ; un remplacement échoué restaure l’ancien artefact, avec une sauvegarde conservée si la restauration échoue. Les références source en ligne exigent le réseau ; un moteur HTML doit convertir les liens `.md` et préserver les ID des titres. Pour les compilations des produits, les contrôles des contenus et les résultats de validation à l’exécution, suivez la [procédure de validation de publication](../../../../VeroScriptInstaller/Resources/Docs/Developer/fr-FR/release-validation.md).

## Manifeste de module

Dans le catalogue supérieur `Resources/Docs/documentation.json`, le préfixe `$module/` désigne la racine du module source propriétaire. Conservez les noms de produits comme `VeroScript` ; les manifestes des modules utilisent toujours des chemins relatifs à leur propre dossier Docs. Après déplacement ou renommage du dossier extérieur, actualisez les commandes littérales des documents et les chemins réels des workflows du dépôt. Les scripts utilisés avec Windows PowerShell 5.1 doivent conserver le BOM UTF-8 lorsqu’ils contiennent du texte non ASCII.

```json
{
  "schemaVersion": 1,
  "module": "VeroScriptCli",
  "user": [
    {
      "source": "User/{locale}/index.md",
      "output": "{locale}/toolchain/cli.md"
    },
    {
      "source": "User/{locale}/packages.md",
      "output": "{locale}/toolchain/packages.md"
    }
  ],
  "developer": true
}
```

## But des documents

User décrit tâches, API publique, configuration et limites. Developer décrit architecture, responsabilités, construction, tests et publication. Tutorials enseigne par prérequis, étapes et résultats attendus. Standards contient uniquement les règles approuvées pour le développement de ce projet et les contributions à celui-ci. Les plans et preuves antérieurs restent dans le contrôle de version ou les sorties temporaires et ne certifient pas un dépôt modifié.

Évitez les dates calendaires et les identifiants personnels superflus dans le contenu, les titres et les noms de fichiers. Les explications générales décrivent le comportement sans répéter le numéro de diffusion courant. Gardez les versions exactes nécessaires à la compatibilité, aux outils, aux contrats ABI ou de format, aux exemples exécutables, au choix des artefacts ou aux mentions de licence originales. Préférez les liens vers les métadonnées maintenues aux identifiants recopiés. Conservez les journaux datés et les preuves dans des sorties temporaires ignorées. User ne répète ni méthode de test ni détails internes.

## Liens et limites des publics

Les liens relatifs dans User doivent rester dans la vue User, les Tutorials ou les ressources de support déclarées. Un lien relatif de User vers Developer ou Standards empêche les publications All et User. Conservez l’implémentation serveur, la maintenance et les procédures de validation dans Developer, et le comportement observable dans User. Developer peut renvoyer au contrat User correspondant. Vérifiez la collection source et les deux paquets après une modification des liens entre modules.

## Références des modules

| Domaine | Références |
| --- | --- |
| Langage et outils | [VeroScript](../../../../VeroScript/Resources/Docs/Developer/fr-FR/index.md) · [CLI](../../../../VeroScriptCli/Resources/Docs/Developer/fr-FR/index.md) · [Tooling](../../../../VeroScriptTooling/Resources/Docs/Developer/fr-FR/index.md) |
| SDK hôtes | [Interop](../../../../VeroScriptInterop/Resources/Docs/Developer/fr-FR/index.md) · [C++](../../../../Extensions/Language/Cpp/Resources/Docs/Developer/fr-FR/index.md) · [Python](../../../../Extensions/Language/Python/Resources/Docs/Developer/fr-FR/index.md) · [C#](../../../../Extensions/Language/CSharp/Resources/Docs/Developer/fr-FR/index.md) · [WebAssembly](../../../../Extensions/Platform/WebAssembly/Resources/Docs/Developer/fr-FR/index.md) |
| Moteurs d’exécution | [Bytecode VM](../../../../Modules/Execution/VeroScriptBytecodeVm/Resources/Docs/Developer/fr-FR/index.md) · [AsmJit](../../../../Modules/Execution/VeroScriptAsmJit/Resources/Docs/Developer/fr-FR/index.md) |
| Paquets et applications | [PackageManager](../../../../VeroScriptPackageManager/Resources/Docs/Developer/fr-FR/index.md) · [Application](../../../../VeroScriptApplication/Resources/Docs/Developer/fr-FR/index.md) · [Build](../../../../VeroScriptBuild/Resources/Docs/Developer/fr-FR/index.md) · [Installer](../../../../VeroScriptInstaller/Resources/Docs/Developer/fr-FR/index.md) |
| Éditeurs | [VS Code](../../../../Extensions/Editor/VSCode/VeroScriptVSCode/Resources/Docs/Developer/fr-FR/index.md) · [VS Code GUI](../../../../Extensions/Editor/VSCode/VeroScriptGuiVSCode/Resources/Docs/Developer/fr-FR/index.md) · [Rider](../../../../Extensions/Editor/JetBrains/VeroScriptJetBrains/Resources/Docs/Developer/fr-FR/index.md) · [Rider GUI](../../../../Extensions/Editor/JetBrains/VeroScriptGuiJetBrains/Resources/Docs/Developer/fr-FR/index.md) |
| Plateformes embarquées | [Mobile](../../../../Extensions/Platform/Mobile/VeroScriptMobile/Resources/Docs/Developer/fr-FR/index.md) · [Android](../../../../Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Docs/Developer/fr-FR/index.md) · [iOS](../../../../Extensions/Platform/Mobile/VeroScriptIOS/Resources/Docs/Developer/fr-FR/index.md) |
| Données et réseau | [JSON](../../../../Modules/Network/VeroScriptJson/Resources/Docs/Developer/fr-FR/index.md) · [Network](../../../../Modules/Network/VeroScriptNetwork/Resources/Docs/Developer/fr-FR/index.md) · [HTTP](../../../../Modules/Network/VeroScriptHttp/Resources/Docs/Developer/fr-FR/index.md) · [WebSocket](../../../../Modules/Network/VeroScriptWebSocket/Resources/Docs/Developer/fr-FR/index.md) · [Boost](../../../../Modules/Network/Backend/VeroScriptBoost/Resources/Docs/Developer/fr-FR/index.md) · [Curl](../../../../Modules/Network/Backend/VeroScriptCurl/Resources/Docs/Developer/fr-FR/index.md) |
| Multimédia et GUI | [Platform](../../../../Modules/Multimedia/VeroScriptPlatform/Resources/Docs/Developer/fr-FR/index.md) · [Graphics](../../../../Modules/Multimedia/VeroScriptGraphics/Resources/Docs/Developer/fr-FR/index.md) · [Audio](../../../../Modules/Multimedia/VeroScriptAudio/Resources/Docs/Developer/fr-FR/index.md) · [Image](../../../../Modules/Multimedia/VeroScriptImage/Resources/Docs/Developer/fr-FR/index.md) · [Font](../../../../Modules/Multimedia/VeroScriptFont/Resources/Docs/Developer/fr-FR/index.md) · [Gui](../../../../Modules/Multimedia/VeroScriptGui/Resources/Docs/Developer/fr-FR/index.md) · [Sdl3](../../../../Modules/Multimedia/Backend/VeroScriptSdl3/Resources/Docs/Developer/fr-FR/index.md) · [Stb](../../../../Modules/Multimedia/Backend/VeroScriptStb/Resources/Docs/Developer/fr-FR/index.md) · [FontBackend](../../../../Modules/Multimedia/Backend/VeroScriptFontBackend/Resources/Docs/Developer/fr-FR/index.md) · [RmlUi](../../../../Modules/Multimedia/Backend/VeroScriptRmlUi/Resources/Docs/Developer/fr-FR/index.md) |

Utilisez des liens relatifs entre guides actuels du dépôt. Les URL en ligne ne doivent ni contourner les frontières de lecteurs ni créer une dépendance réseau pour une page incluse. Les références au code source conservent leur comportement.

<a id="current-publication-policy"></a>

All et User publient uniquement la documentation actuelle. Les répertoires History, anciens plans et instantanés de validation ne sont pas des entrées de publication ; conservez les révisions dans le contrôle de version et les preuves exactes dans des sorties temporaires ignorées. Une source ou un SourceRoot personnalisé contenant un chemin History est refusé, quelle que soit la casse. Publiez Overview, User, Developer, Standards, les Tutorials complets et leurs dépendances selon le public choisi.

<a id="module-entrypoints"></a>

Le README à la racine d’un module présente son rôle, les modules liés, les guides et les licences. Resources/Docs/README est le sélecteur de langue et de public utilisé par la publication intégrée. L’utilisation détaillée appartient à User, l’implémentation, la construction et la vérification à Developer, les leçons à Tutorials. Les sous-modules des outils de langage renvoient aux guides du groupe. Gardez ces entrées concises et corrigez leurs liens relatifs lors des déplacements ; un README ne doit pas devenir un second manuel ou compte rendu de validation.

<!-- docs-navigation -->

---

[Documentation développeur](index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
