// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using VeroScript;
using VeroScript.Interop;
using VeroScriptMauiHost;

static void Require(bool condition, string message)
{
	if (!condition)
	{
		throw new InvalidOperationException(message);
	}
}

const string expectedLibrary =
#if IOS
	"__Internal";
#else
	"VeroScriptInterop";
#endif
MethodInfo[] imports = typeof(NativeMethods).GetMethods(BindingFlags.Static | BindingFlags.NonPublic)
	.Where(method => method.GetCustomAttribute<DllImportAttribute>() is not null).ToArray();
var exports = File.ReadAllLines(Path.Combine(AppContext.BaseDirectory, "VeroScriptInterop.def"))
	.Select(line => line.Trim()).Where(line => line.StartsWith("vs_interop_", StringComparison.Ordinal)).ToHashSet();
Require(imports.Length > 150, "Native import audit did not find the full managed binding");
foreach (MethodInfo method in imports)
{
	var import = method.GetCustomAttribute<DllImportAttribute>()!;
	Require(import.Value == expectedLibrary && import.CallingConvention == CallingConvention.Cdecl,
		"Wrong mobile P/Invoke destination: " + method.Name);
	Require(exports.Contains(import.EntryPoint!), "Unknown mobile Interop entry point: " + method.Name);
}
Require((typeof(NativeMethods).TypeInitializer is null) == (expectedLibrary == "__Internal"),
	"iOS static imports must bypass the dynamic native resolver");
Require((ulong)VeroScriptCompileCapabilities.AnnotationProcessing == 1
	&& (ulong)VeroScriptCompileCapabilities.ScriptExtensions == 2
	&& (ulong)VeroScriptCompileCapabilities.GeneralProcessing == 4
	&& (ulong)VeroScriptCapabilities.ModuleEnvironment == 0x400000
	&& (ulong)VeroScriptCapabilities.VmBackendSelection == 0x2000000
	&& (ulong)VeroScriptCapabilities.Numerics == 0x800000
	&& (ulong)VeroScriptJitBackendCapabilities.ControlledExecution == 0x10
	&& (uint)VeroScriptDiagnosticSeverity.Hint == 3,
	"mobile capability flags and diagnostic severity match the native contract");
Require(Unsafe.SizeOf<NativeVmBackendInfo>() == 80,
	"mobile VM backend metadata matches the native layout");
Require((uint)VeroScriptConsoleOperation.SetCursorVisible == 5
	&& Unsafe.SizeOf<NativeConsoleInfo>() == 16
	&& Unsafe.SizeOf<NativeSessionOptions>() == (Environment.Is64BitProcess ? 56 : 28),
	"mobile console callbacks match the native operation and structure contract");
unsafe
{
	Require(Unsafe.SizeOf<NativeAnnotationDescriptor>() == 80
		&& Marshal.OffsetOf<NativeAnnotationDescriptor>(nameof(NativeAnnotationDescriptor.NameFlags)).ToInt64() == 72,
		"mobile annotation descriptors match the native name-policy layout");
	using var lease = new NativeBindingLease();
	Require(lease.MakeAnnotationDefinition(new("@MobileProbe")).NameFlags == 0
		&& lease.MakeAnnotationDefinition(new("@MobileProbe") { CaseSensitive = false }).NameFlags == 1,
		"mobile annotation name policies marshal exact and ASCII case-folded flags");
}

