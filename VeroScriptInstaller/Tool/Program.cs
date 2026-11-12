// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScriptInstaller.Core;
using VeroScriptInstaller.Core.Platform;
using System.IO.Compression;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace VeroScriptInstaller.Tool;

internal static class Program
{
	public static async Task<int> Main(string[] args)
	{
		try
		{
			if (args.SequenceEqual(["--license"], StringComparer.Ordinal))
			{
				Console.Write(ProjectLicense.Read());
				return 0;
			}
			if (args.SequenceEqual(["--third-party-licenses"], StringComparer.Ordinal))
			{
				Console.OutputEncoding = new System.Text.UTF8Encoding(false);
				Console.Write(ProjectLicense.ReadThirdPartyNotices());
				return 0;
			}
			if (args.Length == 0 || args[0] is "-h" or "--help" or "help")
			{
				PrintHelp();
				return args.Length == 0 ? 2 : 0;
			}
			var options = Parse(args.Skip(1).ToArray());
			return args[0] switch
			{
				"build-bundle" => await BuildBundleAsync(options),
				"pack-bundle" => await PackBundleAsync(options),
				"verify" => await VerifyAsync(options),
				"install" => await InstallAsync(options),
				"repair" => await RepairAsync(options),
				"uninstall" => await UninstallAsync(options),
				_ => throw new ArgumentException($"Unknown command '{args[0]}'."),
			};
		}
		catch (Exception exception)
		{
			var detail = Environment.GetEnvironmentVariable("VEROSCRIPT_INSTALLER_TRACE") == "1"
				? exception.ToString()
				: exception.Message;
			Console.Error.WriteLine($"veroscript-installer-tool: {detail}");
			return 1;
		}
	}

	private static async Task<int> BuildBundleAsync(IReadOnlyDictionary<string, string?> options)
	{
		EnsureAllowedWithPrefixes(options, ["source-"], "output", "version", "profile", "definition",
			"product-id", "product-name", "platform", "architecture");
		var components = new Dictionary<string, string>(StringComparer.Ordinal);
		foreach (var option in options.Where(pair => pair.Key.StartsWith("source-", StringComparison.Ordinal)))
		{
			var componentId = option.Key["source-".Length..];
			if (string.IsNullOrWhiteSpace(option.Value) || !components.TryAdd(componentId, option.Value))
			{
				throw new ArgumentException($"Invalid or duplicate component source '--{option.Key}'.");
			}
		}
		InstallerProductDefinition? productDefinition = null;
		if (Get(options, "definition") is { } definitionPath)
		{
			if (Get(options, "profile") is not null)
			{
				throw new ArgumentException("Choose either '--profile' or '--definition', not both.");
			}

			await using var definitionStream = File.OpenRead(Path.GetFullPath(definitionPath));
			productDefinition = await JsonSerializer.DeserializeAsync<InstallerProductDefinition>(
				definitionStream,
				new JsonSerializerOptions(JsonSerializerDefaults.Web)
				{
					UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
				}) ?? throw new InvalidDataException("The product definition is empty.");
		}
		var manifest = await InstallerBundleBuilder.BuildAsync(
			Require(options, "output"),
			Require(options, "version"),
			components,
			Get(options, "platform"),
			Get(options, "architecture"),
			Get(options, "product-id"),
			Get(options, "product-name"),
			Get(options, "profile"),
			productDefinition,
			functionalPayloadOnly: true);
		Console.WriteLine(manifest);
		return 0;
	}

	private static async Task<int> VerifyAsync(IReadOnlyDictionary<string, string?> options)
	{
		EnsureAllowed(options, "bundle");
		var bundle = Path.GetFullPath(Require(options, "bundle"));
		var manifest = await ManifestValidator.LoadAsync(Path.Combine(bundle, InstallerConstants.ManifestFileName), requireCurrentPlatform: false);
		await ManifestValidator.VerifyPayloadAsync(manifest, bundle, requireCurrentPlatform: false);
		Console.WriteLine($"Verified {manifest.ProductName} installer bundle {manifest.ProductVersion} ({manifest.Platform}-{manifest.Architecture}).");
		return 0;
	}

