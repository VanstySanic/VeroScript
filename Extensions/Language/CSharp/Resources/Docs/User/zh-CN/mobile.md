# Android/iOS 中使用 C# / MAUI

引用与原生 SDK 匹配、包含移动目标的 `VeroScript.CSharp` 包。使用 .NET 10 Android/iOS：Android API 26+（arm64-v8a/x86_64），iOS 15+（arm64 真机、arm64/x86_64 模拟器）。RID 为 `android-arm64`、`android-x64`、`ios-arm64`、`iossimulator-arm64`、`iossimulator-x64`。桌面 .NET 8/9 程序集不能替代 iOS 静态 P/Invoke 程序集。接入仍处于实验阶段，请在模拟环境／模拟器和真机验证应用。

NuGet 自动导入原生及 MAUI 资源项。使用源码 `ProjectReference` 时，须显式导入 `buildTransitive/VeroScript.CSharp.targets`，通过 `VeroScriptMobileNativeRoot` 指定匹配的已准备原生目录。Android 部署 Interop 和一个兼容的共享 C++ 运行库；iOS 静态链接 XCFramework，通过 `__Internal` 调用。应用最低 OS 与原生依赖须匹配。

MAUI 资产是流。使用绝对的应用自有目录及资产打开器，在创建会话前完成 `VeroScriptMobileResources.InstallAsync`。安装会校验 SHA-256 和路径、要求大小写准确的 `stdlib/core.vs`、关闭流，重试会修复旧文件；失败可能留下已完成文件。使用安装目录中的 `stdlib`／`localization`，缓存须可写。同一工作线程创建、配置、执行并释放 Session，结束后再向 UI 返回结果。目录加载后再选择进程级语言。移动包不包含原生 JIT。

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

[用户文档](../../../../../../../Resources/Docs/User/zh-CN/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](../ko-KR/mobile.md) · [zh-CN](mobile.md) · [zh-TW](../zh-TW/mobile.md)
