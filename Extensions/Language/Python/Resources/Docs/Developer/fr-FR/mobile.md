# Python — Mobile SDK

Matérialisez les sous-répertoires d'en-têtes : les liens symboliques de répertoire et les jonctions/répertoires reparse Windows sous `includeDirectory` sont refusés. `includeDirectory` peut lui-même pointer vers un répertoire réel du SDK. Les liens vers des fichiers `.h` restent autorisés si leur cible est dans le SDK ; leur contenu participe à son empreinte.

Cette page s’adresse aux mainteneurs qui construisent et publient les paquets mobiles Python avec `Extensions/Language/Python/Resources/Tools/Mobile`. Fournissez explicitement le SDK CPython cible ; un interpréteur hôte ou un binaire desktop/macOS ne remplace pas les ressources cibles. Construisez dans un nouveau dossier, vérifiez l’inventaire complet et recueillez les preuves sur plateforme cible avant une publication stable.

Python : CPython hôte de même version majeure/mineure, CMake 3.25+, NDK/Ninja Android ou macOS/Xcode complet. Cible CPython 3.13/3.14 release avec GIL ; un paquet par version mineure et même version complète pour toutes les slices. Rejetez debug/free-threaded. Les chemins du descripteur restent dans son dossier. Champs : `schemaVersion`, `platform`, `architecture`, `variant`, `minimumVersion`, `includeDirectory`, `library`, `sysconfigData`. Lecture des en-têtes et du dictionnaire littéral `build_time_vars` via AST, sans exécuter la cible. Vérifiez plateforme, architecture, OS minimal, ABI Python et nom d’installation iOS. Android exige arm64-v8a/x86_64 (API 26) ; iOS arm64 appareil et arm64/x86_64 simulateur (15.0).

Python livre extension/façade/ressources ; l’application fournit CPython et dépendances. Android utilise les chemins JNI extraits ; iOS des frameworks d’extension Python dynamiques signés avec `.fwork`/`.origin`. Ce profil diffère du XCFramework C ABI statique et du JIT natif interdit. Chaque slice reçoit un nonce et une identité native neufs. `python-mobile.json` conserve hashes, versions, plateforme/ABI, empreinte SDK et état appareil en attente.

Auditez inventaire et binaires avant publication. `device_smoke.py` refuse desktop et invalide les preuves précédentes avant chaque essai. Elles doivent correspondre au build ID, aux versions exactes Python/produit, à l’ABI et au type appareil/simulateur. Toutes les slices sont requises ; arm64 Android exige systèmes à pages 4 et 16 Ko. Le smoke VM contrôlé teste imports, callbacks, diagnostics, valeurs conservées et libération. Applications signées, alignement des dépendances/ZIP APK, chemins frameworks iOS, ressources/signature, annulation et tests applicatifs ont leurs propres validations. Les tests hôte ne les remplacent pas.

[Python](index.md)


## Documentation publiée

Les paquets mobiles contiennent les fichiers fonctionnels, les ressources d’exécution et les mentions légales. Ils excluent README, manuels et exemples ; la documentation User intégrée est disponible dans la distribution Resources séparée. Les chemins des ressources natives et les exigences de validation sur appareil restent inchangés.


## Descripteurs et reproduction

Exemples Android arm64 et iOS appareil ci-dessous. Les noms sysconfigData sont indicatifs : sélectionnez les fichiers réels du SDK, jamais un fichier renommé d’une autre ABI. Pour x86_64 Android, adaptez `architecture` et tous les chemins ; les simulateurs iOS utilisent `variant: simulator`, `architecture: arm64` ou `x86_64` et un binaire mince. Les versions exactes doivent correspondre entre toutes les slices. Préservez `@rpath/Python.framework/Python`.

Depuis la racine du dépôt, `--host-python` choisit l’interpréteur hôte et `--cmake` le pilote. `--plan` vérifie les SDK et imprime les commandes sans compiler ; le plan Android exige aussi `--ndk`. La sortie doit être nouvelle, les builds intermédiaires sont temporaires, et chaque build reçoit un nonce neuf. Conservez les licences CPython/dépendances en plus des notices livrées.

Les tests hôte `Extensions/Language/Python/VeroScriptPython/tests/test_mobile.py` vérifient AST, configuration hôte/cible, simulateur universel et preuves. Le smoke appareil utilise VM/Mixed sous contrôle (100 000 instructions, 5 s, 256 frames, sortie console seule), refuse desktop, reste actif sous `-O` et vérifie identité native, ABI et versions. Il ne remplace pas les tests CPython/applicatifs ni les audits de l’application signée.

```json
{
	"schemaVersion": 1,
	"platform": "android",
	"architecture": "arm64-v8a",
	"variant": "device",
	"minimumVersion": "26",
	"includeDirectory": "include/python3.14",
	"library": "lib/libpython3.14.so",
	"sysconfigData": "lib/python3.14/_sysconfigdata__android_aarch64-linux-android.py"
}
```

