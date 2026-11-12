# Installer et utiliser VeroScript

Choisissez le paquet correspondant à votre système et à votre architecture. Runtime exécute les scripts et fournit CLI, LSP et DAP ; les SDK permettent d’intégrer VeroScript dans une application hôte. Les installateurs déploient les composants sélectionnés. Les archives portables peuvent être extraites dans le répertoire de votre choix.

## Choisir un produit

| Produit | Usage |
|---|---|
| Runtime | Exécution des scripts ; CLI/LSP/DAP et bibliothèque standard |
| Core SDK | En-têtes C++ du cœur et de Tooling, bibliothèques et paquet CMake |
| Interop SDK | ABI C pour les hôtes natifs ou d’autres langages |
| C++ SDK | Adaptateur C++ ; exige le même build du Core SDK |
| C# SDK | Adaptateur .NET et runtime Interop natif correspondant |
| Python SDK | Adaptateur CPython et outils Python |
| WebAssembly SDK | Runtime WebAssembly et API TypeScript pour les hôtes JavaScript |

Runtime n’installe pas de SDK. L’installateur propose les outils, AOT et symboles présents dans son paquet. Obtenez séparément la documentation et les exemples dans Resources. Les destinations SDK incluent la version par défaut et permettent des installations parallèles. Les versions des produits peuvent différer ; respectez les dépendances et l’ABI de chaque SDK.

Dans `Release/<business-route>/<target>/<version>/`, choisissez `installers` pour les installateurs de bureau, `archives` pour les archives portables de Runtime ou de modules, `sdks` pour les SDK de développement, `packages` pour les paquets d’écosystème et les extensions d’éditeur, et `symbols` pour les symboles de débogage. Les distributions communes utilisent `any` ; le SDK WebAssembly portable utilise `wasm32`. Les Modules facultatifs sont distribués séparément, sans installateur autonome.

## Installer, mettre à jour et déplacer

Sous Windows, lancez l’installateur `.exe`. Sous Linux, extrayez le `.tar.gz` puis lancez son programme ; sous macOS, ouvrez le `.dmg` puis lancez son `.app`. Suivez l’assistant : composants fonctionnels, dossier de destination, associations de fichiers si proposées, vérification des choix, puis installation. Les boutons Suivant et Précédent permettent de changer de page ; les longues listes de composants sont réparties sur plusieurs pages sans faire défiler la fenêtre. La documentation et les exemples ne sont pas proposés comme options d’installation. L’installateur est autonome et ne requiert pas de .NET Runtime séparé sur la machine cible. L’ajout de Runtime au `PATH` est désactivé par défaut ; ouvrez un nouveau terminal après l’avoir activé.

L’installateur recherche les enregistrements valides du même produit uniquement dans ses dossiers de destination de composants actuels. Il entre en mode mise à jour lorsqu’il y trouve une installation correspondante, y compris pour un paquet reconstruit de même version. Les mises à jour préservent les fichiers créés par l’utilisateur. Un conflit entre un fichier du nouveau paquet et un fichier utilisateur fait échouer l’opération ; déplacez le fichier en conflit avant de réessayer. Désélectionner un composant facultatif installé supprime ses fichiers gérés, mais conserve les fichiers utilisateur. Désactiver l’option PATH ne retire que les entrées appartenant à cet enregistrement.

Lorsqu’une installation existante est détectée, son enregistrement fournit les valeurs par défaut uniquement pour les choix de composants, de PATH et d’associations prises en charge que vous n’avez pas modifiés. Les confirmations de mise à jour et de désinstallation indiquent explicitement la suppression d’une entrée PATH appartenant à l’installation.

La racine d’installation est le dossier parent choisi dans l’assistant, pas un dossier de composant tel que `SDK/Core/<version>`. Les composants des produits intégrés utilisent des dossiers versionnés : une autre version du produit peut coexister avec l’ancienne sous la même racine. L’installateur ne recherche, ne migre et ne désinstalle pas automatiquement les autres versions. Pour remplacer une ancienne version, installez et vérifiez la nouvelle, puis désinstallez l’ancienne avec son installateur original et son enregistrement. Pour changer de racine, installez dans la nouvelle, copiez vos fichiers personnels et désinstallez l’ancienne installation à partir de son enregistrement ; ne déplacez pas directement les dossiers de composants gérés.

## Fichiers runtime natifs Windows

