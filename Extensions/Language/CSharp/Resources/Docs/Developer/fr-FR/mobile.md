# CSharp — Mobile SDK

Cette page s’adresse aux mainteneurs qui construisent et publient le SDK mobile C#. Desktop et mobile partagent `VeroScriptCSharp.csproj` et l’identité NuGet `VeroScript.CSharp`. Les cibles par défaut sont .NET 8/9 ; `VeroScriptIncludeMobile=true` ajoute les deux cibles .NET 10 Android/iOS. Android exige API 26 avec arm64-v8a/x86_64 ; iOS exige 15.0 avec arm64 appareil et arm64/x86_64 simulateur. Le paquet mobile nécessite les quatre cibles managées, les workloads adaptés et une racine native complète et vérifiée.

`prepare-native.py` utilise l’AAR Android et le SDK statique `VeroScriptIOS.xcframework` de cette révision source. Ils contiennent Core/C++/Interop. L’application C# n’exige ni SDK CPython ni producteur mobile Python. Elle n’utilise ni wrapper JNI Java ni module JIT natif. Préparez un nouveau dossier, vérifiez-le, puis créez le paquet.

## Préparation du SDK natif

```sh
gradle --project-dir Extensions/Platform/Mobile/VeroScriptAndroid --no-daemon assembleRelease
python3 Resources/Tools/Packaging/package-veroscript-mobile.py ios \
  --cmake /path/to/cmake --out-root dist/mobile/ios
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py prepare \
  --android-aar Extensions/Platform/Mobile/VeroScriptAndroid/build/outputs/aar/VeroScriptAndroid-release.aar \
  --ios-sdk dist/mobile/ios --output dist/csharp-mobile/native
python3 Extensions/Language/CSharp/Resources/Tools/Mobile/prepare-native.py verify \
  dist/csharp-mobile/native
dotnet pack Extensions/Language/CSharp/VeroScriptCSharp/VeroScriptCSharp.csproj \
  -c Release -p:VeroScriptIncludeMobile=true \
  -p:VeroScriptMobileNativeRoot=/absolute/path/to/native \
  -o /absolute/path/to/mobile-nuget
```

Adaptez le nom de l’AAR si Gradle produit un autre fichier. Android exige son SDK/NDK/Gradle configuré ; iOS exige macOS et Xcode complet. Les cibles managées mobiles nécessitent les workloads .NET 10 Android/iOS. `VeroScriptMobilePython` choisit l’interpréteur de préparation s’il ne s’appelle pas `python3`. La préparation ne télécharge pas de SDK et ne signe pas les applications.

## Contrats d’intégration

La préparation vérifie les en-têtes d’origine, la bibliothèque standard, les catalogues en six langues, les notices de redistribution et l’inventaire SHA-256. Elle contrôle l’identité/API Android et l’alignement ELF LOAD de 16 Ko, en conservant `libVeroScriptInterop.so` et le `libc++_shared.so` correspondant pour les deux ABI. Elle audite les objets d’archives iOS statiques pour les architectures appareil/simulateur et l’OS minimal. Le SDK iOS exige Foundation/libc++. Un dossier de sortie existant est refusé.

NuGet importe automatiquement `buildTransitive/VeroScript.CSharp.targets`. Avec `ProjectReference`, importez ce fichier et fournissez `VeroScriptMobileNativeRoot`. Conservez bibliothèques/assets Android et, sur iOS, `__Internal`, `NativeReference`, `ForceLoad` et la rétention `ReferenceNativeSymbol` générée. Après résolution, vérifiez l’assembly iOS réellement utilisé, références transitives comprises ; refusez les assemblies desktop/Android. Bibliothèque et catalogues sont des ressources de l’application ; le cache de bytecode doit être dans un dossier accessible en écriture géré par celle-ci.

NuGet conserve la disposition relative préparée sous `native/`, dont `android/<abi>`, `ios`, `assets` et `licenses` ; ne dupliquez pas ces segments pendant le packaging. Les targets MSBuild reconnaissent une application via `OutputType=Exe`, ou sur Android via `AndroidApplication=true` après inférence du SDK. La sélection des bibliothèques natives et assets MAUI reste active lorsque le SDK Android déduit `Library`, sans s’appliquer aux bibliothèques de classes ordinaires.

L’installation vérifie routes et empreintes, exige la casse exacte `stdlib/core.vs`, ferme les flux et répare les fichiers obsolètes lors d’un nouvel essai. Un échec peut laisser des fichiers vérifiés ; relancez l’installation avant l’exécution. Créez, configurez, utilisez et libérez les sessions sur un même thread de travail, après la fin de l’installation asynchrone. Choisissez VM explicitement. Le paquet ne fournit pas de JIT natif.

[Intégration applicative](../../User/fr-FR/mobile.md)

## Validation de la version

Les tests portables C# vérifient les profils d’import dynamique et statique `IOS` contre le manifeste d’exports natifs. Ils couvrent corruption, traversée de chemins, annulation, fermeture des flux, réparation et installation réelle des ressources. Les fixtures Python vérifient préparation SDK, sélection d’assets MSBuild, RID/OS incompatibles, matrices incomplètes et rejet d’assemblies iOS résolus incompatibles. Ce sont des tests hôte.

Avant une version mobile stable, construisez et exécutez l’exemple MAUI en Release sur émulateur/appareil Android et simulateur/appareil iOS. Vérifiez systèmes Android à pages de 16 Ko, alignement des dépendances et ZIP APK, signature/liaison iOS, AOT/trimming, annulation, callbacks, imports et démarrage à froid. Consignez versions des outils, inspection des paquets et résultats réels. Le support mobile reste expérimental jusqu’à l’achèvement de ces validations sur les plateformes cibles.
## Documentation publiée

Les paquets mobiles contiennent les fichiers fonctionnels, les ressources d’exécution et les mentions légales. Ils excluent README, manuels et exemples ; la documentation User intégrée est disponible dans la distribution Resources séparée. Les chemins des ressources natives et les exigences de validation sur appareil restent inchangés.

## Feed NuGet commun desktop et mobile

La commande ci-dessus prépare le paquet `VeroScript.CSharp` avec support mobile. Pour publier l’activité CSharp dans une collection desktop ou mobile, préparez un feed canonique contenant les six identifiants C# enregistrés et leurs fichiers `.nupkg` et `.snupkg` correspondants. Sélectionnez ce feed avec `VEROSCRIPT_CSHARP_PACKAGE_DIRECTORY`. Pour réutiliser ce paquet avec `package-veroscript-mobile-release.py`, passez le même fichier `VeroScript.CSharp` avec support mobile à `--csharp-package`. Réutilisez ces octets sur toutes les cibles. Les entrées complètes et l’exemple figurent dans la [procédure de publication mobile](../../../../../../../Resources/Tools/Packaging/README.md#mobile-sdk-tooling).

<!-- docs-navigation -->

---

[Documentation développeur](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
