# Android — Intégration mobile expérimentale

L’AAR expérimental cible `arm64-v8a` et `x86_64`, API 26+, VM et NDK `c++_shared`. Ajoutez-le à l’app avec des dépendances natives compatibles. Il contient `assets/veroscript/stdlib`, en-têtes C ABI Prefab et licences ; placez les scripts dans `src/main/assets/scripts`. `scripts/helper.vs` devient `/veroscript/scripts/helper.vs`. Créez/exécutez/fermez sur un worker et gardez l’engine pour `requestCancel()`. Utilisez `executeAsset` ou `execute(source, absoluteLabel)`. État neuf à chaque appel, concurrence refusée. `close()` attend et est répétable ; ne l’appelez pas synchroniquement dans un callback de l’engine. Les octets UTF-8 préservent NUL et caractères supplémentaires ; vérifiez `outputTruncated`.

Importez le type [VeroScriptEngine](https://github.com/VanstySanic/VeroScript/blob/main/Extensions/Platform/Mobile/VeroScriptAndroid/src/main/java/com/vanstysanic/veroscript/android/VeroScriptEngine.java) du SDK. Ce fragment de méthode s’exécute sur un worker et suppose un `context` Android disponible.

```java
try (VeroScriptEngine engine = new VeroScriptEngine(context.getAssets(), "scripts"))
{
	VeroScriptEngine.Result result = engine.executeAsset("scripts/main.vs");
	// Inspect result.success, output, diagnostics and outputTruncated.
}
```

[Intégration mobile expérimentale](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/fr-FR/index.md)

## Localisation mobile explicite

[Localisation mobile explicite](../../../../../../Mobile/VeroScriptMobile/Resources/Docs/User/fr-FR/index.md)

## Quotas du stockage d’octets géré

Android expose ces quotas dans les derniers paramètres `maxBufferBytes` / `maxRetainedBytes` du constructeur complet de `VeroScriptEngine`. Le `long` Java porte les bits uint64 ; utilisez des tailles positives pour les budgets ordinaires. Les valeurs par défaut sont 16 MiB / 32 MiB ; zéro signifie illimité.

## Exécution native contrôlée et AOT statique

Voir [Intégration mobile expérimentale — Exécution native contrôlée et AOT statique](../../../../../VeroScriptMobile/Resources/Docs/User/fr-FR/index.md#mobile-jit-selection).

<a id="android-native-selection"></a>

Android accepte `ExecutionBackend`, `jitBackendPath`, un catalogue `AotPrograms` créé par l’application et `aotProgramId` ; utilisez `executePackage` pour les applications générées. L’amorçage natif utilise `VeroScriptAndroid.h`. Les catalogues conservent les programmes ; le code généré reste lié statiquement et les ID ne contiennent jamais d’adresses de fonctions.

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/index.md) · [fr-FR](index.md) · [ja-JP](../ja-JP/index.md) · [ko-KR](../ko-KR/index.md) · [zh-CN](../zh-CN/index.md) · [zh-TW](../zh-TW/index.md)
