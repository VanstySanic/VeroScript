// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Text;

namespace VeroScriptInstaller.Core.Platform;

internal sealed class UnixShellPathIntegration : IPathIntegration
{
	private const long MaximumProfileBytes = 16L * 1024 * 1024;
	private const string StartMarker = "# >>> VeroScript PATH >>>";
	private const string EndMarker = "# <<< VeroScript PATH <<<";
	private readonly string? _profilePathOverride;

	internal UnixShellPathIntegration(string? profilePathOverride = null)
	{
		_profilePathOverride = profilePathOverride;
	}

	public async Task<bool> ContainsUserPathAsync(
		string directory,
		CancellationToken cancellationToken = default)
	{
		var fullPath = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar);
		if (fullPath.Any(character => character is '\r' or '\n'))
		{
			throw new InvalidOperationException("A PATH entry cannot contain a line break.");
		}
		var profile = ResolveProfilePath();
		if (!File.Exists(profile))
		{
			return false;
		}
		var content = await ReadProfileAsync(profile, cancellationToken);
		_ = ReplaceManagedBlock(content, string.Empty, out var existingBlock);
		return existingBlock is not null
			&& ParseManagedBlock(existingBlock).Contains(BuildPathLine(fullPath), StringComparer.Ordinal);
	}

	public async Task<PathMutation> AddToUserPathAsync(string directory, CancellationToken cancellationToken = default)
	{
		var fullPath = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar);
		if (fullPath.Any(character => character is '\r' or '\n'))
		{
			throw new InvalidOperationException("A PATH entry cannot contain a line break.");
		}
		var profile = ResolveProfilePath();
		var previous = File.Exists(profile)
			? await ReadProfileAsync(profile, cancellationToken)
			: string.Empty;
		_ = ReplaceManagedBlock(previous, string.Empty, out var existingBlock);
		var pathLine = BuildPathLine(fullPath);
		if (existingBlock is not null && ParseManagedBlock(existingBlock).Contains(pathLine, StringComparer.Ordinal))
		{
			return new PathMutation { Entry = fullPath, Changed = false, StoragePath = profile };
		}
		var lines = existingBlock is null ? [] : ParseManagedBlock(existingBlock);
		lines.Add(pathLine);
		var updated = ReplaceManagedBlock(previous, BuildBlock(lines), out _);
		Directory.CreateDirectory(Path.GetDirectoryName(profile)!);
		await WriteAtomicallyAsync(profile, updated, cancellationToken);
		return new PathMutation
		{
			Entry = fullPath,
			Changed = true,
			StoragePath = profile,
		};
	}

	public async Task UndoAsync(PathMutation mutation, CancellationToken cancellationToken = default)
	{
		if (!mutation.Changed || mutation.StoragePath is null)
		{
			return;
		}
		await RemoveFromProfileAsync(mutation.StoragePath, mutation.Entry, cancellationToken);
	}

	public async Task RemoveFromUserPathAsync(string directory, CancellationToken cancellationToken = default)
	{
		var profile = ResolveProfilePath();
		await RemoveFromProfileAsync(profile, directory, cancellationToken);
	}

	private static async Task RemoveFromProfileAsync(
		string profile,
		string directory,
		CancellationToken cancellationToken)
	{
		if (!File.Exists(profile))
		{
			return;
		}
		var previous = await ReadProfileAsync(profile, cancellationToken);
		_ = ReplaceManagedBlock(previous, string.Empty, out var existingBlock);
		if (existingBlock is null)
		{
			return;
		}
		var lines = ParseManagedBlock(existingBlock);
		if (!lines.Remove(BuildPathLine(Path.GetFullPath(directory))))
		{
			return;
		}
		var replacement = lines.Count == 0 ? string.Empty : BuildBlock(lines);
		var updated = ReplaceManagedBlock(previous, replacement, out _);
		await WriteAtomicallyAsync(profile, updated, cancellationToken);
	}

	private string ResolveProfilePath()
	{
		if (_profilePathOverride is not null)
		{
			return Path.GetFullPath(_profilePathOverride);
		}
		return ResolveDefaultProfilePath();
	}

	private static string ResolveDefaultProfilePath()
	{
		var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
		if (string.IsNullOrWhiteSpace(home))
		{
			throw new InvalidOperationException("The current user's home directory is unavailable.");
		}
		var shell = Environment.GetEnvironmentVariable("SHELL") ?? string.Empty;
		var shellName = Path.GetFileName(shell);
		if (shellName.Equals("zsh", StringComparison.Ordinal))
		{
			return Path.Combine(home, ".zprofile");
		}
		if (shellName.Equals("bash", StringComparison.Ordinal))
		{
			return Path.Combine(home, OperatingSystem.IsMacOS() ? ".bash_profile" : ".bashrc");
		}
		return Path.Combine(home, ".profile");
	}

	private static string BuildBlock(IReadOnlyList<string> pathLines) =>
		StartMarker + Environment.NewLine
		+ string.Join(Environment.NewLine, pathLines) + Environment.NewLine
		+ EndMarker;

	private static string BuildPathLine(string directory) =>
		$"export PATH={ShellQuote(Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar))}:\"$PATH\"";

	private static string ShellQuote(string value) => "'" + value.Replace("'", "'\"'\"'", StringComparison.Ordinal) + "'";

	private static string ReplaceManagedBlock(string content, string replacement, out string? existingBlock)
	{
		var start = content.IndexOf(StartMarker, StringComparison.Ordinal);
		if (start < 0)
		{
			existingBlock = null;
			if (replacement.Length == 0)
			{
				return content;
			}
			var separator = content.Length == 0 || content.EndsWith('\n') ? string.Empty : Environment.NewLine;
			return content + separator + replacement + Environment.NewLine;
		}
		var end = content.IndexOf(EndMarker, start, StringComparison.Ordinal);
		if (end < 0)
		{
			throw new InvalidDataException("The shell profile contains an incomplete VeroScript PATH block.");
		}
		end += EndMarker.Length;
		existingBlock = content[start..end];
		if (content.IndexOf(StartMarker, end, StringComparison.Ordinal) >= 0
			|| content.IndexOf(EndMarker, end, StringComparison.Ordinal) >= 0)
		{
			throw new InvalidDataException("The shell profile contains multiple VeroScript PATH blocks.");
		}
		if (content.IndexOf(EndMarker, 0, start, StringComparison.Ordinal) >= 0)
		{
			throw new InvalidDataException("The shell profile contains an unmatched VeroScript PATH marker.");
		}
		return content[..start] + replacement + content[end..];
	}

	private static List<string> ParseManagedBlock(string block)
	{
		var lines = block.Replace("\r\n", "\n", StringComparison.Ordinal).Split('\n').ToList();
		if (lines.Count < 2 || lines[0] != StartMarker || lines[^1] != EndMarker)
		{
			throw new InvalidDataException("The shell profile contains an invalid VeroScript PATH block.");
		}
		lines.RemoveAt(lines.Count - 1);
		lines.RemoveAt(0);
		if (lines.Any(line => !line.StartsWith("export PATH=", StringComparison.Ordinal)
			|| !line.EndsWith(":\"$PATH\"", StringComparison.Ordinal))
			|| lines.Distinct(StringComparer.Ordinal).Count() != lines.Count)
		{
			throw new InvalidDataException("The shell profile contains an invalid VeroScript PATH entry.");
		}
		return lines;
	}

	private static async Task<string> ReadProfileAsync(string path, CancellationToken cancellationToken)
	{
		var info = new FileInfo(path);
		if (info.Length > MaximumProfileBytes)
		{
			throw new InvalidDataException("The shell profile is too large to update safely.");
		}
		return await File.ReadAllTextAsync(path, cancellationToken);
	}

	private static async Task WriteAtomicallyAsync(string path, string content, CancellationToken cancellationToken)
	{
		var profileInfo = new FileInfo(path);
		if (profileInfo.LinkTarget is not null)
		{
			path = profileInfo.ResolveLinkTarget(returnFinalTarget: true)?.FullName
				?? throw new IOException($"Cannot resolve shell profile symbolic link: {path}");
		}
		var directory = Path.GetDirectoryName(path)!;
		Directory.CreateDirectory(directory);
		var temporary = Path.Combine(directory, $".{Path.GetFileName(path)}.veroscript-{Guid.NewGuid():N}.tmp");
		try
		{
			await File.WriteAllTextAsync(temporary, content, new UTF8Encoding(false), cancellationToken);
			if (!OperatingSystem.IsWindows() && File.Exists(path))
			{
				File.SetUnixFileMode(temporary, File.GetUnixFileMode(path));
			}
			File.Move(temporary, path, overwrite: true);
		}
		finally
		{
			if (File.Exists(temporary))
			{
				File.Delete(temporary);
			}
		}
	}
}
