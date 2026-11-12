// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.IO.Compression;

namespace VeroScriptInstaller.Core;

/// <summary>Safely materializes an installer bundle stored in a ZIP stream.</summary>
public static class InstallerBundleArchive
{
	private const long MaximumExpandedSize = 32L * 1024 * 1024 * 1024;

	public static async Task<string> ExtractAndVerifyAsync(
		Stream archiveStream,
		string destinationDirectory,
		CancellationToken cancellationToken = default)
	{
		ArgumentNullException.ThrowIfNull(archiveStream);
		if (!archiveStream.CanRead)
		{
			throw new ArgumentException("The installer archive stream is not readable.", nameof(archiveStream));
		}

		var destination = Path.GetFullPath(destinationDirectory);
		var root = Path.GetPathRoot(destination);
		if (string.IsNullOrEmpty(root) || PathsEqual(destination, root))
		{
			throw new ArgumentException("Refusing to extract an installer archive to a filesystem root.",
				nameof(destinationDirectory));
		}
		var destinationInfo = new DirectoryInfo(destination);
		if (destinationInfo.Exists || destinationInfo.LinkTarget is not null)
		{
			// Check before enumeration, writing, or failure cleanup: an empty
			// directory link can otherwise redirect extraction outside this root.
			PathSafety.EnsureNotLink(destinationInfo);
		}
		if (File.Exists(destination)
			|| (Directory.Exists(destination) && Directory.EnumerateFileSystemEntries(destination).Any()))
		{
			throw new IOException("The installer archive destination must be absent or empty.");
		}

		Directory.CreateDirectory(destination);
		PathSafety.EnsureDirectoryIsNotLink(destination);
		try
		{
			using var archive = new ZipArchive(archiveStream, ZipArchiveMode.Read, leaveOpen: true);
			if (archive.Entries.Count == 0 || archive.Entries.Count > InstallerConstants.MaximumFileCount + 1024)
			{
				throw new InvalidDataException("The embedded installer archive has an invalid entry count.");
			}

			var paths = new HashSet<string>(PathSafety.RelativePathComparer);
			long expandedSize = 0;
			foreach (var entry in archive.Entries)
			{
				cancellationToken.ThrowIfCancellationRequested();
				var rawName = entry.FullName.Replace('\\', '/');
				var isDirectory = rawName.EndsWith("/", StringComparison.Ordinal);
				var pathText = isDirectory ? rawName.TrimEnd('/') : rawName;
				var relative = PathSafety.NormalizeRelativePath(pathText);
				if (!paths.Add(relative))
				{
					throw new InvalidDataException($"Duplicate embedded installer entry '{relative}'.");
				}
				if (IsSymbolicLink(entry))
				{
					throw new InvalidDataException($"Links are not allowed in the embedded installer archive: {relative}");
				}

				var outputPath = PathSafety.ResolveUnderRoot(destination, relative);
				if (isDirectory)
				{
					Directory.CreateDirectory(outputPath);
					continue;
				}
				if (entry.Length < 0 || entry.Length > InstallerConstants.MaximumFileSize
					|| expandedSize > MaximumExpandedSize - entry.Length)
				{
					throw new InvalidDataException("The embedded installer archive exceeds its safe expanded size.");
				}
				expandedSize += entry.Length;
				Directory.CreateDirectory(Path.GetDirectoryName(outputPath)!);
				await using var input = entry.Open();
				await using var output = new FileStream(
					outputPath,
					FileMode.CreateNew,
					FileAccess.Write,
					FileShare.None,
					1024 * 1024,
					FileOptions.Asynchronous | FileOptions.WriteThrough);
				await input.CopyToAsync(output, 1024 * 1024, cancellationToken);
				await output.FlushAsync(cancellationToken);
				if (output.Length != entry.Length)
				{
					throw new InvalidDataException($"Embedded installer entry was truncated: {relative}");
				}
			}

			var manifestPath = Path.Combine(destination, InstallerConstants.ManifestFileName);
			var manifest = await ManifestValidator.LoadAsync(manifestPath, cancellationToken);
			await ManifestValidator.VerifyPayloadAsync(manifest, destination, cancellationToken);
			return manifestPath;
		}
		catch
		{
			await FileSystemRetry.TryDeleteDirectoryAsync(destination);
			throw;
		}
	}

	private static bool IsSymbolicLink(ZipArchiveEntry entry)
	{
		const int UnixFileTypeMask = 0xF000;
		const int UnixSymbolicLink = 0xA000;
		return ((entry.ExternalAttributes >> 16) & UnixFileTypeMask) == UnixSymbolicLink;
	}

	private static bool PathsEqual(string left, string right) => string.Equals(
		Path.GetFullPath(left).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
		Path.GetFullPath(right).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar),
		OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
}
