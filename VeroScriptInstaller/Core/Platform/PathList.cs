// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller.Core.Platform;

internal static class PathList
{
	internal static bool Contains(string? value, string directory)
	{
		var normalized = Normalize(directory);
		return Split(value).Any(entry => PathEquals(Normalize(entry), normalized));
	}

	internal static string Append(string? value, string directory)
	{
		if (Contains(value, directory))
		{
			return value ?? string.Empty;
		}
		var entry = Path.GetFullPath(directory).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
		if (string.IsNullOrEmpty(value))
		{
			return entry;
		}
		// Add a separate delimiter even when PATH already ends in one. This
		// preserves an existing empty entry instead of consuming its meaning.
		return value + Path.PathSeparator + entry;
	}

	internal static string Remove(string? value, string directory)
	{
		var normalized = Normalize(directory);
		return string.Join(Path.PathSeparator,
			SplitPreservingEmptyEntries(value).Where(entry => string.IsNullOrWhiteSpace(entry)
				|| !PathEquals(Normalize(entry), normalized)));
	}

	private static IEnumerable<string> Split(string? value) => SplitPreservingEmptyEntries(value)
		.Where(entry => !string.IsNullOrWhiteSpace(entry));

	private static IEnumerable<string> SplitPreservingEmptyEntries(string? value) => (value ?? string.Empty)
		.Split(Path.PathSeparator, StringSplitOptions.None);

	private static string Normalize(string value)
	{
		var trimmed = value.Trim().Trim('"');
		if (OperatingSystem.IsWindows())
		{
			trimmed = Environment.ExpandEnvironmentVariables(trimmed);
		}
		try
		{
			return Path.GetFullPath(trimmed).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
		}
		catch
		{
			return trimmed.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
		}
	}

	private static bool PathEquals(string left, string right) => string.Equals(
		left, right, OperatingSystem.IsWindows() ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
}
