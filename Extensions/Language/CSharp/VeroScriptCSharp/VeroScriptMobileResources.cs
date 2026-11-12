// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json;

namespace VeroScript;

/// <summary>Installs packaged source-library and localization resources into an app-owned directory.</summary>
public static class VeroScriptMobileResources
{
	/// <summary>Copies and verifies the resources packaged by the mobile SDK.</summary>
	/// <remarks>
	/// The asset provider receives names beginning with "veroscript/" and transfers ownership
	/// of each returned stream. MAUI hosts can use FileSystem.OpenAppPackageFileAsync.
	/// Use an absolute app-owned directory and complete installation before creating sessions.
	/// The manifest must include the exact, case-sensitive route "stdlib/core.vs".
	/// Failed installation can leave completed files; a retry verifies and replaces each file.
	/// </remarks>
	public static async Task<string> InstallAsync(string directory,
		Func<string, CancellationToken, Task<Stream>> openAsset,
		CancellationToken cancellationToken = default)
	{
		ArgumentException.ThrowIfNullOrWhiteSpace(directory);
		ArgumentNullException.ThrowIfNull(openAsset);
		if (!Path.IsPathFullyQualified(directory))
		{
			throw new ArgumentException("Mobile resource directory must be an absolute app-owned path.", nameof(directory));
		}
		string root = Path.GetFullPath(directory);
		await using Stream manifest = await openAsset("veroscript/resources.json", cancellationToken).ConfigureAwait(false);
		using JsonDocument document = await JsonDocument.ParseAsync(manifest,
			cancellationToken: cancellationToken).ConfigureAwait(false);
		if (document.RootElement.GetProperty("schemaVersion").GetInt32() != 1)
		{
			throw new InvalidDataException("Unsupported mobile resource manifest version.");
		}
		var files = new List<(string Path, string Hash)>();
		var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
		foreach (JsonProperty entry in document.RootElement.GetProperty("files").EnumerateObject())
		{
			string route = entry.Name;
			string? hash = entry.Value.GetString();
			if ((!route.StartsWith("stdlib/", StringComparison.Ordinal) && !route.StartsWith("localization/", StringComparison.Ordinal))
				|| !route.All(character => char.IsAsciiLetterOrDigit(character) || character is '/' or '_' or '-' or '.')
				|| route.Split('/').Any(part => part is "" or "." or ".." || part.EndsWith('.'))
				|| !paths.Add(route) || hash is null || hash.Length != 64 || !hash.All(Uri.IsHexDigit))
			{
				throw new InvalidDataException("Invalid mobile resource path or digest.");
			}
			files.Add((route, hash));
		}
		if (!files.Any(file => file.Path.Equals("stdlib/core.vs", StringComparison.Ordinal)))
		{
			throw new InvalidDataException("Mobile resource manifest is missing stdlib/core.vs.");
		}
		foreach ((string route, string expectedHash) in files)
		{
			cancellationToken.ThrowIfCancellationRequested();
			string target = Path.Combine(root, route.Replace('/', Path.DirectorySeparatorChar));
			Directory.CreateDirectory(Path.GetDirectoryName(target)!);
			string temporary = target + "." + Guid.NewGuid().ToString("N") + ".tmp";
			try
			{
				{
					await using Stream input = await openAsset("veroscript/" + route, cancellationToken).ConfigureAwait(false);
					await using var output = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write, FileShare.None,
						81920, FileOptions.Asynchronous);
					await input.CopyToAsync(output, cancellationToken).ConfigureAwait(false);
				}
				await using (var input = new FileStream(temporary, FileMode.Open, FileAccess.Read, FileShare.Read,
					81920, FileOptions.Asynchronous))
				{
					byte[] hash = await SHA256.HashDataAsync(input, cancellationToken).ConfigureAwait(false);
					if (!Convert.ToHexString(hash).Equals(expectedHash, StringComparison.OrdinalIgnoreCase))
					{
						throw new InvalidDataException("Mobile resource digest mismatch: " + route);
					}
				}
				cancellationToken.ThrowIfCancellationRequested();
				File.Move(temporary, target, overwrite: true);
			}
			finally
			{
				File.Delete(temporary);
			}
		}
		return root;
	}
}
