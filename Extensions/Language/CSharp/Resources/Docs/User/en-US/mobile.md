# C# / MAUI on Android and iOS

Reference a mobile-enabled `VeroScript.CSharp` package matching the native SDK. It uses .NET 10 Android/iOS targets, Android API 26+ (arm64-v8a/x86_64) and iOS 15+ (arm64 devices; arm64/x86_64 simulators). RIDs are `android-arm64`, `android-x64`, `ios-arm64`, `iossimulator-arm64`, `iossimulator-x64`. Desktop .NET 8/9 assemblies cannot replace the iOS-specific static P/Invoke assembly. These integrations remain experimental; validate your application on emulator/simulator and device.

NuGet imports native and MAUI resource items automatically. When consuming source through `ProjectReference`, import `buildTransitive/VeroScript.CSharp.targets` explicitly and provide the matching prepared native root with `VeroScriptMobileNativeRoot`. Android deploys Interop and one compatible shared C++ runtime; iOS statically links the XCFramework and calls `__Internal`. Keep minimum OS settings and native dependencies compatible.

MAUI package assets are streams. Complete `VeroScriptMobileResources.InstallAsync` before creating sessions, using an absolute app-owned directory and an asset opener. It verifies SHA-256 and paths, requires the case-sensitive `stdlib/core.vs`, closes streams, and repairs stale files on retry. Failure may leave previously completed files. Use its `stdlib` and `localization` directories; caches must be writable. Create, configure, run and dispose each Session on one worker thread. Return results to the UI afterwards. Language selection is process-wide; select it after loading catalogs. Mobile packages ship no native JIT.

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

[User documentation](../../../../../../../Resources/Docs/User/en-US/index.md)

[en-US](mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
