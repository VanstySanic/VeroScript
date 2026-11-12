# Python sur Android et iOS

Utilisez un SDK d’intégration correspondant à la version mineure du CPython cible. Android : CPython 3.13/3.14 release avec GIL, arm64-v8a/x86_64, API 26+. iOS : mêmes versions Python, iOS 15+, appareil arm64 et simulateur arm64/x86_64. Un wheel desktop ne remplace pas ce SDK. L’application fournit CPython et ses dépendances. L’intégration est expérimentale ; vérifiez l’application sur simulateur/émulateur et appareil avant déploiement.

Sur Android, déployez `jniLibs/<abi>/` et les bibliothèques CPython cibles. Utilisez un seul `libc++_shared.so` compatible. Conservez `python/veroscript/`, `stdlib/`, `localization/` et `py.typed` dans un répertoire extrait appartenant à l’application. Avant l’import, définissez `VEROSCRIPT_ANDROID_NATIVE_LIB_DIR` avec le chemin absolu `ApplicationInfo.nativeLibraryDir` de l’application installée et ajoutez à `sys.path` le dossier contenant `veroscript/`. L’application doit extraire les bibliothèques JNI (`useLegacyPackaging = true`) ; un nom d’asset APK n’est pas un chemin de bibliothèque.

Sur iOS, intégrez et signez les `Python.xcframework` et `VeroScriptPython.xcframework` correspondants. Conservez dans le bundle `python/veroscript/`, `_veroscript.fwork` et le marqueur `.origin` du framework. Ajoutez `<app-bundle>/python` à `sys.path` et désactivez l’écriture du bytecode dans le bundle en lecture seule. Le chargeur Apple du CPython cible résout l’extension préparée ; ne remplacez pas celle-ci par un `.so` desktop et ne déplacez pas les ressources sans adapter l’origine.

L’import de la façade charge ses catalogues ; appelez ensuite `set_language(tag)`. La sélection est globale au processus. Gardez la bibliothèque standard près de la façade et les caches dans un répertoire réel accessible en écriture. Créez, configurez, exécutez et fermez chaque Session sur un même thread worker ; retournez ensuite les résultats à l’interface. Les callbacks appliquent leurs propres permissions et annulation. Aucun JIT natif n’est livré ; utilisez VM pour l’exécution contrôlée.

```python
import threading
from concurrent.futures import ThreadPoolExecutor
from veroscript import Backend, ScriptCapabilities, Session, set_language

threading.stack_size(8 * 1024 * 1024)

def run_script():
	set_language("zh-CN")
	output = []
	with Session(backend=Backend.VM, print_callback=output.append,
		max_instructions=100_000, timeout_milliseconds=5_000,
		max_call_depth=256, enable_execution_control=True,
		capability_policy_enabled=True,
		allowed_script_capabilities=ScriptCapabilities.CONSOLE_OUTPUT) as session:
		session.set_memory_limits(16 * 1024 * 1024, 32 * 1024 * 1024)
		loaded = session.load_source('import "core.vs"; void main() { print(42); }')
		if not loaded.success:
			raise RuntimeError(loaded.diagnostics)
		result = session.run()
		if not result.success:
			raise RuntimeError(result.diagnostics)
	return output

with ThreadPoolExecutor(max_workers=1) as worker:
	output = worker.submit(run_script).result()
```

[Session](session.md) · [Localization](packages.md)

Compilez le source sur un worker disposant d’une pile native d’au moins 8 MiB ; créez, utilisez et fermez chaque Session sur ce même worker. Configurez la pile avant de démarrer les threads Python de l’application. La liaison ne déplace pas les Sessions existantes ni leurs rappels vers un autre thread.

<a id="mobile-compiler-thread"></a>

Pour ce profil embarqué mobile, fournissez au moins 8 Mio de pile native au compilateur. Configurez `threading.stack_size` avant de créer les workers Python ; cela concerne les nouveaux threads Python, pas un thread natif/JNI/dispatch existant. Si l’hôte fournit directement le worker, configurez sa pile native. Créez, utilisez et fermez chaque Session sur le même worker ; les sessions existantes ne sont pas migrées automatiquement. L’exemple attend le résultat et doit s’exécuter hors du thread UI. Sa sortie est `["42"]`. Pile native, tampons et quotas d’exécution VM sont indépendants.

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
