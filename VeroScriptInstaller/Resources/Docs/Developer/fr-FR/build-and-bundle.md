# Compilation de l’installateur et contrat des bundles

## Produits compilés

App et Tool ciblent .NET 9 et utilisent `VeroScriptInstaller.Core`. App embarque les catalogues comme ressources et le bundle comme contenu auto-extractible dans l’exécutable unique. Tool construit et vérifie l’archive sans interface graphique.

`VeroScriptInstaller/global.json` sélectionne le SDK .NET `9.0.100` avec roll-forward `latestFeature`. Lancez les exemples depuis la racine ; `Push-Location` résout le SDK depuis le répertoire Installer. Conservez un SDK .NET 9 installé même avec les workloads mobiles .NET 10 ; la chaîne C# mobile reste distincte.

```powershell
Push-Location VeroScriptInstaller
try
{
	dotnet build App/VeroScriptInstaller.csproj -c Release -warnaserror
	dotnet build Tool/VeroScriptInstaller.Tool.csproj -c Release -warnaserror
	dotnet run --project Tests/VeroScriptInstaller.Tests.csproj -c Release
}
finally
{
	Pop-Location
}
```

Tests est un exécutable de régression : utilisez `dotnet run`, pas une découverte supposée par `dotnet test`. Compiler App seul n’y ajoute pas le contenu distribuable ; utilisez les scripts de publication.

## Publication Windows complète

La chaîne complète prend en charge Release pour `win-x64` et `win-arm64`. Elle nécessite CMake, MSVC, les SDK .NET 8/9 du jeu de produits, Node.js/npm et Emscripten pour reconstruire WebAssembly. Exécutez `npm ci` dans `Extensions/Platform/WebAssembly/VeroScriptWebAssembly` ; les tests navigateur nécessitent aussi un navigateur disponible, par exemple Chromium de Playwright.

```powershell
.\Resources\Tools\Packaging\package-veroscript-all.ps1 -ConfigurePreset windows-msvc-vs2026 -Runtime win-x64
```

Pour Windows ARM64, utilisez `-Runtime win-arm64` avec un preset correspondant, par exemple `-ConfigurePreset windows-msvc-vs2026-arm64`, et installez les outils MSVC ARM64 et le Windows SDK. La publication complète exécute des contrôles natifs, dont la matrice de wheels Python : utilisez un hôte Windows ARM64 natif avec tous les interpréteurs CPython ARM64 requis. Compiler les artefacts natifs depuis x64 ne suffit pas à réaliser ces contrôles. `win-arm64` désigne ARM64 natif, pas ARM64EC ni x64 émulé.

Choisissez le preset adapté à Visual Studio ou omettez-le pour la sélection automatique. La destination par défaut est `Release`. `-SkipBuild` réutilise les sorties natives et Web, mais publie toujours les applications managées et vérifie le SDK WebAssembly préparé. Il exige donc des sorties dont l’origine et la configuration sont connues. Les artefacts sont répartis sous `Release/<business-route>/<target>/<version>/`, avec les catégories `archives/`, `sdks/`, `packages/`, `installers/` et `symbols/` créées selon les besoins de chaque version. Les distributions partagées utilisent `any`, les SDK WebAssembly `wasm32`. Les manifestes, sommes de contrôle et index restent requis. Les versions sont indépendantes. Consultez les [règles de publication](release-layout.md) pour les dépendances Core/C++, les verrous, le nettoyage des versions et le retour arrière.

## Dépendances runtime natives Windows

La préparation MSVC Release installe les DLL redistribuables de l’outillage sélectionné via `vs_install_windows_runtime`. Le composant `VeroScriptWindowsRuntime` fournit la racine Runtime ; Interop, les outils C++ et Python utilisent leurs composants d’installation respectifs. L’assemblage des paquets place aussi les DLL près des points d’entrée natifs des SDK et des runtimes managés. Utilisez les fichiers redistribuables de l’outillage, sans copier de DLL depuis les répertoires système ; ni le CRT de débogage ni le Universal CRT ne sont inclus.

