// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;

namespace VeroScriptInstaller.Core;

public static class PackageManifestBuilder
{
	public static async Task<InstallerManifest> BuildAsync(
		string payloadRoot,
		string productVersion,
		IReadOnlyDictionary<string, string> components,
		string? platform = null,
		string? architecture = null,
		string? productId = null,
		string? productName = null,
		string? profileId = null,
		InstallerProductDefinition? productDefinition = null,
		CancellationToken cancellationToken = default)
	{
		ArgumentNullException.ThrowIfNull(components);
		if (!Version.TryParse(productVersion, out _))
		{
			throw new ArgumentException("Product version must be a numeric dotted version.", nameof(productVersion));
		}
		if (components.Count == 0)
		{
			throw new ArgumentException("At least one component is required.", nameof(components));
		}

		if (profileId is not null && productDefinition is not null)
		{
			throw new ArgumentException("Choose either a built-in profile or a product definition, not both.");
		}
		var definition = productDefinition ?? (profileId is not null
			? InstallerProductCatalog.Get(profileId)
			: InstallerProductCatalog.Infer(productId, components.Keys));
		if (string.Equals(definition.ReleaseGroup, "Modules", StringComparison.OrdinalIgnoreCase)
			|| definition.ProfileId == InstallerProductCatalog.AsmJitProfileId
			|| definition.ProductId == "dev.veroscript.jit.asmjit")
		{
			throw new InvalidOperationException("Modules are distributed as functional packages and do not provide standalone installers.");
		}
		ValidateProductDefinition(definition);
		if (productId is not null && productId != definition.ProductId)
		{
			throw new ArgumentException("The product id does not match the selected installer profile.", nameof(productId));
		}
		var unknownComponent = components.Keys.FirstOrDefault(id => !definition.Components.ContainsKey(id));
		if (unknownComponent is not null)
		{
			throw new ArgumentException(
				$"Component '{unknownComponent}' is not part of profile '{definition.ProfileId}'.", nameof(components));
		}
		var missingRequired = definition.Components.Values.FirstOrDefault(component =>
			component.Required && !components.ContainsKey(component.Id));
		if (missingRequired is not null)
		{
			throw new ArgumentException(
				$"Required component '{missingRequired.Id}' is missing from profile '{definition.ProfileId}'.", nameof(components));
		}

		var manifest = new InstallerManifest
		{
			ProfileId = definition.ProfileId,
			ProductId = definition.ProductId,
			ProductName = productName ?? definition.ProductName,
			Description = definition.Description,
			LocalizedNames = definition.LocalizedNames?.ToDictionary(pair => pair.Key, pair => pair.Value,
				StringComparer.OrdinalIgnoreCase) ?? new(StringComparer.OrdinalIgnoreCase),
			LocalizedDescriptions = definition.LocalizedDescriptions?.ToDictionary(pair => pair.Key, pair => pair.Value,
				StringComparer.OrdinalIgnoreCase) ?? new(StringComparer.OrdinalIgnoreCase),
			DescriptionResourceKey = definition.DescriptionResourceKey,
			ProductVersion = productVersion,
			Platform = platform ?? PlatformInfo.CurrentPlatform,
			Architecture = architecture ?? PlatformInfo.CurrentArchitecture,
			InstallRoot = new InstallerRootDefinition
			{
				Kind = definition.RootKind,
				DirectoryName = definition.RootDirectoryName,
				IncludeVersion = definition.IncludeVersion,
			},
			PathIntegration = definition.PathComponentId is null ? null : new InstallerPathDefinition
			{
				ComponentId = definition.PathComponentId,
				RelativeDirectory = definition.PathRelativeDirectory,
				DefaultEnabled = definition.PathDefaultEnabled,
			},
		};
		var fullPayloadRoot = Path.GetFullPath(payloadRoot);
		foreach (var pair in components.OrderBy(pair => pair.Key, StringComparer.Ordinal))
		{
			var payloadDirectory = PathSafety.NormalizeRelativePath(pair.Value);
			var componentRoot = PathSafety.ResolveUnderRoot(fullPayloadRoot, payloadDirectory);
			PathSafety.EnsureDirectoryIsNotLink(componentRoot);
			var componentDefinition = definition.Components[pair.Key];
			var component = new InstallerComponent
			{
				Id = pair.Key,
				DisplayName = componentDefinition.DisplayName,
				Description = componentDefinition.Description,
				LocalizedNames = componentDefinition.LocalizedNames?.ToDictionary(pair => pair.Key, pair => pair.Value,
					StringComparer.OrdinalIgnoreCase) ?? new(StringComparer.OrdinalIgnoreCase),
				LocalizedDescriptions = componentDefinition.LocalizedDescriptions?.ToDictionary(pair => pair.Key, pair => pair.Value,
					StringComparer.OrdinalIgnoreCase) ?? new(StringComparer.OrdinalIgnoreCase),
				DisplayNameResourceKey = componentDefinition.DisplayNameResourceKey,
				DescriptionResourceKey = componentDefinition.DescriptionResourceKey,
				InstallDirectory = componentDefinition.InstallDirectory.Replace("{version}", productVersion, StringComparison.Ordinal),
				Required = componentDefinition.Required,
				DefaultSelected = componentDefinition.DefaultSelected,
				Dependencies = componentDefinition.Dependencies?.ToList() ?? [],
				Validation = new InstallerComponentValidation
				{
					RequiredFiles = componentDefinition.RequiredFiles?.ToList() ?? [],
					RequiredExtensions = componentDefinition.RequiredExtensions?.ToList() ?? [],
					RequiredFilePrefixes = componentDefinition.RequiredFilePrefixes?.ToList() ?? [],
					RequiredExecutables = componentDefinition.RequiredExecutables?.ToList() ?? [],
				},
				PayloadDirectory = payloadDirectory,
			};
			foreach (var path in PathSafety.EnumerateFilesWithoutLinks(componentRoot)
				.OrderBy(path => path, PathSafety.RelativePathComparer))
			{
				cancellationToken.ThrowIfCancellationRequested();
				PathSafety.EnsurePathHasNoLinks(componentRoot, path);
				var info = new FileInfo(path);
				if (info.Length > InstallerConstants.MaximumFileSize)
				{
					throw new InvalidDataException($"Payload file is too large: {path}");
				}
				await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024,
					FileOptions.Asynchronous | FileOptions.SequentialScan);
				component.Files.Add(new InstallerFile
				{
					Path = Path.GetRelativePath(componentRoot, path).Replace('\\', '/'),
					Size = info.Length,
					Sha256 = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken)),
					Executable = IsExecutable(info),
				});
				if (component.Files.Count > InstallerConstants.MaximumFileCount)
				{
					throw new InvalidDataException("The component contains too many files.");
				}
			}
			manifest.Components.Add(component);
		}
		ManifestValidator.Validate(manifest, requireCurrentPlatform: false);
		return manifest;
	}

	public static Task WriteAsync(string path, InstallerManifest manifest, CancellationToken cancellationToken = default)
	{
		ManifestValidator.Validate(manifest, requireCurrentPlatform: false);
		return InstallerJson.WriteAsync(path, manifest, cancellationToken);
	}

	private static bool IsExecutable(FileInfo info)
	{
		if (OperatingSystem.IsWindows())
		{
			return info.Extension.Equals(".exe", StringComparison.OrdinalIgnoreCase);
		}
		var mode = File.GetUnixFileMode(info.FullName);
		return (mode & (UnixFileMode.UserExecute | UnixFileMode.GroupExecute | UnixFileMode.OtherExecute)) != 0;
	}

	private static void ValidateProductDefinition(InstallerProductDefinition definition)
	{
		ArgumentNullException.ThrowIfNull(definition);
		if (string.IsNullOrWhiteSpace(definition.ProfileId)
			|| string.IsNullOrWhiteSpace(definition.ProductId)
			|| string.IsNullOrWhiteSpace(definition.ProductName)
			|| definition.Description is null
			|| string.IsNullOrWhiteSpace(definition.RootDirectoryName)
			|| definition.Components is null || definition.Components.Count == 0)
		{
			throw new ArgumentException("The installer product definition is incomplete.", nameof(definition));
		}
		if (definition.PathComponentId is not null && !definition.Components.ContainsKey(definition.PathComponentId))
		{
			throw new ArgumentException("PATH integration refers to an unknown product component.", nameof(definition));
		}
		var installDirectories = new List<string>();
		foreach (var pair in definition.Components)
		{
			var component = pair.Value ?? throw new ArgumentException(
				$"Product component '{pair.Key}' is null.", nameof(definition));
			if (!string.Equals(pair.Key, component.Id, StringComparison.Ordinal))
			{
				throw new ArgumentException(
					$"Product component key '{pair.Key}' does not match id '{component.Id}'.", nameof(definition));
			}
			var installDirectory = component.InstallDirectory == "."
				? "."
				: PathSafety.NormalizeRelativePath(component.InstallDirectory);
			if (installDirectories.Any(existing => existing == "." || installDirectory == "."
				|| existing.Equals(installDirectory, StringComparison.OrdinalIgnoreCase)
				|| existing.StartsWith(installDirectory + '/', StringComparison.OrdinalIgnoreCase)
				|| installDirectory.StartsWith(existing + '/', StringComparison.OrdinalIgnoreCase)))
			{
				throw new ArgumentException(
					$"Product component '{component.Id}' has an overlapping installation directory.", nameof(definition));
			}
			installDirectories.Add(installDirectory);
			foreach (var dependency in component.Dependencies ?? [])
			{
				if (dependency == component.Id || !definition.Components.ContainsKey(dependency))
				{
					throw new ArgumentException(
						$"Product component '{component.Id}' has invalid dependency '{dependency}'.", nameof(definition));
				}
			}
		}
	}
}
