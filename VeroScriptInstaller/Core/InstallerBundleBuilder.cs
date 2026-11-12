// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller.Core;

public static class InstallerBundleBuilder
{
	public static async Task<string> BuildAsync(
		string outputDirectory,
		string productVersion,
		IReadOnlyDictionary<string, string> componentSources,
		string? platform = null,
		string? architecture = null,
		string? productId = null,
		string? productName = null,
		string? profileId = null,
		InstallerProductDefinition? productDefinition = null,
		CancellationToken cancellationToken = default,
		bool functionalPayloadOnly = false)
	{
		ArgumentNullException.ThrowIfNull(componentSources);
		if (functionalPayloadOnly)
		{
			componentSources = componentSources.Where(pair => pair.Key is not ("docs" or "examples"))
				.ToDictionary(pair => pair.Key, pair => pair.Value, StringComparer.Ordinal);
		}
		// Historical module profiles remain readable for existing installations.
		// Modules, including future definitions, never produce new installers.
		var definition = productDefinition ?? (profileId is not null ? InstallerProductCatalog.Get(profileId)
			: componentSources.Count > 0 ? InstallerProductCatalog.Infer(productId, componentSources.Keys) : null);
		if (string.Equals(definition?.ReleaseGroup, "Modules", StringComparison.OrdinalIgnoreCase)
			|| definition?.ProfileId == InstallerProductCatalog.AsmJitProfileId
			|| definition?.ProductId == "dev.veroscript.jit.asmjit"
			|| productId == "dev.veroscript.jit.asmjit")
		{
			throw new InvalidOperationException("Modules are distributed as functional packages and do not provide standalone installers.");
		}
		if (componentSources.Count == 0)
		{
			throw new ArgumentException("At least one component source is required.", nameof(componentSources));
		}
		var output = Path.GetFullPath(outputDirectory);
		var parent = Path.GetDirectoryName(output)
			?? throw new ArgumentException("The output directory must have a parent.", nameof(outputDirectory));
		var leaf = Path.GetFileName(output.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
		if (string.IsNullOrWhiteSpace(leaf))
		{
			throw new ArgumentException("Refusing to use a filesystem root as the bundle output.", nameof(outputDirectory));
		}
		if (File.Exists(output))
		{
			throw new IOException($"The bundle output is an existing file: {output}");
		}
		var sources = componentSources
			.Select(pair => new KeyValuePair<string, string>(pair.Key, Path.GetFullPath(pair.Value)))
			.OrderBy(pair => pair.Key, StringComparer.Ordinal)
			.ToArray();
		ValidateSourceAndOutputRoots(output, sources);
		Directory.CreateDirectory(parent);
		if (Directory.Exists(output) && Directory.EnumerateFileSystemEntries(output).Any())
		{
			var existingManifest = Path.Combine(output, InstallerConstants.ManifestFileName);
			if (!File.Exists(existingManifest))
			{
				throw new IOException($"Refusing to replace a non-empty directory that is not an installer bundle: {output}");
			}
			var existing = await InstallerJson.ReadAsync<InstallerManifest>(existingManifest, cancellationToken);
			try
			{
				ManifestValidator.Validate(existing, requireCurrentPlatform: false);
			}
			catch (Exception exception) when (exception is InvalidDataException or PlatformNotSupportedException)
			{
				throw new IOException($"Refusing to replace an invalid installer bundle: {output}", exception);
			}
		}

		var token = Guid.NewGuid().ToString("N");
		var stage = Path.Combine(parent, $".{leaf}.veroscript-bundle-stage-{token}");
		var backup = Path.Combine(parent, $".{leaf}.veroscript-bundle-backup-{token}");
		var committed = false;
		var backupMoved = false;
		try
		{
			var payload = Path.Combine(stage, InstallerConstants.PayloadDirectoryName);
			Directory.CreateDirectory(payload);
			var componentPayloads = new Dictionary<string, string>(StringComparer.Ordinal);
			foreach (var pair in sources)
			{
				cancellationToken.ThrowIfCancellationRequested();
				var source = Path.GetFullPath(pair.Value);
				PathSafety.EnsureDirectoryIsNotLink(source);
				var relativePayload = Path.Combine(InstallerConstants.PayloadDirectoryName, pair.Key).Replace('\\', '/');
				var destination = Path.Combine(stage, relativePayload.Replace('/', Path.DirectorySeparatorChar));
				await CopyDirectoryAsync(source, destination, cancellationToken, functionalPayloadOnly);
				componentPayloads[pair.Key] = relativePayload;
			}

			var manifest = await PackageManifestBuilder.BuildAsync(
				stage, productVersion, componentPayloads, platform, architecture,
				productId, productName, profileId, productDefinition, cancellationToken);
			await PackageManifestBuilder.WriteAsync(
				Path.Combine(stage, InstallerConstants.ManifestFileName), manifest, cancellationToken);
			await ManifestValidator.VerifyPayloadAsync(manifest, stage, cancellationToken, requireCurrentPlatform: false);

			if (Directory.Exists(output))
			{
				await FileSystemRetry.MoveDirectoryAsync(output, backup, cancellationToken);
				backupMoved = true;
			}
			await FileSystemRetry.MoveDirectoryAsync(stage, output, cancellationToken);
			committed = true;
			await FileSystemRetry.TryDeleteDirectoryAsync(backup);
			return Path.Combine(output, InstallerConstants.ManifestFileName);
		}
		catch
		{
			if (committed && Directory.Exists(output))
			{
				await FileSystemRetry.TryDeleteDirectoryAsync(output);
			}
			if (backupMoved && Directory.Exists(backup) && !Directory.Exists(output))
			{
				await FileSystemRetry.MoveDirectoryAsync(backup, output, CancellationToken.None);
			}
			throw;
		}
		finally
		{
			await FileSystemRetry.TryDeleteDirectoryAsync(stage);
		}
	}

	private static void ValidateSourceAndOutputRoots(
		string output,
		IReadOnlyList<KeyValuePair<string, string>> sources)
	{
		for (var index = 0; index < sources.Count; ++index)
		{
			var source = sources[index].Value;
			if (!Directory.Exists(source))
			{
				throw new DirectoryNotFoundException($"Component source directory not found: {source}");
			}
			var sourceRoot = Path.GetPathRoot(source);
			if (string.IsNullOrEmpty(sourceRoot) || PathsEqual(source, sourceRoot))
			{
				throw new InvalidOperationException("A filesystem root cannot be used as a component source.");
			}
			if (PathsEqual(source, output)
				|| PathSafety.IsWithinRoot(source, output)
				|| PathSafety.IsWithinRoot(output, source))
			{
				throw new InvalidOperationException("Component sources and bundle output must not overlap.");
			}
			for (var other = index + 1; other < sources.Count; ++other)
			{
				var otherSource = sources[other].Value;
				if (PathsEqual(source, otherSource)
					|| PathSafety.IsWithinRoot(source, otherSource)
					|| PathSafety.IsWithinRoot(otherSource, source))
				{
					throw new InvalidOperationException("Component source directories must not overlap.");
				}
			}
		}
	}

	private static bool PathsEqual(string left, string right) => string.Equals(
		Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
		Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
		OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);

	public static bool IsAuxiliaryContent(string relative)
	{
		var parts = relative.Replace('\\', '/').Split('/');
		string[] directories = ["doc", "docs", "documentation", "tutorial", "tutorials", "example", "examples",
			"sample", "samples", "test", "tests", "benchmark", "benchmarks", "history", "standards"];
		string[] documents = ["readme", "changelog", "changes", "contributing"];
		var name = parts[^1];
		return parts.SkipLast(1).Any(part => directories.Contains(part, StringComparer.OrdinalIgnoreCase))
			|| documents.Any(word => name.Equals(word, StringComparison.OrdinalIgnoreCase)
				|| new[] { ".", "-", "_" }.Any(suffix => name.StartsWith(word + suffix, StringComparison.OrdinalIgnoreCase)));
	}

	private static async Task CopyDirectoryAsync(string sourceRoot, string destinationRoot, CancellationToken cancellationToken,
		bool functionalPayloadOnly)
	{
		Directory.CreateDirectory(destinationRoot);
		foreach (var source in PathSafety.EnumerateFilesWithoutLinks(sourceRoot))
		{
			cancellationToken.ThrowIfCancellationRequested();
			var relative = Path.GetRelativePath(sourceRoot, source);
			if (functionalPayloadOnly && IsAuxiliaryContent(relative))
			{
				continue;
			}
			if (relative.Split(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
				.Any(segment => string.Equals(segment, ".veroscript", StringComparison.OrdinalIgnoreCase)))
			{
				continue;
			}
			PathSafety.EnsurePathHasNoLinks(sourceRoot, source);
			var destination = Path.Combine(destinationRoot, relative);
			Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
			await using var input = new FileStream(source, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024,
				FileOptions.Asynchronous | FileOptions.SequentialScan);
			await using var output = new FileStream(destination, FileMode.CreateNew, FileAccess.Write, FileShare.None, 1024 * 1024,
				FileOptions.Asynchronous | FileOptions.WriteThrough);
			await input.CopyToAsync(output, 1024 * 1024, cancellationToken);
			await output.FlushAsync(cancellationToken);
			if (!OperatingSystem.IsWindows())
			{
				File.SetUnixFileMode(destination, File.GetUnixFileMode(source));
			}
		}
	}

}