Le vérificateur de publication contrôle l’architecture native et exige `msvcp140.dll` et `vcruntime140.dll` près de chaque point d’entrée natif distribué, ainsi que `vcruntime140_1.dll` pour les cibles x64. ARM64 utilise son CRT natif et exclut le fichier x64 `vcruntime140_1.dll` fourni dans le composant redistribuable ARM64. Il exige aussi `licenses/MicrosoftVisualCpp/NOTICE.txt` dans ce répertoire. Conservez la source `Resources/Tools/CMake/WindowsRuntimeNotice.txt` comme avis tiers distinct ; la licence MIT du projet ne la remplace pas. Des dépendances manquantes empêchent la publication et l’absence des redistribuables MSVC fait échouer la configuration d’une wheel Windows. Le Universal CRT reste un prérequis du système.

## Prérequis de publication Python

Si Python est sélectionné pour une publication complète, préparez la matrice CPython 3.11–3.15 avant de lancer le pipeline. `-SkipBuild` / `--skip-build` construit et vérifie toujours les wheels et la distribution source. Consultez le [contrat de matrice Python](../../../../../Extensions/Language/Python/Resources/Docs/Developer/fr-FR/toolchain-and-packaging.md) pour la sélection des interpréteurs, le contrôle préalable et la distinction entre les ABI des wheels et l’ABI native unique du SDK.

## Contrat du bundle

Le manifeste définit l’identité du produit, la plateforme, l’architecture, les composants sélectionnables, les chemins relatifs et les hachages. Installation et réparation utilisent ce bundle, jamais l’état courant des sources. Un bundle développé contient `manifest.json` et `payload/` ; ce n’est pas une archive Runtime/SDK portable.

```powershell
Push-Location VeroScriptInstaller
try
{
	$tool = 'Tool/VeroScriptInstaller.Tool.csproj'
	dotnet run --project $tool -c Release -- build-bundle --output ../.tmp/runtime-installer-bundle --version 0.1.1 --profile runtime --source-runtime D:/Packages/Runtime
	dotnet run --project $tool -c Release -- verify --bundle ../.tmp/runtime-installer-bundle
	dotnet run --project $tool -c Release -- pack-bundle --bundle ../.tmp/runtime-installer-bundle --output ../.tmp/runtime-installer-bundle.zip
}
finally
{
	Pop-Location
}
```

`D:/Packages/Runtime` représente un contenu préparé avec CLI/LSP/DAP, bibliothèque standard et localisation. Adaptez les options `--source-*` au profil. Voir les [définitions de produits](../../../Examples/Products/README.md) et l’[automatisation](automation.md) pour installation, déplacement, réparation et désinstallation avec Tool.

## Transport du bundle dans un fichier unique

`App/InstallerBundleContent.targets` découpe `InstallerBundleArchive` en 32 parties au maximum, de 1 GiB chacune au plus, nommées `VeroScriptInstaller.EmbeddedBundle.zip.0000` puis numérotées à la suite. La publication en fichier unique les inclut via `IncludeAllContentForSelfExtract` ; ce ne sont pas des téléchargements séparés. Les métadonnées d’assembly enregistrent `VeroScriptInstaller.BundleSha256` pour le ZIP complet et `VeroScriptInstaller.BundlePartCount`.

`EmbeddedInstallerBundle` ouvre les parties extraites, refuse liens et nombres/tailles invalides, vérifie le SHA-256 du ZIP complet et fournit un flux repositionnable sans charger toute l’archive ni la recopier dans un autre ZIP. Sous Linux, l’expansion utilise `AppContext.BaseDirectory/VeroScriptInstaller.Unpack` ; ailleurs, `Path.GetTempPath()/VeroScriptInstaller`. Extraction et validation du manifeste restent applicables. La cible de publication refuse un exécutable absent ou vide ; ces gardes ne qualifient pas à elles seules un grand installateur sur sa cible.

Un échec supprime normalement le staging de publication possédé. `-KeepFailedStage` sous Windows ou `--keep-failed-stage` pour le producteur complet Unix conserve le staging en échec et affiche son chemin pour diagnostic. Le staging réussi reste supprimé. Ces fichiers sont des candidats : revérifier sources, configuration et empreintes avant réutilisation.

## Vérification et signature