string temporaryRoot = Path.Combine(Path.GetTempPath(), "veroscript-mobile-contracts-" + Guid.NewGuid().ToString("N"));
Directory.CreateDirectory(temporaryRoot);
try
{
	var files = new Dictionary<string, byte[]>
	{
		["stdlib/core.vs"] = Encoding.UTF8.GetBytes("// test resource\n"),
		["localization/en-US/runtime.json"] = Encoding.UTF8.GetBytes("{}")
	};
	var assets = files.ToDictionary(entry => "veroscript/" + entry.Key, entry => entry.Value);
	byte[] Manifest(Dictionary<string, string> hashes) => JsonSerializer.SerializeToUtf8Bytes(
		new { schemaVersion = 1, files = hashes });
	var hashes = files.ToDictionary(entry => entry.Key, entry => Convert.ToHexString(SHA256.HashData(entry.Value)));
	assets["veroscript/resources.json"] = Manifest(hashes);
	var openedStreams = new List<Stream>();
	Task<Stream> Open(string name, CancellationToken cancellation)
	{
		cancellation.ThrowIfCancellationRequested();
		Stream stream = new MemoryStream(assets[name], writable: false);
		openedStreams.Add(stream);
		return Task.FromResult(stream);
	}
	string destination = Path.Combine(temporaryRoot, "resources");
	Require(await VeroScriptMobileResources.InstallAsync(destination, Open) == destination, "Wrong resource root");
	Require(openedStreams.All(stream => !stream.CanRead), "Installed asset stream ownership was not released");
	foreach (var entry in files)
	{
		Require(File.ReadAllBytes(Path.Combine(destination, entry.Key)).SequenceEqual(entry.Value), "Resource hierarchy changed");
	}
	string core = Path.Combine(destination, "stdlib/core.vs");
	File.WriteAllText(core, "stale");
	await VeroScriptMobileResources.InstallAsync(destination, Open);
	Require(File.ReadAllBytes(core).SequenceEqual(files["stdlib/core.vs"]), "Resource installation did not repair stale data");
	assets["veroscript/stdlib/core.vs"] = Encoding.UTF8.GetBytes("corrupted");
	try
	{
		await VeroScriptMobileResources.InstallAsync(destination, Open);
		throw new InvalidOperationException("Resource corruption was accepted");
	}
	catch (InvalidDataException)
	{
		Require(File.ReadAllBytes(core).SequenceEqual(files["stdlib/core.vs"]), "Bad resource replaced the valid file");
		Require(!Directory.EnumerateFiles(destination, "*.tmp", SearchOption.AllDirectories).Any(), "Temporary resource file leaked");
		Require(openedStreams.All(stream => !stream.CanRead), "Corrupted asset stream ownership was not released");
	}
	foreach (string path in new[] { "stdlib/../../escape.vs", "stdlib\\escape.vs", "stdlib/C:/escape.vs",
		"stdlib//escape.vs", "stdlib/.. /escape.vs", "stdlib/folder./escape.vs" })
	{
		assets["veroscript/resources.json"] = Manifest(new() { [path] = hashes["stdlib/core.vs"] });
		try
		{
			await VeroScriptMobileResources.InstallAsync(Path.Combine(temporaryRoot, "invalid"), Open);
			throw new InvalidOperationException("Resource path traversal was accepted");
		}
		catch (InvalidDataException)
		{
			Require(!Directory.Exists(Path.Combine(temporaryRoot, "invalid")), "Invalid manifest created output files");
		}
	}
	assets["veroscript/stdlib/CORE.VS"] = files["stdlib/core.vs"];
	assets["veroscript/resources.json"] = Manifest(new() { ["stdlib/CORE.VS"] = hashes["stdlib/core.vs"] });
	string wrongCase = Path.Combine(temporaryRoot, "wrong-case");
	try
	{
		await VeroScriptMobileResources.InstallAsync(wrongCase, Open);
		throw new InvalidOperationException("Wrong-case core.vs was accepted");
	}
	catch (InvalidDataException)
	{
		Require(!Directory.Exists(wrongCase), "Wrong-case core.vs created output files");
	}
	assets["veroscript/resources.json"] = Manifest(hashes);
	using var cancellation = new CancellationTokenSource();
	cancellation.Cancel();
	try
	{
		await VeroScriptMobileResources.InstallAsync(destination, Open, cancellation.Token);
		throw new InvalidOperationException("Resource cancellation was ignored");
	}
	catch (OperationCanceledException)
	{
	}
	using var copyingCancellation = new CancellationTokenSource();
	Task<Stream> OpenCancelDuringCopy(string name, CancellationToken cancellation)
	{
		Task<Stream> stream = Open(name, cancellation);
		if (name != "veroscript/resources.json")
		{
			// Cancellation reaches CopyToAsync after the temporary output is opened.
			copyingCancellation.Cancel();
		}
		return stream;
	}
	try
	{
		await VeroScriptMobileResources.InstallAsync(destination, OpenCancelDuringCopy, copyingCancellation.Token);
		throw new InvalidOperationException("Resource copy cancellation was ignored");
	}
	catch (OperationCanceledException)
	{
		Require(File.ReadAllBytes(core).SequenceEqual(files["stdlib/core.vs"]), "Canceled copy replaced the valid resource");
		Require(!Directory.EnumerateFiles(destination, "*.tmp", SearchOption.AllDirectories).Any(), "Canceled resource copy leaked temporary files");
		Require(openedStreams.All(stream => !stream.CanRead), "Canceled asset stream ownership was not released");
	}
#if !IOS
	if (args.Length != 0)
	{
		string repo = Path.GetFullPath(args[0]);
		string smoke = Path.Combine(temporaryRoot, "smoke");
		var smokeResources = new Dictionary<string, string>(StringComparer.Ordinal);
		void AddDirectory(string source, string route, string pattern)
		{
			foreach (string path in Directory.GetFiles(source, pattern, SearchOption.AllDirectories))
			{
				smokeResources.Add(route + "/" + Path.GetRelativePath(source, path).Replace('\\', '/'), path);
			}
		}
		AddDirectory(Path.Combine(repo, "VeroScript/Resources/Scripts/stdlib"), "stdlib", "*.vs");
		foreach (string module in new[] { "VeroScript", "Extensions/Language/Cpp/VeroScriptCpp", "VeroScriptInterop", "Extensions/Platform/Mobile/VeroScriptMobile" })
		{
			AddDirectory(Path.Combine(repo, module, "Resources/Localization"), "localization", "*.json");
		}
		byte[] smokeManifest = Manifest(smokeResources.ToDictionary(entry => entry.Key,
			entry => Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(entry.Value)))));
		Task<Stream> OpenSmoke(string name, CancellationToken cancellation)
		{
			cancellation.ThrowIfCancellationRequested();
			return Task.FromResult<Stream>(name == "veroscript/resources.json"
				? new MemoryStream(smokeManifest, writable: false)
				: File.OpenRead(smokeResources[name["veroscript/".Length..]]));
		}
		// Exercise actual shipped resources through the same installation path as MAUI.
		await VeroScriptMobileResources.InstallAsync(smoke, OpenSmoke);
		Console.WriteLine(MobileSmoke.Run(smoke, temporaryRoot));
	}
#endif
}
finally
{
	Directory.Delete(temporaryRoot, recursive: true);
}
Console.WriteLine($"Mobile portable contracts passed: {expectedLibrary}, {imports.Length} imports, resources ({RuntimeInformation.FrameworkDescription}).");
