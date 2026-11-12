// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;

namespace VeroScriptInstaller.Core;

public static class InstallerRootKinds
{
	public const string Programs = "programs";
	public const string Developer = "developer";
}

public sealed record InstallerComponentDefinition(
	string Id,
	string DisplayName,
	string Description,
	string InstallDirectory = ".",
	bool Required = false,
	bool DefaultSelected = true,
	IReadOnlyList<string>? Dependencies = null,
	IReadOnlyList<string>? RequiredFiles = null,
	IReadOnlyList<string>? RequiredExtensions = null,
	IReadOnlyList<string>? RequiredFilePrefixes = null,
	IReadOnlyList<string>? RequiredExecutables = null,
	IReadOnlyDictionary<string, string>? LocalizedNames = null,
	IReadOnlyDictionary<string, string>? LocalizedDescriptions = null,
	string? DisplayNameResourceKey = null,
	string? DescriptionResourceKey = null);

public sealed record InstallerProductDefinition(
	string ProfileId,
	string ProductId,
	string ProductName,
	string Description,
	string RootKind,
	string RootDirectoryName,
	bool IncludeVersion,
	IReadOnlyDictionary<string, InstallerComponentDefinition> Components,
	string? PathComponentId = null,
	string PathRelativeDirectory = ".",
	bool PathDefaultEnabled = false,
	IReadOnlyDictionary<string, string>? LocalizedNames = null,
	IReadOnlyDictionary<string, string>? LocalizedDescriptions = null,
	string? DescriptionResourceKey = null,
	string ReleaseGroup = "");

public static class InstallerProductCatalog
{
	public const string RuntimeProfileId = "runtime";
	public const string CoreSdkProfileId = "core-sdk";
	public const string InteropProfileId = "interop";
	public const string CppProfileId = "cpp";
	public const string CSharpProfileId = "csharp";
	public const string PythonProfileId = "python";
	public const string WebAssemblyProfileId = "web";
	public const string AsmJitProfileId = "jit-asmjit";

