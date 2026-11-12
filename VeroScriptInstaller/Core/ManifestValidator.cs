// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace VeroScriptInstaller.Core;

public static partial class ManifestValidator
{
	public static void Validate(InstallerManifest manifest, bool requireCurrentPlatform = true)
	{
		ArgumentNullException.ThrowIfNull(manifest);
		if (manifest.SchemaVersion != InstallerConstants.ManifestSchemaVersion)
		{
			throw new InvalidDataException($"Unsupported installer manifest schema {manifest.SchemaVersion}.");
		}
		if (string.IsNullOrWhiteSpace(manifest.ProfileId) || !ProfileIdRegex().IsMatch(manifest.ProfileId))
		{
			throw new InvalidDataException($"Invalid installer profile id '{manifest.ProfileId}'.");
		}
		if (string.IsNullOrWhiteSpace(manifest.ProductId) || !ProductIdRegex().IsMatch(manifest.ProductId))
		{
			throw new InvalidDataException($"Invalid installer product id '{manifest.ProductId}'.");
		}
		if (string.IsNullOrWhiteSpace(manifest.ProductName) || manifest.ProductName.Length > 128)
		{
			throw new InvalidDataException("The product name is missing or too long.");
		}
		if (manifest.Description is null || manifest.Description.Length > 1024)
		{
			throw new InvalidDataException("The product description is missing or too long.");
		}
		if (!Version.TryParse(manifest.ProductVersion, out _))
		{
			throw new InvalidDataException($"Invalid product version '{manifest.ProductVersion}'.");
		}
		if (manifest.Platform is not ("windows" or "linux" or "macos"))
		{
			throw new InvalidDataException($"Invalid target platform '{manifest.Platform}'.");
		}
		if (manifest.Architecture is not ("x64" or "x86" or "arm64" or "arm"))
		{
			throw new InvalidDataException($"Invalid target architecture '{manifest.Architecture}'.");
		}
		if (requireCurrentPlatform && !manifest.Platform.Equals(PlatformInfo.CurrentPlatform, StringComparison.Ordinal))
		{
			throw new PlatformNotSupportedException(
				$"This package targets {manifest.Platform}, but the current platform is {PlatformInfo.CurrentPlatform}.");
		}
		if (requireCurrentPlatform && !manifest.Architecture.Equals(PlatformInfo.CurrentArchitecture, StringComparison.Ordinal))
		{
			throw new PlatformNotSupportedException(
				$"This package targets {manifest.Architecture}, but the current process is {PlatformInfo.CurrentArchitecture}.");
		}
		if (manifest.Components is null || manifest.Components.Count == 0)
		{
			throw new InvalidDataException("The installer manifest has no components.");
		}
		ValidateLocalizedText(manifest.LocalizedNames, 128, "product name");
		ValidateLocalizedText(manifest.LocalizedDescriptions, 1024, "product description");
		if (!IsValidResourceKey(manifest.DescriptionResourceKey))
		{
			throw new InvalidDataException("The product has an invalid localization resource key.");
		}
		if (manifest.InstallRoot is null
			|| manifest.InstallRoot.Kind is not (InstallerRootKinds.Programs or InstallerRootKinds.Developer)
			|| string.IsNullOrWhiteSpace(manifest.InstallRoot.DirectoryName)
			|| manifest.InstallRoot.DirectoryName.Length > 96
			|| manifest.InstallRoot.DirectoryName.IndexOfAny(Path.GetInvalidFileNameChars()) >= 0)
		{
			throw new InvalidDataException("The installer has an invalid default installation root.");
		}

		var componentIds = new HashSet<string>(StringComparer.Ordinal);
		var payloadDirectories = new List<string>();
		var installDirectories = new List<string>();
		var totalFiles = 0;
		foreach (var component in manifest.Components)
		{
			if (component is null || component.Files is null)
			{
				throw new InvalidDataException("The installer manifest contains a null component or file list.");
			}
			if (component.Id is null || !ComponentIdRegex().IsMatch(component.Id) || !componentIds.Add(component.Id))
			{
				throw new InvalidDataException($"Invalid or duplicate component id '{component.Id}'.");
			}
			if (string.IsNullOrWhiteSpace(component.DisplayName) || component.DisplayName.Length > 128)
			{
				throw new InvalidDataException($"Component '{component.Id}' has an invalid display name.");
			}
			if (component.Description is null || component.Description.Length > 1024)
			{
				throw new InvalidDataException($"Component '{component.Id}' has an invalid description.");
			}
			ValidateLocalizedText(component.LocalizedNames, 128, $"component '{component.Id}' name");
			ValidateLocalizedText(component.LocalizedDescriptions, 1024, $"component '{component.Id}' description");
			if (!IsValidResourceKey(component.DisplayNameResourceKey)
				|| !IsValidResourceKey(component.DescriptionResourceKey))
			{
				throw new InvalidDataException($"Component '{component.Id}' has an invalid localization resource key.");
			}
			var installDirectory = component.InstallDirectory == "."
				? "."
				: PathSafety.NormalizeRelativePath(component.InstallDirectory);
			if (installDirectories.Any(existing => InstallDirectoriesOverlap(existing, installDirectory)))
			{
				throw new InvalidDataException($"Overlapping component installation directory '{installDirectory}'.");
			}
			installDirectories.Add(installDirectory);
			if (component.Required && !component.DefaultSelected)
			{
				throw new InvalidDataException($"Required component '{component.Id}' must be selected by default.");
			}
			if (component.Dependencies is null || component.Validation is null
				|| component.Validation.RequiredFiles is null
				|| component.Validation.RequiredExtensions is null
				|| component.Validation.RequiredFilePrefixes is null
				|| component.Validation.RequiredExecutables is null)
			{
				throw new InvalidDataException($"Component '{component.Id}' has null metadata collections.");
			}
			var payloadDirectory = PathSafety.NormalizeRelativePath(component.PayloadDirectory);
			if (payloadDirectories.Any(existing => PathsOverlap(existing, payloadDirectory)))
			{
				throw new InvalidDataException($"Overlapping component payload directory '{payloadDirectory}'.");
			}
			payloadDirectories.Add(payloadDirectory);
			if (component.Files.Count == 0)
			{
				throw new InvalidDataException($"Component '{component.Id}' has no files.");
			}

			var files = new HashSet<string>(PathSafety.RelativePathComparer);
			foreach (var file in component.Files)
			{
				if (file is null)
				{
					throw new InvalidDataException($"Component '{component.Id}' contains a null file entry.");
				}
				var relativePath = PathSafety.NormalizeRelativePath(file.Path);
				if (relativePath.Equals(InstallerConstants.InstallRecordFileName, StringComparison.OrdinalIgnoreCase))
				{
					throw new InvalidDataException("The installer payload cannot replace its installation record.");
				}
				if (!files.Add(relativePath))
				{
					throw new InvalidDataException($"Component '{component.Id}' contains duplicate file '{relativePath}'.");
				}
				if (file.Size < 0 || file.Size > InstallerConstants.MaximumFileSize)
				{
					throw new InvalidDataException($"File '{relativePath}' has an invalid size.");
				}
				if (file.Sha256 is null || !Sha256Regex().IsMatch(file.Sha256))
				{
					throw new InvalidDataException($"File '{relativePath}' has an invalid SHA-256 digest.");
				}
			}

			ValidateComponentRules(component, manifest.Platform);

			totalFiles = checked(totalFiles + component.Files.Count);
			if (totalFiles > InstallerConstants.MaximumFileCount)
			{
				throw new InvalidDataException("The installer manifest contains too many files.");
			}
		}
		foreach (var component in manifest.Components)
		{
			foreach (var dependency in component.Dependencies)
			{
				if (!ComponentIdRegex().IsMatch(dependency) || dependency == component.Id
					|| !componentIds.Contains(dependency))
				{
					throw new InvalidDataException(
						$"Component '{component.Id}' has invalid or missing dependency '{dependency}'.");
				}
			}
		}
		if (manifest.PathIntegration is not null)
		{
			if (!componentIds.Contains(manifest.PathIntegration.ComponentId))
			{
				throw new InvalidDataException("PATH integration refers to a component that is not in the package.");
			}
			if (manifest.PathIntegration.RelativeDirectory != ".")
			{
				_ = PathSafety.NormalizeRelativePath(manifest.PathIntegration.RelativeDirectory);
			}
		}
	}