	private static async Task<int> PackBundleAsync(IReadOnlyDictionary<string, string?> options)
	{
		EnsureAllowed(options, "bundle", "output");
		var bundle = Path.GetFullPath(Require(options, "bundle"));
		var output = Path.GetFullPath(Require(options, "output"));
		if (Directory.Exists(output))
		{
			throw new IOException($"Archive output is an existing directory: {output}");
		}
		var outputRelativeToBundle = Path.GetRelativePath(bundle, output);
		if (outputRelativeToBundle != ".."
			&& !outputRelativeToBundle.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
			&& !Path.IsPathRooted(outputRelativeToBundle))
		{
			throw new InvalidOperationException("Archive output must not be inside the source bundle.");
		}
		var manifest = await ManifestValidator.LoadAsync(Path.Combine(bundle, InstallerConstants.ManifestFileName), requireCurrentPlatform: false);
		await ManifestValidator.VerifyPayloadAsync(manifest, bundle, requireCurrentPlatform: false);
		var outputDirectory = Path.GetDirectoryName(output)!;
		Directory.CreateDirectory(outputDirectory);
		var stage = Path.Combine(outputDirectory, $".{Path.GetFileName(output)}.new-{Guid.NewGuid():N}");
		try
		{
			ZipFile.CreateFromDirectory(bundle, stage, CompressionLevel.Optimal, includeBaseDirectory: false);
			File.Move(stage, output, overwrite: true);
		}
		finally
		{
			File.Delete(stage);
		}
		Console.WriteLine(output);
		return 0;
	}