	private static readonly IReadOnlyDictionary<string, InstallerProductDefinition> Definitions =
		new ReadOnlyDictionary<string, InstallerProductDefinition>(
			new Dictionary<string, InstallerProductDefinition>(StringComparer.Ordinal)
			{
				[RuntimeProfileId] = Product(
					RuntimeProfileId, "dev.veroscript.runtime", "VeroScript",
					"VeroScript language runtime, command-line tools and standard library.",
					InstallerRootKinds.Programs,
					[
						Component("runtime", "VeroScript Runtime", "Language runtime, CLI, LSP, DAP and standard library.", "Runtime",
							required: true, requiredFiles: RuntimeFiles(), requiredExecutables: RuntimeExecutables()),
						Component("examples", "Examples", "Example VeroScript source files.", "examples", defaultSelected: false),
						Component("docs", "Documentation", "Offline language and tooling documentation.", "docs", defaultSelected: false),
					], "runtime"),
				[CoreSdkProfileId] = Product(
					CoreSdkProfileId, "dev.veroscript.sdk", "VeroScript Core SDK",
					"Native headers, libraries, tooling APIs and build-system integration.",
					InstallerRootKinds.Developer,
					[
						Component("sdk", "Core SDK", "Core native SDK and development tools.", "Core", required: true,
							requiredFiles:
							[
								"include/Script/VeroScript.h",
								"include/Script/VeroScriptJit.h",
								"include/Script/VeroScriptRun.h",
								"include/Script/VeroScriptValue.h",
								"include/Script/VeroScriptVm.h",
								"include/Vm/VeroScriptBytecodeVm.h",
								PlatformLibrary("lib/VeroScript"),
								PlatformLibrary("lib/VeroScriptBytecodeVm"),
								"lib/cmake/VeroScript/VeroScriptConfig.cmake",
								"lib/cmake/VeroScriptBytecodeVm/VeroScriptBytecodeVmConfig.cmake",
								"lib/cmake/VeroScriptExecutionContract/VeroScriptExecutionContractConfig.cmake",
								"bin/" + RuntimeVmLibrary(),
							]),
						Component("examples", "Examples", "Core embedding and script examples.", "examples", defaultSelected: false),
						Component("docs", "Documentation", "Offline SDK reference documentation.", "Docs", defaultSelected: false),
						Component("symbols", "Debug Symbols", "Matching native debug symbols.", "Symbols", defaultSelected: false,
							requiredExtensions: SymbolExtensions()),
					]),
				[InteropProfileId] = Product(
					InteropProfileId, "dev.veroscript.interop", "VeroScript Interop SDK",
					"Stable C ABI for embedding VeroScript from other languages.",
					InstallerRootKinds.Developer,
					[
						Component("interop-sdk", "Interop SDK", "C ABI headers, library and runtime binary.", "Core", required: true,
							requiredFiles: OperatingSystem.IsWindows()
								? ["include/VeroScript/VeroScriptInterop.h", "lib/VeroScriptInterop.lib", "bin/VeroScriptInterop.dll"]
								: ["include/VeroScript/VeroScriptInterop.h", "lib/" + PlatformSharedLibrary()]),
						Component("examples", "Examples", "Minimal C ABI embedding examples.", "examples", defaultSelected: false),
						Component("docs", "Documentation", "Offline Interop SDK documentation.", "docs", defaultSelected: false),
						Component("symbols", "Debug Symbols", "Matching Interop native debug symbols.", "Symbols", defaultSelected: false,
							requiredExtensions: SymbolExtensions()),
					]),
				[AsmJitProfileId] = Product(
					AsmJitProfileId, "dev.veroscript.jit.asmjit", "VeroScript AsmJit Backend",
					"Independent JIT backend. Select its installed file or directory when starting a host.",
					InstallerRootKinds.Programs,
					[
						Component("jit-backend", "AsmJit Backend", "External JIT backend and its runtime dependencies.",
							required: true, requiredFiles: [OperatingSystem.IsWindows() ? "VeroScriptJitAsmJit.dll"
								: OperatingSystem.IsMacOS() ? "libVeroScriptJitAsmJit.dylib" : "libVeroScriptJitAsmJit.so"]),
					]) with { DescriptionResourceKey = "jit.Description", ReleaseGroup = "Modules" },
				[CppProfileId] = LanguageProduct(CppProfileId, "cpp", "C++", "VeroScript C++", "VeroScript C++ SDK and tools. Requires the matching Core SDK."),
				[CSharpProfileId] = LanguageProduct(CSharpProfileId, "csharp", "C#", "VeroScript C#", "VeroScript C# runtime and tools, including the matching native Interop runtime."),
				[PythonProfileId] = LanguageProduct(PythonProfileId, "python", "Python", "VeroScript Python", "VeroScript Python runtime and tools."),
				[WebAssemblyProfileId] = Product(
					WebAssemblyProfileId, "dev.veroscript.web", "VeroScript WebAssembly SDK",
					"WebAssembly engine, JavaScript/TypeScript API and declarations.",
					InstallerRootKinds.Developer,
					[
						Component("web-sdk", "WebAssembly SDK", "WebAssembly engines and JavaScript/TypeScript API.", required: true,
							installDirectory: "Core", requiredExtensions: [".wasm", ".mjs", ".d.ts"]),
						Component("examples", "Examples", "Browser and worker integration examples.", "examples", defaultSelected: false),
						Component("docs", "Documentation", "Offline WebAssembly SDK documentation.", "docs", defaultSelected: false),
					]),
			});

	public static IReadOnlyCollection<InstallerProductDefinition> All => Definitions.Values.ToArray();

	public static InstallerProductDefinition Get(string profileId) =>
		Definitions.TryGetValue(profileId, out var definition)
			? definition
			: throw new ArgumentException($"Unknown installer product profile '{profileId}'.", nameof(profileId));

	public static bool TryGet(string profileId, out InstallerProductDefinition definition) =>
		Definitions.TryGetValue(profileId, out definition!);

	public static InstallerProductDefinition Infer(string? productId, IEnumerable<string> componentIds)
	{
		var ids = componentIds.ToHashSet(StringComparer.Ordinal);
		if (!string.IsNullOrWhiteSpace(productId))
		{
			var exact = Definitions.Values.SingleOrDefault(value => value.ProductId == productId);
			if (exact is not null && ids.All(exact.Components.ContainsKey)) return exact;
		}
		if (ids.Contains("runtime")) return Get(RuntimeProfileId);
		if (ids.Contains("sdk")) return Get(CoreSdkProfileId);
		if (ids.Contains("interop-sdk")) return Get(InteropProfileId);
		if (ids.Contains("web-sdk")) return Get(WebAssemblyProfileId);
		throw new ArgumentException("A product profile is required for this component set.");
	}

