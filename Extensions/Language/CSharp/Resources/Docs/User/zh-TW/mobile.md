# Android/iOS 中使用 C# / MAUI

參考與原生 SDK 相符、包含行動目標的 `VeroScript.CSharp` 套件。使用 .NET 10 Android/iOS：Android API 26+（arm64-v8a/x86_64），iOS 15+（arm64 真機、arm64/x86_64 模擬器）。RID 為 `android-arm64`、`android-x64`、`ios-arm64`、`iossimulator-arm64`、`iossimulator-x64`。桌面 .NET 8/9 組件不能替代 iOS 靜態 P/Invoke 組件。接入仍處於實驗階段，請在模擬環境／模擬器和真機驗證應用。

NuGet 自動匯入原生及 MAUI 資源項目。使用原始碼 `ProjectReference` 時，須明確匯入 `buildTransitive/VeroScript.CSharp.targets`，透過 `VeroScriptMobileNativeRoot` 指定相符的已準備原生目錄。Android 部署 Interop 和一個相容的共享 C++ 執行階段；iOS 靜態連結 XCFramework，透過 `__Internal` 呼叫。應用程式最低 OS 與原生相依項目須相符。

MAUI 資產是串流。使用絕對的應用程式自有目錄及資產開啟器，在建立工作階段前完成 `VeroScriptMobileResources.InstallAsync`。安裝會驗證 SHA-256 和路徑、要求大小寫準確的 `stdlib/core.vs`、關閉串流，重試會修復舊檔案；失敗可能留下已完成檔案。使用安裝目錄中的 `stdlib`／`localization`，快取須可寫。同一工作執行緒建立、設定、執行並釋放 Session，結束後再向 UI 傳回結果。目錄載入後再選擇程序級語言。行動套件不包含原生 JIT。

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

[使用者文件](../../../../../../../Resources/Docs/User/zh-TW/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](mobile.md)