```json
{
	"schemaVersion": 1,
	"platform": "ios",
	"architecture": "arm64",
	"variant": "device",
	"minimumVersion": "15.0",
	"includeDirectory": "Python.framework/Headers",
	"library": "Python.framework/Python",
	"sysconfigData": "stdlib/_sysconfigdata__ios_arm64-apple-ios.py"
}
```

[Python](../../User/fr-FR/mobile.md)

## Construire avec les descripteurs

Les outils sont implémentés, mais ne constituent pas une preuve de compilation croisée ou d’exécution sur appareil. Les SDK Python cibles doivent être fournis explicitement. L’hôte CPython a le même major/minor ; la cible est une version release avec GIL. Les outils Frontend/BindGen/Transpiler/AOT restent sur l’hôte. Core, support C++ et Interop sont liés statiquement dans l’extension ; VM et Mixed fonctionnent sans module JIT natif.

Android requiert NDK/Ninja et les deux SDK cibles. Vérifiez les segments LOAD/RELRO 16 Ko de libpython et de l’extension, ainsi qu’un `libc++_shared.so` compatible. iOS exige macOS et Xcode complet. Un Python.framework simulateur universel doit fournir des entrées minces arm64/x86_64 aux descripteurs, par exemple via `xcrun lipo -thin`. Le nom d’installation reste `@rpath/Python.framework/Python`.

Exécutez depuis la racine du dépôt. Choisissez **une** commande de paquet correspondant à votre plateforme ; chaque chemin entre chevrons doit être remplacé par un SDK ou une destination réelle. Le contrôle du SDK ne lance jamais Python cible.

```text
python Extensions/Language/Python/Resources/Tools/Mobile/mobile_sdk.py --sdk <sdk.json> --check-host
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py android --sdk <android-arm64/sdk.json> --sdk <android-x86_64/sdk.json> --ndk <ndk-directory> --out-root <new-output-directory>
python Extensions/Language/Python/Resources/Tools/Mobile/package_mobile.py ios --sdk <ios-device/sdk.json> --sdk <ios-simulator-arm64/sdk.json> --sdk <ios-simulator-x86_64/sdk.json> --out-root <new-output-directory>
```

## Déployer Android

Copiez chaque `slices/<target>/jniLibs/<abi>/` dans `src/main/jniLibs/<abi>/`, ainsi que le libpython cible et ses dépendances selon le fournisseur CPython. Utilisez un seul `libc++_shared.so` compatible dans l’application ; `pickFirst` ne corrige pas une incompatibilité. Copiez `python/veroscript/` d’une slice correspondante dans les ressources Python de l’application, extrayez-les si ce sont des assets APK, et conservez `stdlib/`, `localization/`, `py.typed`.

Le chargeur exige un vrai fichier extrait. Configurez le module Gradle de l’application, puis l’interpréteur avant le premier import :

```kotlin
android {
    defaultConfig { minSdk = 26 }
    packaging { jniLibs { useLegacyPackaging = true } }
}
```

```python
import os
import sys

os.environ["VEROSCRIPT_ANDROID_NATIVE_LIB_DIR"] = native_library_dir
sys.path.insert(0, application_python_directory)
import veroscript
```

`native_library_dir` est le chemin absolu `context.applicationInfo.nativeLibraryDir` de l’application installée ; vérifiez qu’il contient les bibliothèques. `application_python_directory` est le dossier déployé contenant `veroscript/`. Le chargeur utilise `veroscript._veroscript` avec `libveroscript_python.so`. Testez l’APK installé, pas un chemin de la machine de build.

## Déployer iOS

Ajoutez le **Python.xcframework** externe et le **VeroScriptPython.xcframework** produit avec **Embed & Sign**. Xcode choisit appareil ou simulateur universel. Le framework d’extension fourni est non signé ; la phase d’intégration de l’application le signe. Conservez `@executable_path/Frameworks` dans les runpaths et cette disposition :

```text
Application.app/
  Frameworks/Python.framework/Python
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript
  Frameworks/veroscript._veroscript.framework/veroscript._veroscript.origin
  python/veroscript/__init__.py
  python/veroscript/_veroscript.fwork
  python/veroscript/stdlib/...
  python/veroscript/localization/...
```

Configurez CPython et sa bibliothèque standard séparément. Ajoutez `<app-bundle>/python` à la recherche de modules et désactivez l’écriture des bytecodes dans le bundle en lecture seule. `AppleFrameworkLoader` lit `_veroscript.fwork` ; préservez le marqueur et `.origin`, communs aux deux architectures simulateur. Ne déplacez pas vers `app_packages` sans modifier l’origine, ne copiez pas de `.so` dans les ressources et ne retraitez pas ces marqueurs déjà préparés.

## Audit et preuves de plateforme

