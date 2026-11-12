# C# / MAUI sur Android et iOS

Référencez un paquet mobile `VeroScript.CSharp` correspondant au SDK natif. Cibles .NET 10 Android/iOS, API 26+ Android (arm64-v8a/x86_64) et iOS 15+ (appareil arm64, simulateur arm64/x86_64). RIDs : `android-arm64`, `android-x64`, `ios-arm64`, `iossimulator-arm64`, `iossimulator-x64`. L’assembly desktop .NET 8/9 ne remplace pas celui d’iOS avec P/Invoke statique. Intégration expérimentale : vérifiez l’application sur émulateur/simulateur et appareil.

NuGet importe automatiquement les ressources natives et MAUI. Avec `ProjectReference`, importez explicitement `buildTransitive/VeroScript.CSharp.targets` et fournissez la racine native préparée via `VeroScriptMobileNativeRoot`. Android déploie Interop et un runtime C++ partagé compatible ; iOS lie statiquement le XCFramework et appelle `__Internal`. Respectez les OS minimaux et dépendances.

Les assets MAUI sont des streams. Terminez `VeroScriptMobileResources.InstallAsync` avant création des sessions, avec un dossier absolu appartenant à l’application et un ouvreur d’assets. L’installation vérifie SHA-256/chemins, exige `stdlib/core.vs` avec cette casse, ferme les streams et répare les fichiers au nouvel essai. Un échec peut conserver les fichiers déjà terminés. Utilisez `stdlib`/`localization` et des caches accessibles en écriture. Créez, configurez, exécutez et libérez chaque Session sur le même thread worker ; retournez ensuite les résultats à l’interface. La langue est globale au processus et se choisit après chargement des catalogues. Aucun JIT natif n’est livré.

```csharp
using VeroScript;

string root = await VeroScriptMobileResources.InstallAsync(
	Path.Combine(FileSystem.AppDataDirectory, "veroscript-0.1.1"),
	(name, _) => FileSystem.OpenAppPackageFileAsync(name));

string output = await Task.Run(() =>
{
	VeroScriptRuntime.LoadCatalogDirectory(Path.Combine(root, "localization"));
	VeroScriptRuntime.CurrentLanguage = "zh-CN";
	var lines = new List<string>();
	using var session = new VeroScriptSession(new()
	{
		Print = lines.Add
	});
	session.SetMemoryLimits(new(16UL * 1024 * 1024, 32UL * 1024 * 1024));
	session.ApplyConfiguration(new()
	{
		ExecutionBackend = VeroScriptExecutionBackend.Vm,
		StandardLibrarySearchPaths = [Path.Combine(root, "stdlib")],
		MaxInstructions = 100_000, TimeoutMilliseconds = 5_000,
		MaxCallDepth = 256, EnableExecutionControl = true,
		CapabilityPolicyEnabled = true,
		AllowedScriptCapabilities = VeroScriptScriptCapabilities.ConsoleOutput
	});
	var loaded = session.LoadSource("import \"core.vs\"; void main() { print(42); }");
	if (!loaded.Success || !session.Run().Success)
	{
		throw new InvalidOperationException(session.GetLastError());
	}
	return string.Join("\n", lines);
});
```

[Session](session.md)

<!-- docs-navigation -->

---

[Documentation utilisateur](../../../../../../../Resources/Docs/User/fr-FR/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
