// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller.Core;

public static class PathSafety
{
	// Bundles are portable artifacts. Reject case-only collisions even when they
	// are produced on a case-sensitive host, because they cannot be installed
	// safely on the default Windows and macOS filesystems.
	public static StringComparer RelativePathComparer => StringComparer.OrdinalIgnoreCase;

	public static string NormalizeRelativePath(string path)
	{
		if (string.IsNullOrWhiteSpace(path) || path.Length > 1024 || Path.IsPathRooted(path))
		{
			throw new InvalidDataException($"Unsafe relative path '{path}'.");
		}
		var normalized = path.Replace('\\', '/');
		if (normalized.StartsWith('/') || normalized.EndsWith('/') || normalized.Contains("//", StringComparison.Ordinal))
		{
			throw new InvalidDataException($"Unsafe relative path '{path}'.");
		}
		var segments = normalized.Split('/');
		if (segments.Length == 0 || segments.Any(IsUnsafeSegment))
		{
			throw new InvalidDataException($"Unsafe relative path '{path}'.");
		}
		return string.Join('/', segments);
	}

	private static bool IsUnsafeSegment(string segment)
	{
		if (segment is "" or "." or ".." || segment.Length > 255
			|| segment.EndsWith(' ') || segment.EndsWith('.') || segment.Contains(':')
			|| segment.Any(character => character < 0x20 || character is '<' or '>' or '"' or '|' or '?' or '*'))
		{
			return true;
		}
		var stem = segment.Split('.')[0];
		return stem.Equals("CON", StringComparison.OrdinalIgnoreCase)
			|| stem.Equals("CONIN$", StringComparison.OrdinalIgnoreCase)
			|| stem.Equals("CONOUT$", StringComparison.OrdinalIgnoreCase)
			|| stem.Equals("CLOCK$", StringComparison.OrdinalIgnoreCase)
			|| stem.Equals("PRN", StringComparison.OrdinalIgnoreCase)
			|| stem.Equals("AUX", StringComparison.OrdinalIgnoreCase)
			|| stem.Equals("NUL", StringComparison.OrdinalIgnoreCase)
			|| (stem.Length == 4 && (stem.StartsWith("COM", StringComparison.OrdinalIgnoreCase)
				|| stem.StartsWith("LPT", StringComparison.OrdinalIgnoreCase))
				&& stem[3] is >= '1' and <= '9');
	}

	public static string ResolveUnderRoot(string root, string relativePath)
	{
		var normalized = NormalizeRelativePath(relativePath);
		var fullRoot = Path.GetFullPath(root);
		var candidate = Path.GetFullPath(Path.Combine(fullRoot, normalized.Replace('/', Path.DirectorySeparatorChar)));
		if (!IsWithinRoot(fullRoot, candidate))
		{
			throw new InvalidDataException($"Path escapes its root: {relativePath}");
		}
		return candidate;
	}

	internal static bool IsWithinRoot(string root, string candidate)
	{
		var relative = Path.GetRelativePath(Path.GetFullPath(root), Path.GetFullPath(candidate));
		return relative != ".."
			&& !relative.StartsWith(".." + Path.DirectorySeparatorChar, StringComparison.Ordinal)
			&& !Path.IsPathRooted(relative);
	}

	internal static void EnsureDirectoryIsNotLink(string path)
	{
		var info = new DirectoryInfo(path);
		if (!info.Exists)
		{
			throw new DirectoryNotFoundException($"Payload directory not found: {path}");
		}
		EnsureNotLink(info);
	}

	internal static void EnsurePathHasNoLinks(string root, string path)
	{
		var fullRoot = Path.GetFullPath(root);
		var fullPath = Path.GetFullPath(path);
		if (!IsWithinRoot(fullRoot, fullPath))
		{
			throw new InvalidDataException($"Path escapes its root: {path}");
		}
		EnsureNotLink(new DirectoryInfo(fullRoot));
		var relative = Path.GetRelativePath(fullRoot, fullPath);
		var current = fullRoot;
		var segments = relative.Split(new[] { Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar },
			StringSplitOptions.RemoveEmptyEntries);
		for (var index = 0; index < segments.Length; ++index)
		{
			current = Path.Combine(current, segments[index]);
			FileSystemInfo info = index == segments.Length - 1 ? new FileInfo(current) : new DirectoryInfo(current);
			if (!info.Exists)
			{
				throw new FileNotFoundException("Payload entry not found.", current);
			}
			EnsureNotLink(info);
		}
	}

	internal static void EnsureNotLink(FileSystemInfo info)
	{
		if (info.LinkTarget is not null || (info.Attributes & FileAttributes.ReparsePoint) != 0)
		{
			throw new InvalidDataException($"Symbolic links and reparse points are not allowed in installer payloads: {info.FullName}");
		}
	}

	internal static IEnumerable<string> EnumerateFilesWithoutLinks(string root)
	{
		var fullRoot = Path.GetFullPath(root);
		EnsureDirectoryIsNotLink(fullRoot);
		var pending = new Stack<string>();
		pending.Push(fullRoot);
		while (pending.Count > 0)
		{
			var directory = pending.Pop();
			foreach (var entry in Directory.EnumerateFileSystemEntries(directory))
			{
				var attributes = File.GetAttributes(entry);
				if ((attributes & FileAttributes.Directory) != 0)
				{
					var info = new DirectoryInfo(entry);
					EnsureNotLink(info);
					pending.Push(entry);
				}
				else
				{
					var info = new FileInfo(entry);
					EnsureNotLink(info);
					yield return entry;
				}
			}
		}
	}

	internal static IReadOnlyList<string> EnumerateDirectoriesWithoutLinks(string root)
	{
		var fullRoot = Path.GetFullPath(root);
		EnsureDirectoryIsNotLink(fullRoot);
		var result = new List<string>();
		var pending = new Stack<string>();
		pending.Push(fullRoot);
		while (pending.Count > 0)
		{
			var directory = pending.Pop();
			foreach (var entry in Directory.EnumerateDirectories(directory))
			{
				EnsureNotLink(new DirectoryInfo(entry));
				result.Add(entry);
				pending.Push(entry);
			}
		}
		return result;
	}
}