	private static InstallerProductDefinition LanguageProduct(
		string profile, string id, string displayLanguage, string name, string description)
	{
		IReadOnlyList<string>? runtimeFiles = id switch
		{
			"cpp" =>
			[
				"include/Cpp/VeroScriptCpp.h",
				"include/Cpp/VeroScriptSession.h",
				PlatformLibrary("lib/VeroScriptCpp"),
				"lib/cmake/VeroScriptCpp/VeroScriptCppConfig.cmake",
				"lib/cmake/VeroScriptCpp/VeroScriptCppConfigVersion.cmake",
			],
			"csharp" when OperatingSystem.IsWindows() =>
			[
				"lib/dotnet/net8.0/VeroScriptCSharp.dll",
				"lib/dotnet/net8.0/VeroScriptInterop.dll",
				"lib/dotnet/net9.0/VeroScriptCSharp.dll",
				"lib/dotnet/net9.0/VeroScriptInterop.dll",
			],
			"csharp" =>
			[
				"lib/dotnet/net8.0/VeroScriptCSharp.dll",
				"lib/dotnet/net8.0/" + PlatformSharedLibrary(),
				"lib/dotnet/net9.0/VeroScriptCSharp.dll",
				"lib/dotnet/net9.0/" + PlatformSharedLibrary(),
			],
			"python" when OperatingSystem.IsWindows() =>
			[
				"Lib/site-packages/veroscript/__init__.py",
				"Lib/site-packages/veroscript/_veroscript.pyi",
				"Lib/site-packages/veroscript/py.typed",
				"Lib/site-packages/veroscript/VeroScriptInterop.dll",
			],
			_ => null,
		};
		IReadOnlyList<string>? runtimeExtensions = id switch
		{
			"csharp" => [".dll"],
			"python" => [OperatingSystem.IsWindows() ? ".pyd" : ".so"],
			_ => null,
		};
		IReadOnlyList<string>? aotFiles = id switch
		{
			"cpp" => ["include/CppAot/VeroScriptCppAot.h", PlatformLibrary("lib/VeroScriptCppAot")],
			"csharp" => [PlatformLibrary("lib/VeroScriptCSharpAot")],
			"python" => ["include/PythonAot/VeroScriptPythonAot.h", PlatformLibrary("lib/VeroScriptPythonAot")],
			_ => null,
		};
		IReadOnlyList<string>? toolsFiles = id switch
		{
			"cpp" =>
			[
				"include/CppFrontend/VeroScriptCppFrontend.h",
				"include/CppBindGen/VeroScriptCppBindGen.h",
				"include/CppTranspiler/VeroScriptCppTranspiler.h",
				PlatformLibrary("lib/VeroScriptCppFrontend"),
				PlatformLibrary("lib/VeroScriptCppBindGenLib"),
				PlatformLibrary("lib/VeroScriptCppTranspilerLib"),
				PlatformExecutable("bin/VeroScriptCppBindGen"),
				PlatformExecutable("bin/VeroScriptCppTranspiler"),
			],
			"csharp" when OperatingSystem.IsWindows() =>
				["bin/dotnet-tools/veroscript-csharp-bindgen/VeroScriptInterop.dll"],
			_ => null,
		};
		return Product(profile, $"dev.veroscript.{id}", name, description,
			InstallerRootKinds.Developer,
		[
			Component($"{id}-runtime", $"{displayLanguage} Runtime", $"{displayLanguage} runtime integration.", "Runtime", required: true,
				requiredFiles: runtimeFiles, requiredExtensions: runtimeExtensions),
			Component($"{id}-tools", $"{displayLanguage} Tools", $"{displayLanguage} frontend, binding and generation tools.", "Tools", defaultSelected: true,
				requiredFiles: toolsFiles),
			Component($"{id}-aot", $"{displayLanguage} AOT", $"{displayLanguage} ahead-of-time backend.", "AOT", defaultSelected: false,
				requiredFiles: aotFiles),
			Component("examples", "Examples", $"{displayLanguage} integration examples.", "examples", defaultSelected: false),
			Component("docs", "Documentation", $"Offline {displayLanguage} integration documentation.", "docs", defaultSelected: false),
			Component("symbols", "Debug Symbols", $"Matching {displayLanguage} native debug symbols.", "Symbols", defaultSelected: false,
				requiredExtensions: SymbolExtensions()),
		]) with { DescriptionResourceKey = id + ".Description" };
	}

	private static string PlatformLibrary(string stem) => OperatingSystem.IsWindows()
		? stem + ".lib"
		: "lib/" + "lib" + Path.GetFileName(stem) + ".a";

	private static string PlatformExecutable(string stem) => OperatingSystem.IsWindows()
		? stem + ".exe"
		: stem;

	private static string PlatformSharedLibrary() => OperatingSystem.IsMacOS()
		? "libVeroScriptInterop.dylib" : "libVeroScriptInterop.so";

	private static string RuntimeVmLibrary() => "Vm/" + (OperatingSystem.IsWindows()
		? "VeroScriptVmBytecode.dll" : OperatingSystem.IsMacOS()
			? "libVeroScriptVmBytecode.dylib" : "libVeroScriptVmBytecode.so");

	private static string[]? SymbolExtensions() => OperatingSystem.IsWindows()
		? [".pdb"] : OperatingSystem.IsLinux() ? [".debug"] : null;