Bundles embarqués et externes doivent passer les mêmes validations de manifeste, chemins, hachages, composants et plateforme. Une modification du format d’archive, de l’empreinte ou du schéma d’enregistrement impose d’actualiser ensemble les tests de réparation, déplacement, désinstallation et publication. Les contrats des composants doivent inclure les API et binaires actuels : en-têtes Core, session et paquet CMake C++, extension Python, types et DLL Interop correspondante. Le vérificateur final contrôle aussi les API de cache des sessions, les assemblies C# libres et NuGet, les paquets de symboles et les deux SDK WebAssembly avec déclarations.

La chaîne exécute `--verify-localization` et `--verify-embedded-bundle` sur l’installateur, également après signature. Vérifiez les codes de sortie ; ces contrôles ne remplacent pas un essai graphique. La vérification linguistique couvre les six catalogues, huit profils, clés des composants, valeurs non vides et paramètres de format, ainsi que les surcharges partielles, les variantes chinoises et les changements de langue. Les traductions intégrées manquantes doivent échouer sans être masquées par un repli anglais.

Les clés de localisation participent à l’empreinte du manifeste. Reconstruisez bundle et application ensemble. Conservez l’installateur original pour la réparation ; une reconstruction de même version avec une empreinte différente n’est acceptée qu’en mise à jour.

La signature est optionnelle. Pour la rendre obligatoire, configurez `VEROSCRIPT_SIGNING_CERTIFICATE_THUMBPRINT` et `VEROSCRIPT_TIMESTAMP_SERVER`, puis utilisez `-RequireSignedInstallers` pour la chaîne complète ou `-RequireSignature` pour un produit. Les sommes de contrôle finales sont calculées après signature. Passez également `-RequireSignedInstallers` au vérificateur indépendant : tous les installateurs doivent avoir un état Authenticode `Valid`.

## Publications Linux/macOS

Les publications complètes et les composants autonomes suivent les [règles de publication par produit](release-layout.md). Les archives natives Runtime/SDK utilisent `.tar.gz` avec leurs permissions exécutables ; WebAssembly reste en ZIP. Les installateurs Linux utilisent `.tar.gz` et ceux de macOS un `.dmg` contenant une `.app` standard. Les installateurs sont autonomes. Linux respecte un `XDG_DATA_HOME` absolu. L’ABI Python et la compatibilité Linux dépendent de l’environnement de construction enregistré.

```bash
bash Resources/Tools/Packaging/package-veroscript-all.sh --target linux-x64
python3 Resources/Tools/Packaging/verify-veroscript-release.py \
  --release-directory Release --target linux-x64
```

`target`: `linux-x64`, `linux-arm64`, `macos-x64`, `macos-arm64`.

Pour assembler les distributions de bureau vers une autre cible, dont `win-x86`, consultez `Resources/Tools/Packaging/package-veroscript-desktop-cross.py --help` et fournissez une véritable construction cible avec ses dépendances correspondantes. La vérification de construction/archive et la validation d’exécution cible sont consignées séparément.


## Paquets documentaires et vues intégrées

