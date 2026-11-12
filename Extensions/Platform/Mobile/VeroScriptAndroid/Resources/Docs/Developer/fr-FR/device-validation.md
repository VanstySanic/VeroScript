# Validation cible de l’AAR Android

## Prérequis et construction

Cette application autonome consomme l’AAR de base construit via Java/JNI et Prefab C11/C++17 ; elle ne compile pas un second Core. L’APK release active R8 et utilise une clé debug uniquement pour cette fixture. Utilisez Python 3.11+ et les versions JDK, Gradle, SDK, NDK et CMake du [guide Android](index.md). Construisez d’abord les artefacts correspondants dans une nouvelle sortie. `--with-asmjit` prépare aussi l’AAR JIT optionnel.

```text
python Resources/Tools/Packaging/package-veroscript-mobile.py android --with-asmjit --out-root dist/mobile/android-acceptance --gradle <Gradle-8.11.1-executable>
```

## Exécution sur une cible explicite

Démarrez un émulateur ou connectez un appareil autorisant le débogage et choisissez son numéro exact dans `adb devices`. Fournissez l’AAR de base correspondant ; l’exemple demande explicitement l’ABI d’installation x86_64.

```text
python Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device/run.py --aar <absolute-base.aar> --sdk <Android-Sdk> --gradle <Gradle-8.11.1-executable> --java-home <JDK-17> --serial <adb-serial> --abi x86_64 --out <report-directory>
```

Ajoutez `--jit-aar <absolute-jit.aar>` pour le backend optionnel et les compteurs natifs contrôlés. `--abi arm64-v8a` choisit ARM64. `--page-size 4096` ou `--page-size 16384` vérifie la taille native renvoyée par Bionic `sysconf` ; `--debug` choisit la version non minifiée. Couvrez API 26 et versions suivantes, et les deux tailles de pages, avec une sortie distincte par configuration. Activez CheckJNI séparément sur un émulateur de test pour JNI.

## Résultats et périmètre

Le pilote désinstalle/remplace seulement l’application de test déclarée dans [sa configuration de construction](../../../../Resources/Tests/Device/build.gradle.kts) et ses données, sans arrêter l’appareil/émulateur. Il conserve APK, journaux, `commands.json` et `device-smoke.json`. Il vérifie l’alignement ZIP APK 16 Ko, les cas renvoyés et `INSTRUMENTATION_CODE: -1` ; le code de sortie adb seul ne suffit pas. Chaque tentative remet d’abord `acceptancePassed` à false ; true exige tous les contrôles, dont ABI et taille de page demandées.

Les cas couvrent imports/noms Unicode, sorties UTF-8/NUL, arrondi float32, budgets mémoire/exécution/sortie, cache, capacités, récupération, backend explicite, annulation, concurrence refusée, fermeture, localisation et durée de vie du catalogue AOT JNI. Ce dernier n’exécute pas de code métier AOT généré. Une bibliothèque ARM64 traduite sur émulateur ne valide pas le matériel ARM64 physique ; conservez identité réelle et empreintes des artefacts. Cette fixture ne remplace pas les validations Python, C#, graphiques ou applicatives finales.

La régression Python ci-dessous simule les commandes natives. Elle valide la logique du pilote, pas Android. La présence de la fixture, une vérification hôte ou une construction seule ne constituent pas une validation cible.

```text
python -m unittest discover -s Extensions/Platform/Mobile/VeroScriptAndroid/Resources/Tests/Device -p test_run.py
```

<!-- docs-navigation -->

---

[Android](index.md) · [VeroScript](../../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

Langue: [en-US](../en-US/device-validation.md) · [fr-FR](device-validation.md) · [ja-JP](../ja-JP/device-validation.md) · [ko-KR](../ko-KR/device-validation.md) · [zh-CN](../zh-CN/device-validation.md) · [zh-TW](../zh-TW/device-validation.md)
