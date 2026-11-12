# Android/iOS의 C# / MAUI

native SDK와 일치하는 mobile 지원 `VeroScript.CSharp` package를 참조하세요. .NET 10 Android/iOS, Android API 26+(arm64-v8a/x86_64), iOS 15+(기기 arm64, simulator arm64/x86_64)입니다. RID는 `android-arm64`, `android-x64`, `ios-arm64`, `iossimulator-arm64`, `iossimulator-x64`입니다. desktop .NET 8/9 assembly로 iOS 정적 P/Invoke assembly를 대체할 수 없습니다. 실험적 통합이므로 emulator/simulator와 기기에서 앱을 검증하세요.

NuGet은 native/MAUI 리소스 항목을 자동 import합니다. 소스를 `ProjectReference`로 사용하면 `buildTransitive/VeroScript.CSharp.targets`를 명시적으로 import하고 `VeroScriptMobileNativeRoot`로 일치하는 준비된 native root를 제공합니다. Android는 Interop과 하나의 호환 공유 C++ runtime을 배포하며 iOS는 XCFramework를 정적으로 링크하고 `__Internal`을 호출합니다. 최소 OS와 의존성을 맞추세요.

MAUI asset은 stream입니다. 절대 앱 소유 디렉터리와 asset opener를 지정하고 Session 생성 전에 `VeroScriptMobileResources.InstallAsync`를 완료하세요. SHA-256/경로 검증, 대소문자가 정확한 `stdlib/core.vs` 요구, stream 해제 및 재시도 시 오래된 파일 복구를 수행합니다. 실패 시 이미 완료된 파일은 남을 수 있습니다. `stdlib`/`localization` 및 쓰기 가능한 cache를 사용하세요. Session 생성, 설정, 실행과 해제는 같은 worker thread에서 하고 결과를 UI에 전달합니다. 언어는 프로세스 전체에 적용되며 카탈로그를 읽은 뒤 선택합니다. native JIT는 제공되지 않습니다.

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

[사용자 문서](../../../../../../../Resources/Docs/User/ko-KR/index.md)

[en-US](../en-US/mobile.md) · [fr-FR](../fr-FR/mobile.md) · [ja-JP](../ja-JP/mobile.md) · [ko-KR](mobile.md) · [zh-CN](../zh-CN/mobile.md) · [zh-TW](../zh-TW/mobile.md)