Les paquets Windows incluent les DLL runtime Visual C++ correspondantes près des programmes et bibliothèques natifs. Extrayez l’archive entière ou utilisez l’installateur ; lors du déploiement, conservez ces DLL et leur `licenses/MicrosoftVisualCpp/NOTICE.txt` avec le programme ou la bibliothèque native. Copier seulement l’exécutable d’un programme natif, une DLL Interop ou une extension Python peut laisser de côté des dépendances nécessaires. Le système doit toujours fournir le Universal CRT.

## Réparer et désinstaller

Chaque racine de composant contient `.veroscript-install.json`. Réparez avec le package produit original : version, plateforme, architecture et empreinte du manifeste doivent correspondre exactement. Conservez donc l'installateur d'origine. La désinstallation ne relit pas le payload embarqué et les modes réparation/désinstallation sont exclusifs.

Choisissez une seule opération ci-dessous. Remplacez les chemins de l’installateur et de l’enregistrement par les vôtres. Chaque commande ouvre sa fenêtre et attend votre confirmation.

Réparation uniquement:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --repair-record $record
```

Désinstallation uniquement:

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$record = 'D:\Apps\VeroScript\Runtime\0.1.1\.veroscript-install.json'
& $installer --uninstall-record $record
```

La désinstallation ne retire que les fichiers gérés, les entrées PATH et les valeurs d’association et d’icône appartenant encore à cette installation ; les répertoires contenant des fichiers utilisateur demeurent. Après une interruption, la lecture de l'enregistrement, la réparation ou la désinstallation commence par récupérer la transaction. Ne supprimez pas manuellement `.veroscript-transaction-*.json`, les répertoires de staging ou de sauvegarde.

## Consommer les SDK CMake

Ajoutez la racine SDK extraite à `CMAKE_PREFIX_PATH` :

```cmake
find_package(VeroScript 0.1.1 CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScript)
```

Le Core SDK fournit `VeroScript::VeroScript` et `VeroScript::VeroScriptTooling`. Ses cibles importées transmettent les exigences de compilation des en-têtes publics. Le backend JIT est un module distinct ; Core n’exporte pas `VeroScript::asmjit`. Les bibliothèques C++/AOT appartiennent au produit C++ distinct. Pour une installation par installateur, ajoutez `<install-root>/SDK/Core/<version>` à `CMAKE_PREFIX_PATH` ; `<install-root>` est la destination choisie dans l’assistant. Obtenez les exemples séparément ; ils ne sont inclus ni dans le SDK portable ni dans l’installateur actuel.

Le SDK C++ exige le Core SDK correspondant et son répertoire `Runtime` :

```cmake
find_package(VeroScriptCpp 0.1.1 EXACT CONFIG REQUIRED)
target_link_libraries(my_host PRIVATE VeroScript::VeroScriptCpp)
```

`VeroScriptCppConfig.cmake` exige la version exacte du Core SDK enregistrée à la compilation du paquet C++ et vérifie l’identifiant SHA-256 de la bibliothèque Core. La version Core peut différer de celle du produit C++. Utilisez la dépendance Core indiquée dans le manifeste de publication C++ ; des numéros de version égaux ne suffisent pas. Ajoutez `<cpp-package>/Runtime` pour un paquet portable ou `<install-root>/SDK/Cpp/<version>/Runtime` après installation. Demandez `COMPONENTS Tools Aot` si ces composants sont installés.

Les outils du Core SDK trouvent `assets/stdlib` et `assets/localization` relativement à `bin`. Depuis la racine du Core SDK sous Windows, exécutez un exemple de la distribution Resources obtenue séparément ; remplacez le chemin absolu par son emplacement réel :

```powershell
.\bin\veroscript.exe --bc-cache=off 'D:\Packages\Resources\Scripts\examples\standard_library_algorithms.vs'
```

## Documentation hors ligne et exemples

Obtenez séparément la distribution Resources correspondante. Ouvrez `Resources/Docs/README.md`, choisissez une langue et suivez les liens relatifs vers les références et tutoriels. Un ZIP documentaire autonome utilise `docs/README.md` dans son dossier extrait. Resources fournit aussi les exemples de scripts sous `Resources/Scripts/examples`. Les archives SDK et installateurs actuels n’incluent pas de composants documentation/exemples. Les références aux sources en ligne nécessitent le réseau ; les exemples peuvent nécessiter le SDK correspondant et ses dépendances.

## Langue de l’installateur

L’installateur fournit l’anglais, le français, le japonais, le coréen et les chinois simplifié et traditionnel. Choisissez la langue dans la fenêtre ou passez `--language` au lancement. Par défaut il suit la langue d’interface du système, avec repli en anglais si aucune langue ne correspond. Ce choix concerne l’installateur ; utilisez CLI `--lang` ou les API des SDK pour les diagnostics. Dans cet exemple Windows, remplacez le chemin et la version par votre fichier téléchargé :

