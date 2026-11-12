// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace VeroScript.CSharpTools;

/// <summary>Identifies output paths through existing directory and file links before staging.</summary>
internal static class OutputPathIdentity
{
	internal static void Validate(IEnumerable<string> inputs, IEnumerable<string> outputs)
	{
		var comparer = OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal;
		var resolvedPaths = new Dictionary<string, string>(comparer);
		var activeLinks = new HashSet<string>(comparer);
		string[] outputPaths = outputs.Where(path => !string.IsNullOrWhiteSpace(path))
			.Select(path => Resolve(path, resolvedPaths, activeLinks)).ToArray();
		string[] inputPaths = inputs.Select(path => Resolve(path, resolvedPaths, activeLinks)).ToArray();
		if (!OperatingSystem.IsWindows())
		{
			// Unix can also mount case- or normalization-insensitive filesystems
			// (APFS, NTFS in WSL). Inspect actual names only for colliding paths, keeping
			// ordinary project validation free of directory enumeration.
			var directories = new Dictionary<string, ILookup<string, string>>(StringComparer.Ordinal);
			var aliases = outputPaths.Concat(inputPaths)
				.GroupBy(path => path.Normalize(NormalizationForm.FormC), StringComparer.OrdinalIgnoreCase)
				.Where(group => group.Distinct(StringComparer.Ordinal).Skip(1).Any())
				.SelectMany(group => group).Distinct(StringComparer.Ordinal)
				.ToDictionary(path => path, path => ResolveUnixNameAlias(path, directories), StringComparer.Ordinal);
			outputPaths = outputPaths.Select(path => aliases.GetValueOrDefault(path, path)).ToArray();
			inputPaths = inputPaths.Select(path => aliases.GetValueOrDefault(path, path)).ToArray();
		}
		if (outputPaths.Distinct(comparer).Count() != outputPaths.Length)
		{
			throw new ArgumentException("Output paths must be distinct, including linked paths.");
		}
		var inputSet = inputPaths.ToHashSet(comparer);
		if (outputPaths.Any(inputSet.Contains))
		{
			throw new ArgumentException("An output path cannot overwrite an input source file, including through a link.");
		}
	}

	private static string ResolveUnixNameAlias(string path, Dictionary<string, ILookup<string, string>> directories)
	{
		string root = Path.GetPathRoot(path)!;
		string resolved = root;
		var options = new EnumerationOptions
		{
			AttributesToSkip = 0,
			IgnoreInaccessible = false,
		};
		foreach (string component in path[root.Length..].Split(Path.DirectorySeparatorChar,
			StringSplitOptions.RemoveEmptyEntries))
		{
			string candidate = Path.Combine(resolved, component);
			if (File.Exists(candidate) || Directory.Exists(candidate))
			{
				if (!directories.TryGetValue(resolved, out var entries))
				{
					entries = Directory.EnumerateFileSystemEntries(resolved, "*", options)
						.ToLookup(entry => Path.GetFileName(entry).Normalize(NormalizationForm.FormC), StringComparer.OrdinalIgnoreCase);
					directories.Add(resolved, entries);
				}
				// On a case/normalization-sensitive volume both spellings may exist. Prefer
				// the exact name, and share each directory listing across paths.
				IEnumerable<string> matches = entries[component.Normalize(NormalizationForm.FormC)];
				candidate = matches.FirstOrDefault(entry =>
					StringComparer.Ordinal.Equals(Path.GetFileName(entry), component))
					?? matches.FirstOrDefault() ?? candidate;
			}
			resolved = candidate;
		}
		return resolved;
	}

	private static string Resolve(string path, Dictionary<string, string> resolvedPaths, HashSet<string> activeLinks)
	{
		string fullPath = Path.GetFullPath(path);
		string root = Path.GetPathRoot(fullPath)!;
		if (!resolvedPaths.TryGetValue(root, out string? resolved))
		{
			resolved = OperatingSystem.IsWindows() && Directory.Exists(root)
				? ResolveWindowsEntry(root) : root;
			resolvedPaths.Add(root, resolved);
		}
		// Resolve each existing ancestor: resolving just the final entry misses
		// an output in a junction or a symlinked directory, even if it is new.
		foreach (string component in fullPath[root.Length..].Split(
			[Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar], StringSplitOptions.RemoveEmptyEntries))
		{
			string candidate = Path.Combine(resolved, component);
			// Project outputs share most ancestors; inspect each path once in
			// this validation instead of repeating filesystem probes per file.
			if (resolvedPaths.TryGetValue(candidate, out string? cached))
			{
				resolved = cached;
				continue;
			}
			FileSystemInfo entry = Directory.Exists(candidate)
				? new DirectoryInfo(candidate) : new FileInfo(candidate);
			if (entry.LinkTarget is null)
			{
				resolved = OperatingSystem.IsWindows() && entry.Exists
					? ResolveWindowsEntry(candidate) : candidate;
			}
			else
			{
				if (activeLinks.Count >= 64 || !activeLinks.Add(candidate))
				{
					throw new IOException($"Cyclic or excessive linked path '{candidate}'.");
				}
				try
				{
					string target = entry.ResolveLinkTarget(returnFinalTarget: false)?.FullName
						?? throw new IOException($"Cannot resolve linked path '{candidate}'.");
					resolved = Resolve(target, resolvedPaths, activeLinks);
				}
				finally
				{
					activeLinks.Remove(candidate);
				}
			}
			resolvedPaths.Add(candidate, resolved);
		}
		return resolved;
	}

	private static string ResolveWindowsEntry(string path)
	{
		// Query metadata only, share all access, and never create an entry.
		// NT volume names also cover volume GUIDs, drive aliases and short names;
		// DOS/GUID final names can be unavailable on network or virtual drives.
		using SafeFileHandle handle = CreateFileW(path, 0, 7, 0, 3, 0x02000000, 0);
		if (handle.IsInvalid)
		{
			throw new IOException($"Cannot resolve filesystem path '{path}'.",
				new Win32Exception(Marshal.GetLastWin32Error()));
		}
		var buffer = new char[512];
		while (true)
		{
			uint length = GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Length, 2);
			if (length == 0)
			{
				throw new IOException($"Cannot resolve filesystem path '{path}'.",
					new Win32Exception(Marshal.GetLastWin32Error()));
			}
			if (length < buffer.Length)
			{
				// GLOBALROOT makes the returned NT name usable by subsequent
				// filesystem probes. Actual writes retain the caller's spelling.
				return @"\\?\GLOBALROOT" + new string(buffer, 0, (int)length);
			}
			if (length > 32768)
			{
				throw new PathTooLongException($"The resolved path '{path}' exceeds the Windows path limit.");
			}
			buffer = new char[(int)length];
		}
	}

	[DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
	private static extern SafeFileHandle CreateFileW(string path, uint desiredAccess, uint shareMode,
		nint securityAttributes, uint creationDisposition, uint flagsAndAttributes, nint templateFile);

	[DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
	private static extern uint GetFinalPathNameByHandleW(SafeFileHandle handle, [Out] char[] path,
		uint capacity, uint flags);
}
