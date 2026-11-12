# Android/iOS の C# / MAUI

native SDK と一致する mobile 対応 `VeroScript.CSharp` package を参照します。.NET 10 Android/iOS、Android API 26 以上（arm64-v8a/x86_64）、iOS 15 以上（実機 arm64、simulator arm64/x86_64）です。RID は `android-arm64`、`android-x64`、`ios-arm64`、`iossimulator-arm64`、`iossimulator-x64`。desktop .NET 8/9 assembly では iOS の静的 P/Invoke assembly を代用できません。統合は実験段階です。アプリを simulator/emulator と実機で検証してください。

NuGet は native/MAUI 資源項目を自動 import します。ソースを `ProjectReference` で使う場合は `buildTransitive/VeroScript.CSharp.targets` を明示 import し、`VeroScriptMobileNativeRoot` で対応する準備済み native root を指定します。Android は Interop と互換な共有 C++ runtime を配備し、iOS は XCFramework を静的リンクして `__Internal` を呼びます。最低 OS と依存を一致させます。

MAUI asset は stream です。絶対パスのアプリ所有ディレクトリと asset opener を指定し、Session 作成前に `VeroScriptMobileResources.InstallAsync` を完了します。SHA-256/パスを検証し、大小文字が一致した `stdlib/core.vs` を要求し、stream を閉じ、再試行で古いファイルを修復します。失敗時には完了済みファイルが残る場合があります。`stdlib`/`localization` と書き込み可能な cache を使います。Session の作成、設定、実行、破棄は同じ worker thread で行い、終了後に UI へ結果を渡します。言語はプロセス共通で、カタログ読み込み後に選択します。native JIT は付属しません。

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

[ユーザー文書](../../../../../../../Resources/Docs/User/ja-JP/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
