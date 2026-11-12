# Android — Documentation développeur

Android maintient l’adaptateur Java/JNI et AAR/Prefab. Le dispatch UI et le cycle de vie restent dans l’adaptateur/hôte. JNI utilise des tableaux UTF-8 standard et un chargement/sélection explicite des catalogues ; les exceptions traduites ne doivent pas utiliser le UTF-8 modifié JNI. Le smoke JNI bureau utilise une JVM réelle et un AssetManager de test et ne valide pas un appareil Android.

La compilation embarquée commune, les durées de vie, les sorties de paquets et la localisation sont décrites dans [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/fr-FR/index.md).

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --out-root dist/mobile/android
```

Android exige JDK 17, Gradle 8.11.1, AGP 8.9.2, SDK 35, NDK 28.2.13676358, CMake 3.31.6. Vérifiez JNI/Interop/libc++ des deux ABI, identité Android/API et alignement ELF LOAD 16 Ko. Appareil/simulateur, R8 et ZIP APK final restent à vérifier.

[Documentation utilisateur](../../User/fr-FR/index.md)


## Ressources et localisation mobiles

L’inventaire commun des catalogues mobiles, leurs chemins de déploiement et leur chargement explicite sont décrits dans [Mobile](../../../../../VeroScriptMobile/Resources/Docs/Developer/fr-FR/index.md#mobile-localization).

La propriété des tampons traduits, les erreurs et la maintenance des exports sont définies dans [Interop](../../../../../../../../VeroScriptInterop/Resources/Docs/Developer/fr-FR/abi-and-lifetime.md#localization-abi).

[Validation cible de l’AAR Android](device-validation.md)

L’AAR Android de base active explicitement Application (`VEROSCRIPT_BUILD_APPLICATION=ON`), y compris en mode embedded-only. Conservez `assets/veroscript/licenses/MINIZ-LICENSE.txt`, `JSON-LICENSE.txt` et `SHA256-LICENSE.txt` avec LICENSE et les notices NDK. Le vérificateur mobile les exige ; les conteneurs applicatifs n’activent pas le client d’installation de paquets ni les outils desktop.

<a id="jni-host-fixture"></a>

Pour la fixture JNI desktop optionnelle, configurez `VEROSCRIPT_TEST_ANDROID_JNI_HOST=ON`, `BUILD_TESTING=ON` et un vrai JDK via JAVA_HOME. Construisez VeroScriptAndroidHostSmoke puis lancez l’entrée CTest VeroScriptAndroidJniHostSmoke. Elle utilise l’adaptateur JNI réel et un AssetManager de test pour UTF-8/NUL, imports, exceptions, annulation et libération sous `-Xcheck:jni` ; elle ne prouve aucun résultat Android ABI/API, APK ou appareil. La validation cible est dans [le guide appareil](device-validation.md).

<!-- docs-navigation -->

---

[Documentation développeur](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