	public static string ComputeFingerprint(InstallerManifest manifest)
	{
		Validate(manifest, requireCurrentPlatform: false);
		return Convert.ToHexString(SHA256.HashData(JsonSerializer.SerializeToUtf8Bytes(manifest, InstallerJson.Options)));
	}

	public static async Task<InstallerManifest> LoadAsync(string manifestPath, CancellationToken cancellationToken = default,
		bool requireCurrentPlatform = true)
	{
		var manifest = await InstallerJson.ReadAsync<InstallerManifest>(manifestPath, cancellationToken);
		Validate(manifest, requireCurrentPlatform);
		return manifest;
	}

	public static async Task VerifyPayloadAsync(
		InstallerManifest manifest,
		string payloadRoot,
		CancellationToken cancellationToken = default,
		bool requireCurrentPlatform = true)
	{
		Validate(manifest, requireCurrentPlatform);
		var fullPayloadRoot = Path.GetFullPath(payloadRoot);
		foreach (var component in manifest.Components)
		{
			var componentRoot = PathSafety.ResolveUnderRoot(fullPayloadRoot, component.PayloadDirectory);
			PathSafety.EnsureDirectoryIsNotLink(componentRoot);
			foreach (var file in component.Files)
			{
				var path = PathSafety.ResolveUnderRoot(componentRoot, file.Path);
				PathSafety.EnsurePathHasNoLinks(componentRoot, path);
				var info = new FileInfo(path);
				if (!info.Exists || info.Length != file.Size)
				{
					throw new InvalidDataException($"Payload file is missing or has the wrong size: {component.Id}/{file.Path}");
				}
				await using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024,
					FileOptions.Asynchronous | FileOptions.SequentialScan);
				var digest = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken));
				if (!digest.Equals(file.Sha256, StringComparison.OrdinalIgnoreCase))
				{
					throw new InvalidDataException($"Payload SHA-256 mismatch: {component.Id}/{file.Path}");
				}
			}
		}
	}

	private static void ValidateComponentRules(InstallerComponent component, string platform)
	{
		var files = component.Files
			.Select(file => file.Path.Replace('\\', '/'))
			.ToHashSet(StringComparer.OrdinalIgnoreCase);
		if (platform != "windows" && component.Id == "python-runtime")
		{
			var facades = files.Where(path => path.EndsWith("/veroscript/__init__.py", StringComparison.Ordinal)).ToArray();
			if (facades.Length != 1)
			{
				throw new InvalidDataException("Python Runtime must contain one native Python ABI package.");
			}

			var prefix = facades[0][..^"__init__.py".Length];
			var nativeLibrary = platform == "macos" ? "libVeroScriptInterop.dylib" : "libVeroScriptInterop.so";
			if (!files.Contains(prefix + "_veroscript.pyi") || !files.Contains(prefix + "py.typed")
				|| !files.Contains(prefix + nativeLibrary)
				|| !files.Any(path => path.StartsWith(prefix + "_veroscript.", StringComparison.Ordinal)
					&& path.EndsWith(".so", StringComparison.Ordinal)))
			{
				throw new InvalidDataException("Python Runtime is missing its facade, typing or native Interop runtime.");
			}
		}
		if (platform == "macos" && component.Id == "symbols"
			&& !files.Any(path => path.Contains(".dSYM/Contents/Resources/DWARF/", StringComparison.Ordinal)
				|| path.EndsWith(".a", StringComparison.Ordinal)))
		{
			throw new InvalidDataException("macOS Symbols must contain dSYM bundles or static archives with DWARF.");
		}

		foreach (var rawPath in component.Validation.RequiredFiles)
		{
			var requiredPath = PathSafety.NormalizeRelativePath(rawPath);
			if (!files.Contains(requiredPath))
			{
				throw new InvalidDataException(
					$"Component '{component.Id}' is missing required file '{requiredPath}'.");
			}
		}
		foreach (var extension in component.Validation.RequiredExtensions)
		{
			if (string.IsNullOrWhiteSpace(extension) || extension[0] != '.' || extension.Length > 16
				|| !files.Any(path => path.EndsWith(extension, StringComparison.OrdinalIgnoreCase)))
			{
				throw new InvalidDataException(
					$"Component '{component.Id}' is missing required file extension '{extension}'.");
			}
		}
		foreach (var rawPrefix in component.Validation.RequiredFilePrefixes)
		{
			var prefix = PathSafety.NormalizeRelativePath(rawPrefix).TrimEnd('/') + '/';
			if (!files.Any(path => path.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)))
			{
				throw new InvalidDataException(
					$"Component '{component.Id}' has no files below required prefix '{prefix}'.");
			}
		}
		foreach (var rawPath in component.Validation.RequiredExecutables)
		{
			var requiredPath = PathSafety.NormalizeRelativePath(rawPath);
			var entry = component.Files.SingleOrDefault(file => file.Path.Replace('\\', '/').Equals(
				requiredPath, StringComparison.OrdinalIgnoreCase));
			if (entry is null || (platform != "windows" && !entry.Executable))
			{
				throw new InvalidDataException(
					$"Component '{component.Id}' is missing required executable '{requiredPath}'.");
			}
		}
	}

	private static bool PathsOverlap(string left, string right) =>
		left.Equals(right, StringComparison.OrdinalIgnoreCase)
		|| left.StartsWith(right + '/', StringComparison.OrdinalIgnoreCase)
		|| right.StartsWith(left + '/', StringComparison.OrdinalIgnoreCase);

	private static bool InstallDirectoriesOverlap(string left, string right) =>
		left == "." || right == "." || PathsOverlap(left, right);

	private static void ValidateLocalizedText(
		IReadOnlyDictionary<string, string>? values,
		int maximumLength,
		string field)
	{
		if (values is null || values.Count > 128)
		{
			throw new InvalidDataException($"Invalid localized {field} collection.");
		}

		foreach (var pair in values)
		{
			if (!CultureTagRegex().IsMatch(pair.Key) || string.IsNullOrWhiteSpace(pair.Value)
				|| pair.Value.Length > maximumLength)
			{
				throw new InvalidDataException($"Invalid localized {field} for culture '{pair.Key}'.");
			}
		}
	}

	private static bool IsValidResourceKey(string? value) => value is null
		|| (value.Length is > 0 and <= 96 && value.All(character => char.IsAsciiLetterOrDigit(character)
			|| character is '.' or '_' or '-'));

	[GeneratedRegex("^[a-z][a-z0-9._-]{0,63}$", RegexOptions.CultureInvariant)]
	private static partial Regex ComponentIdRegex();

	[GeneratedRegex("^[a-z][a-z0-9-]{0,63}$", RegexOptions.CultureInvariant)]
	private static partial Regex ProfileIdRegex();

	[GeneratedRegex("^[a-z][a-z0-9]*(?:\\.[a-z][a-z0-9-]*)+$", RegexOptions.CultureInvariant)]
	private static partial Regex ProductIdRegex();

	[GeneratedRegex("^[A-Za-z]{2,8}(?:-[A-Za-z0-9]{1,8})+$", RegexOptions.CultureInvariant)]
	private static partial Regex CultureTagRegex();

	[GeneratedRegex("^[0-9A-Fa-f]{64}$", RegexOptions.CultureInvariant)]
	private static partial Regex Sha256Regex();
}