L’audit compare tout l’inventaire, la façade, la bibliothèque standard et chaque catalogue à la version source correspondante. Il vérifie initialiseur CPython, dépendances, architecture/API/alignement Android, plateforme/OS/framework/Info.plist et marqueurs iOS, puis les membres XCFramework. Les payloads natifs supplémentaires sont rejetés même ajoutés à l’inventaire. Exécutez depuis ce checkout ou la racine `_native` du sdist correspondant ; les hashes du paquet seuls ne prouvent pas la présence des ressources requises.

Exécutez d’abord l’audit sans preuve. Copiez `device_smoke.py` dans le harnais de l’application, puis appelez `run` sur un thread worker de l’interpréteur mobile avec un chemin de rapport accessible en écriture. Exportez un rapport pour chaque slice et les deux environnements de pages Android arm64 ; ajoutez autant de `--evidence` que nécessaire.

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory>
```

```python
from device_smoke import run
run(writable_report_path)
```

```text
python Extensions/Language/Python/Resources/Tools/Mobile/verify_mobile.py <package-directory> --require-device-tests --evidence <report-1.json> --evidence <report-2.json> --evidence <report-3.json>
```

Les nouveaux builds exigent de nouvelles preuves. L’identité provient de `veroscript.HOST_PLATFORM` et la version sémantique de `veroscript.__version__`, pas du nom matériel du noyau ni de `RUNTIME_VERSION` qui inclut le bytecode. `pending` dans `python-mobile.json` n’est pas une validation. Avant publication, auditez toutes les dépendances natives Android, contrôlez l’APK avec `zipalign -v -c -P 16 4 <application.apk>` et testez l’application signée issue de l’AAB. Sur iOS, contrôlez signature, ressources, runpaths et import sur appareil et les deux ABI simulateur. Conservez les licences CPython et dépendances. Ces étapes ne remplacent pas les tests CPython/applicatifs et ne prouvent pas la conformité aux boutiques.

## Identité de build et bibliothèques Android

La version du produit provient de `VeroScriptVersion.h` ; ne la remplacez pas par la version CPython ni par une chaîne d’exécution composite. Sur Android, confirmez que les bibliothèques sont extraites dans `ApplicationInfo.nativeLibraryDir`, puis transmettez ce chemin absolu à `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR` avant le premier `import veroscript`.

<a id="android-build-contract"></a>

Les builds Android activent explicitement `VEROSCRIPT_BUILD_APPLICATION=ON` ; conservez `licenses/application/miniz/LICENSE`, `licenses/application/json/LICENSE.MIT` et `licenses/application/sha256/LICENSE` dans `python/veroscript/`. La validation compare les valeurs NDK résolues `ANDROID_ABI` / `ANDROID_PLATFORM_LEVEL`, sinon les champs Android/système de CMake ; `CMAKE_SYSTEM_VERSION=1` du NDK historique n’est pas l’API cible. L’audit 16 Ko vérifie LOAD et l’absence de données inscriptibles hors RELRO dans les pages protégées arrondies ; il n’exige pas une fin RELRO toujours alignée à 16 Ko. Le STL partagé NDK peut avoir un suffixe RELRO non aligné.

<a id="compiler-nesting-acceptance"></a>

## Pile native du compilateur

L’hôte doit compiler sur un worker dont la pile native atteint au moins 8 MiB. Les petites piles JNI Android et Apple dispatch peuvent déborder avant le diagnostic de profondeur du parseur. Créez, utilisez et détruisez chaque Session sur ce même worker ; ne déplacez pas implicitement une Session existante ou ses callbacks. Configurez les piles avant de créer les threads Python, ou fournissez-les dans l’hôte natif. Exécutez aussi device_smoke.run sur ce worker. Les quatre groupes vérifient la profondeur 200, le diagnostic de limite à 512 puis la réutilisation. Les preuves doivent inclure compilerNestingChecked=true ; les anciennes preuves sans ce contrôle sont refusées.

`compilerNestingChecked` est une métadonnée de preuve, pas une API Session publique. `verify_mobile.py::verify_package` exige le booléen true avec schemaVersion 1 et les identités de build/runtime/ABI/slice/artefact correspondantes ; run ne l’émet qu’après le succès de exercise_runtime. La compilation Python Session/ABI C n’utilise pas l’adaptateur de pile Mobile::FRunner. Fournissez au code un report_file absolu dans un répertoire accessible en écriture appartenant à l’application ; appelez ce superviseur bloquant hors du thread UI. Les régressions simulées valident la logique, pas un binaire cible.

```python
import threading
from concurrent.futures import ThreadPoolExecutor
import device_smoke

def validate_on_worker(report_file):
	threading.stack_size(8 * 1024 * 1024)
	with ThreadPoolExecutor(max_workers=1) as worker:
		return worker.submit(device_smoke.run, report_file).result()
```

<!-- docs-navigation -->

---

[Documentation développeur](../../../../../../../Resources/Docs/Developer/fr-FR/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