```powershell
& 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe' --language fr-FR
```

## Installation Linux/macOS

Sous Linux, extrayez l’installateur `.tar.gz` adapté à `linux-x64` ou `linux-arm64`, puis lancez son programme. Sous macOS, ouvrez le `.dmg` adapté à `macos-x64` ou `macos-arm64` et lancez son installateur `.app`. Un bureau graphique est nécessaire. Réparation et désinstallation acceptent les mêmes options d’enregistrement que sous Windows. Les exemples ci-dessous utilisent Linux ; sous macOS, définissez `installer` sur le chemin de l’installateur original `<installer>.app/Contents/MacOS/VeroScriptInstaller` et omettez `chmod` pour une application sur une image en lecture seule. Remplacez les chemins par les vôtres. Préservez les permissions exécutables à l’extraction des archives portables Runtime/SDK.

Choisissez une seule opération ci-dessous. Remplacez les chemins de l’installateur et de l’enregistrement par les vôtres. Chaque commande ouvre sa fenêtre et attend votre confirmation.

Réparation uniquement:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --repair-record "$record"
```

Désinstallation uniquement:

```bash
installer="$HOME/Downloads/VeroScriptRuntimeInstaller-0.1.1-linux-x64"
record="$HOME/Apps/VeroScript/Runtime/0.1.1/.veroscript-install.json"
chmod +x "$installer"
"$installer" --uninstall-record "$record"
```

## Avis de licence

Le texte complet de la [licence MIT](../../../../../LICENSE) du projet est inclus dans cette documentation. Les avis des dépendances sont accessibles depuis l’installateur téléchargé. Utilisez chaque option séparément : elle affiche le texte original puis termine le programme sans ouvrir la fenêtre d’installation. Remplacez le chemin d’exemple par celui de votre fichier. Les avis conservent leur langue d’origine ; `--language` sélectionne séparément la langue de l’interface d’installation.

L’exemple utilise temporairement UTF-8 pour afficher correctement les caractères des avis dans Windows PowerShell 5.1, puis rétablit l’encodage initial du terminal.

```powershell
$installer = 'D:\Downloads\VeroScriptRuntimeInstaller-0.1.1-win-x64.exe'
$noticeEncoding = [Console]::OutputEncoding
try {
	[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
	& $installer --license | Out-Host
	& $installer --third-party-licenses | Out-Host
}
finally
{
	[Console]::OutputEncoding = $noticeEncoding
}
```

<a id="file-associations"></a>

## Associations de fichiers Windows

Lorsqu’un installateur Windows contient Runtime et les icônes correspondantes, sélectionner Runtime permet de choisir les associations de fichiers facultatives. Elles sont désactivées pour une nouvelle installation. Enregistrez `.vs` et/ou `.vsapp` pour les exécuter avec ce Runtime, ou demandez uniquement l’icône `.vspkg`. Ouvrir un fichier `.vs` ou `.vsapp` associé l’exécute ; l’option `.vspkg` n’exécute ni n’installe de paquet.

L’enregistrement ajoute un gestionnaire et des icônes pour l’utilisateur courant. Choisissez vous-même l’application par défaut dans les paramètres Windows ; l’enregistrement ne définit pas le choix système. L’action facultative demande cette page après une installation réussie ; la page de résultat signale un échec d’ouverture ou un enregistrement incomplet. Windows peut afficher l’icône de l’application par défaut choisie.

Les mises à jour reprennent les choix enregistrés tant que vous ne les modifiez pas. Désactiver une option, réparer, annuler ou désinstaller préserve les inscriptions étrangères et les modifications ultérieures ; seules les valeurs inchangées appartenant à cette installation peuvent être supprimées ou restaurées. Ces choix ne sont pas proposés sur les autres plateformes ni dans les paquets dépourvus des fichiers Runtime requis.

L’icône `.vspkg` est ajoutée uniquement si aucune icône n’est déjà enregistrée. Désactiver l’option d’application par défaut annule seulement la demande d’ouverture des paramètres pour cette opération, sans réinitialiser un choix confirmé dans Windows. La désinstallation ne modifie pas les choix système protégés et ne garantit pas le retour au choix précédent. Les anciens enregistrements sans propriété des associations ne déclenchent aucun nettoyage de celles-ci.

<!-- docs-navigation -->

---

[Documentation utilisateur](index.md) · [VeroScript](../../../../../Resources/Docs/User/fr-FR/index.md)

Langue: [en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