	private static async Task<int> InstallAsync(IReadOnlyDictionary<string, string?> options)
	{
		EnsureAllowed(options, "bundle", "root", "components", "existing-record",
			"add-to-path", "allow-downgrade", "associate-vs", "associate-vsapp", "package-icon", "choose-defaults");
		foreach (var flag in new[] { "add-to-path", "allow-downgrade", "associate-vs", "associate-vsapp", "package-icon", "choose-defaults" })
		{
			if (options.TryGetValue(flag, out var flagValue) && flagValue is not null)
			{
				throw new ArgumentException($"Option '--{flag}' does not accept a value.");
			}
		}
		var bundle = Path.GetFullPath(Require(options, "bundle"));
		if (options.ContainsKey("choose-defaults") && !options.ContainsKey("associate-vs") && !options.ContainsKey("associate-vsapp"))
		{
			throw new ArgumentException("--choose-defaults requires --associate-vs or --associate-vsapp.");
		}
		var selected = new HashSet<string>(StringComparer.Ordinal);
		var destinations = new Dictionary<string, string>(StringComparer.Ordinal);
		var installRoot = Require(options, "root");
		var manifest = await ManifestValidator.LoadAsync(
			Path.Combine(bundle, InstallerConstants.ManifestFileName));
		var requested = Get(options, "components")?.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries)
			.ToHashSet(StringComparer.Ordinal)
			?? manifest.Components.Where(component => component.Required || component.DefaultSelected)
				.Select(component => component.Id).ToHashSet(StringComparer.Ordinal);
		AddDependencies(manifest, requested);
		foreach (var componentId in requested)
		{
			var component = manifest.Components.SingleOrDefault(value => value.Id == componentId)
				?? throw new ArgumentException($"Unknown component '{componentId}'.");
			selected.Add(component.Id);
			destinations[component.Id] = component.InstallDirectory == "."
				? installRoot
				: Path.Combine(installRoot, component.InstallDirectory);
		}
		var result = await new InstallerEngine().InstallAsync(new InstallOptions
		{
			ManifestPath = Path.Combine(bundle, InstallerConstants.ManifestFileName),
			PayloadRoot = bundle,
			SelectedComponents = selected,
			ComponentDestinations = destinations,
			AddToUserPath = options.ContainsKey("add-to-path"),
			ExistingInstallRecordPath = Get(options, "existing-record"),
			AllowDowngrade = options.ContainsKey("allow-downgrade"),
			FileAssociations = new() { Source = options.ContainsKey("associate-vs"), Application = options.ContainsKey("associate-vsapp"),
				PackageIcon = options.ContainsKey("package-icon") },
		});
		Console.WriteLine($"Installed {manifest.ProductName} {result.ProductVersion} ({result.InstallationId}).");
		if (result.AssociationSettingsPreserved)
		{
			Console.WriteLine("Changed or unowned association settings were preserved.");
		}
		if (options.ContainsKey("choose-defaults"))
		{
			Console.WriteLine(result.DefaultApplicationName is { } app && DefaultAppsSettings.TryOpen(app)
				? "Default app settings requested. Confirm your selection in Windows; no default has been assigned by this tool."
				: "Installation succeeded. Open Settings > Apps > Default apps to choose a registered handler.");
		}
		return 0;
	}

	private static async Task<int> UninstallAsync(IReadOnlyDictionary<string, string?> options)
	{
		EnsureAllowed(options, "record");
		var recordPath = Require(options, "record");
		var engine = new InstallerEngine();
		var record = await engine.LoadRecordAsync(recordPath);
		await engine.UninstallAsync(recordPath);
		Console.WriteLine($"{record.ProductName} was uninstalled.");
		return 0;
	}

	private static async Task<int> RepairAsync(IReadOnlyDictionary<string, string?> options)
	{
		EnsureAllowed(options, "bundle", "record");
		var bundle = Path.GetFullPath(Require(options, "bundle"));
		var result = await new InstallerEngine().RepairAsync(
			Require(options, "record"),
			Path.Combine(bundle, InstallerConstants.ManifestFileName),
			bundle);
		Console.WriteLine($"Repaired product {result.ProductVersion} ({result.InstallationId}).");
		return 0;
	}

	private static Dictionary<string, string?> Parse(string[] args)
	{
		var result = new Dictionary<string, string?>(StringComparer.Ordinal);
		for (var index = 0; index < args.Length; ++index)
		{
			var argument = args[index];
			if (!argument.StartsWith("--", StringComparison.Ordinal) || argument.Length == 2)
			{
				throw new ArgumentException($"Unexpected argument '{argument}'.");
			}
			var key = argument[2..];
			string? value = null;
			if (index + 1 < args.Length && !args[index + 1].StartsWith("--", StringComparison.Ordinal))
			{
				value = args[++index];
			}
			if (!result.TryAdd(key, value))
			{
				throw new ArgumentException($"Duplicate option '--{key}'.");
			}
		}
		return result;
	}

	private static string Require(IReadOnlyDictionary<string, string?> options, string key) =>
		Get(options, key) ?? throw new ArgumentException($"Missing required option '--{key}'.");

	private static string? Get(IReadOnlyDictionary<string, string?> options, string key) =>
		options.TryGetValue(key, out var value) && !string.IsNullOrWhiteSpace(value) ? value : null;

	private static void EnsureAllowed(IReadOnlyDictionary<string, string?> options, params string[] allowed)
	{
		var allowedSet = allowed.ToHashSet(StringComparer.Ordinal);
		var unknown = options.Keys.FirstOrDefault(key => !allowedSet.Contains(key));
		if (unknown is not null)
		{
			throw new ArgumentException($"Unknown option '--{unknown}'.");
		}
	}

	private static void EnsureAllowedWithPrefixes(
		IReadOnlyDictionary<string, string?> options,
		IReadOnlyList<string> prefixes,
		params string[] allowed)
	{
		var allowedSet = allowed.ToHashSet(StringComparer.Ordinal);
		var unknown = options.Keys.FirstOrDefault(key =>
			!allowedSet.Contains(key) && !prefixes.Any(prefix => key.StartsWith(prefix, StringComparison.Ordinal)));
		if (unknown is not null)
		{
			throw new ArgumentException($"Unknown option '--{unknown}'.");
		}
	}

	private static void AddDependencies(InstallerManifest manifest, ISet<string> selected)
	{
		var changed = true;
		while (changed)
		{
			changed = false;
			foreach (var component in manifest.Components.Where(component => selected.Contains(component.Id)))
			{
				foreach (var dependency in component.Dependencies)
				{
					changed |= selected.Add(dependency);
				}
			}
		}
	}

	private static void PrintHelp()
	{
		Console.WriteLine("""
			VeroScript cross-platform installer tool
			  --license    Print the complete MIT license notice
			  --third-party-licenses    Print the complete dependency license notices

			  build-bundle --output <dir> --version <version> (--profile <profile> | --definition <json>)
			               [--source-<component-id> <dir>] [--product-name <name>]
			               [--platform windows|linux|macos] [--architecture x64|arm64|x86]
			  pack-bundle  --bundle <dir> --output <zip>
			  verify       --bundle <dir>
			  install      --bundle <dir> --root <dir> [--components <id,id,...>]
			               [--existing-record <path-to-.veroscript-install.json>]
			               [--add-to-path]
			               [--associate-vs] [--associate-vsapp] [--package-icon] [--choose-defaults]
			               [--allow-downgrade]
			  repair       --bundle <dir> --record <path-to-.veroscript-install.json>
			  uninstall    --record <path-to-.veroscript-install.json>
			""");
	}
}