	private static InstallerProductDefinition Product(
		string profile, string id, string name, string description, string rootKind,
		IEnumerable<InstallerComponentDefinition> components, string? pathComponent = null,
		string pathDirectory = ".") =>
		new(profile, id, name, description, rootKind, "VeroScript", false,
			new ReadOnlyDictionary<string, InstallerComponentDefinition>(components.Select(value => value with
			{
				InstallDirectory = InstallationDirectory(profile, value),
			}).ToDictionary(value => value.Id, StringComparer.Ordinal)),
			pathComponent, pathDirectory,
			DescriptionResourceKey: rootKind == InstallerRootKinds.Developer ? "SdkSubtitle" : "Subtitle");

	private static string InstallationDirectory(string profile, InstallerComponentDefinition component)
	{
		var product = profile switch
		{
			RuntimeProfileId => "Runtime",
			CoreSdkProfileId => "Core",
			InteropProfileId => "Interop",
			CppProfileId => "Cpp",
			CSharpProfileId => "CSharp",
			PythonProfileId => "Python",
			WebAssemblyProfileId => "WebAssembly",
			AsmJitProfileId => "AsmJit",
			_ => throw new ArgumentException("Unknown installer profile.", nameof(profile)),
		};
		if (component.Id is "examples" or "docs" or "symbols")
		{
			var category = component.Id switch
			{
				"examples" => "Examples",
				"docs" => "Docs",
				_ => "Symbols",
			};
			return $"{category}/{product}/{{version}}";
		}
		if (profile == RuntimeProfileId) return "Runtime/{version}";
		if (profile == AsmJitProfileId) return "Jit/asmjit/{version}";
		var prefix = $"SDK/{product}/{{version}}";
		return profile is CppProfileId or CSharpProfileId or PythonProfileId
			? prefix + "/" + component.InstallDirectory
			: prefix;
	}

	private static InstallerComponentDefinition Component(
		string id, string name, string description, string installDirectory = ".", bool required = false,
		bool defaultSelected = true, IReadOnlyList<string>? requiredFiles = null,
		IReadOnlyList<string>? requiredExtensions = null,
		IReadOnlyList<string>? requiredExecutables = null) =>
		new(id, name, description, installDirectory, required, defaultSelected,
			RequiredFiles: requiredFiles, RequiredExtensions: requiredExtensions,
			RequiredExecutables: requiredExecutables,
			DisplayNameResourceKey: ComponentResourceKey(id, false),
			DescriptionResourceKey: ComponentResourceKey(id, true));

	private static string? ComponentResourceKey(string id, bool description) => id switch
	{
		"jit-backend" => description ? "jit-backend.Description" : "jit-backend.Name",
		"runtime" => description ? "RuntimeDescription" : "Runtime",
		"sdk" => description ? "SdkCoreDescription" : "SdkCore",
		"interop-sdk" => description ? "InteropSdkDescription" : "InteropSdk",
		"web-sdk" => description ? "WebAssemblySdkDescription" : "WebAssemblySdk",
		"examples" => description ? "ExamplesDescription" : "Examples",
		"docs" => description ? "DocsDescription" : "Docs",
		"symbols" => description ? "SymbolsDescription" : "Symbols",
		"cpp-runtime" or "cpp-tools" or "cpp-aot"
			or "csharp-runtime" or "csharp-tools" or "csharp-aot"
			or "python-runtime" or "python-tools" or "python-aot"
			=> id + (description ? ".Description" : ".Name"),
		_ => null,
	};

	private static string[] RuntimeFiles() =>
	[
		PlatformInfo.CurrentPlatform == "windows" ? "veroscript.exe" : "veroscript",
		PlatformInfo.CurrentPlatform == "windows" ? "veroscript-lsp.exe" : "veroscript-lsp",
		PlatformInfo.CurrentPlatform == "windows" ? "veroscript-dap.exe" : "veroscript-dap",
		RuntimeVmLibrary(),
		.. StandardLibraryFiles("stdlib/"),
		"localization/en-US/shared.json",
	];

	private static string[] RuntimeExecutables() =>
	[
		OperatingSystem.IsWindows() ? "veroscript.exe" : "veroscript",
		OperatingSystem.IsWindows() ? "veroscript-lsp.exe" : "veroscript-lsp",
		OperatingSystem.IsWindows() ? "veroscript-dap.exe" : "veroscript-dap",
	];

	private static string[] StandardLibraryFiles(string prefix) =>
	[
		prefix + "algorithm.vs", prefix + "concurrent.vs", prefix + "core.vs",
		prefix + "delegate.vs", prefix + "io.vs", prefix + "math.vs",
		prefix + "numeric.vs", prefix + "path.vs", prefix + "random.vs", prefix + "string.vs",
	];
}
