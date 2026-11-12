# iOS — Documentation développeur

iOS maintient l’adaptateur Objective-C/Objective-C++ et l’intégration XCFramework statique. Le dispatch UI et le cycle de vie restent dans l’adaptateur/hôte ; préservez l’identité appareil/simulateur de chaque slice.

La compilation embarquée commune, les durées de vie, les sorties de paquets et la localisation sont décrites dans [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/fr-FR/index.md).

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py ios --out-root dist/mobile/ios
```

iOS exige macOS/Xcode, trois slices et vérification `LC_BUILD_VERSION`/CPU/iOS minimal de chaque objet. Refusez les archives macOS ou mixtes. Liaison réelle Objective-C/Swift, signature, arrière-plan/reprise, annulation et mesures restent des gates.

[Documentation utilisateur](../../User/fr-FR/index.md)


## Ressources et localisation mobiles

L’inventaire commun des catalogues mobiles, leurs chemins de déploiement et leur chargement explicite sont décrits dans [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/fr-FR/index.md#mobile-localization).

La propriété des tampons traduits, les erreurs et la maintenance des exports sont définies dans [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/fr-FR/abi-and-lifetime.md#localization-abi).

<a id="ios-acceptance-evidence"></a>

Conservez les preuves par identité exacte des sources/artefacts, versions Xcode/SDK, cible minimale, OS exécuté et architecture appareil/simulateur. Construisez séparément arm64 appareil et arm64/x86_64 simulateur avec CMAKE_SYSTEM_NAME=iOS et le sysroot iphoneos/iphonesimulator correspondant ; les builds embarqués utilisent Interop statique. Vérifiez archives fusionnées et XCFramework avant les applications Objective-C++, Swift, ABI C et AOT statique. Contrôlez imports, pont NSError/factory, ressources Bundle, réutilisation du cache, refus de droits, annulation, analyse/récupération sur petite pile, ARC et arrière-plan/reprise dans une vraie application. Une slice appareil compilée n’est pas une exécution physique ; signature et captures du simulateur ne valident ni signature appareil, ni Metal natif, ni performance. Compiler pour iOS 15.0 ne prouve pas l’exécution sur iOS 15. Gardez les reçus et différences de sources dans les preuves temporaires, pas dans des paragraphes datés du manuel ; un reçu simulateur n’approuve jamais automatiquement un build ultérieur.

<!-- docs-navigation -->

---

[Documentation développeur](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