Voir [Maintenance documentaire — Vérification et publication](../../../../../Resources/Docs/Developer/fr-FR/index.md#integrated-publication-views).

Les versions complètes publient la vue User via Resources dans `Resources/Docs/`, à côté de `Resources/Scripts` et `Resources/Assets`. Les archives et installateurs actuels Runtime/SDK/extensions omettent les documents et exemples rédigés. La politique de contenu figure dans `release-layout.md` du guide Developer de l’installateur.

Les guides communs, Overview, Standards et Tutorials restent dans Resources/Docs ; les originaux détaillés restent dans Resources/Docs de chaque module. La publication assemble et vérifie une collection temporaire `.tmp/docs-collection-*`, puis la supprime. Aucune collection générée ne reste dans les sources Docs.

## Avis de licence intégrés

Le projet Core intègre les textes complets du fichier `LICENSE` à la racine et de `VeroScriptInstaller/Resources/Licenses/THIRD-PARTY-NOTICES.txt`. App et Tool acceptent `--license` ou `--third-party-licenses` comme argument unique et terminent avant l’initialisation de l’interface ou du bundle. Ces avis sont des ressources originales, pas des catalogues de localisation ; ne les traduisez ni ne les abrègez. Les outils de publication Windows et Unix comparent les deux sorties aux textes source avant de publier l’installateur. Lors d’un changement de dépendance, mettez à jour les [sources et le registre de maintenance](../../../Licenses/README.md), reconstruisez l’exécutable et relancez les deux vérifications. Un ancien installateur conserve ses avis intégrés antérieurs.

Les produits personnalisés utilisent le Tool avec --definition ; le producteur PowerShell exige un nom de produit VeroScript configuré. InstallerProductDefinition.ReleaseGroup (JSON releaseGroup) égal à Modules interdit la création d’un nouvel installateur/bundle. Le guide des définitions fournit un exemple complet et distingue la politique des archives Modules.

- [Organisation des distributions et règles de publication](release-layout.md)

Le contrat Python Windows exige le point d’entrée du paquet, son fichier de types, le marqueur `py.typed` et la DLL Interop correspondante ; la présence de la seule extension native ne suffit pas.

<a id="publication-paths"></a>

`veroscript_business_release.guard_tree` refuse les racines et entrées de publication liées. Sur macOS seulement, il accepte comme ancêtres les alias root `/tmp`, `/var` et `/etc` vers exactement `/private/tmp`, `/private/var` et `/private/etc`. Les sorties temporaires ordinaires sont ainsi possibles, sans autoriser une racine liée ni des liens dans la collection. Préservez cette distinction lors des modifications de contrôle.

## Téléchargements des installateurs autonomes

Utilisez `package-veroscript-installer.ps1` pour l’installateur Runtime Windows et `package-veroscript-installer.sh` sur un hôte Linux ou macOS correspondant. Avec la sortie gérée par défaut, l’archive Runtime provient de `Release/Runtime/<target>/<version>/archives/`. Un contenu déjà développé peut être fourni avec `-RuntimePackageDir` ou `--runtime-package-dir`. Les noms suivent `VeroScript<business>Installer-<version>-<target>`, avec `.exe` sous Windows, `.tar.gz` sous Linux et `.dmg` sous macOS. Linux publie une archive contenant l’exécutable et macOS une image contenant l’application. Voir la [validation des téléchargements](release-validation.md#installer-download-validation) pour le contenu embarqué.

## Icônes des exécutables Windows

Les constructions Windows de `veroscript`, `veroscript-runtime`, `veroscript-launcher`, `veroscript-lsp` et `veroscript-dap` embarquent `Resources/Assets/icons/veroscript-icon.ico` via le helper CMake des sources `vs_use_product_icon`. Les déclarations de cibles CLI activent `PRODUCT_ICON`. Ces exécutables requièrent le langage RC et un compilateur de ressources Windows. Le projet racine détecte et active RC quand il est disponible. Un projet parent réutilisant le helper source doit activer RC pour ses exécutables Windows. Les profils limités aux bibliothèques peuvent être configurés sans RC.

Après une modification de l’ICO, reconstruisez les exécutables concernés. `VeroScriptProductIcon.rc.in` inclut le SHA-256 de l’icône afin que la modification du RC généré déclenche aussi la reconstruction incrémentale Visual Studio. Réservez les options C/C++ à leurs langages et excluez RC. L’application Installer consomme séparément l’ICO et le PNG 256px via son projet, tandis que le bundle macOS utilise ICNS. Le [guide des ressources](../../../../../Resources/Assets/icons/README.md) possède les détails de génération et de formats. `python Resources/Tools/Packaging/veroscript_icons.py check --require-tracked` vérifie les sources maintenues. Les audits de publication lisent les groupes d’icônes PE Windows et comparent leurs trames à l’ICO maintenu sur tout hôte de construction, sans lancer l’exécutable cible. Consignez séparément l’affichage réel du shell et la validation de la plateforme cible.

<!-- docs-navigation -->

---

[Documentation développeur](index.md) · [VeroScript](../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/build-and-bundle.md) · [fr-FR](build-and-bundle.md) · [ja-JP](../ja-JP/build-and-bundle.md) · [ko-KR](../ko-KR/build-and-bundle.md) · [zh-CN](../zh-CN/build-and-bundle.md) · [zh-TW](../zh-TW/build-and-bundle.md)
