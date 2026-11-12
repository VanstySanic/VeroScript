// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Serialization;

namespace VeroScriptInstaller.Core;

internal static class InstallerJson
{
	private const long MaximumJsonBytes = 128L * 1024 * 1024;
	internal static readonly JsonSerializerOptions Options = new()
	{
		PropertyNameCaseInsensitive = false,
		ReadCommentHandling = JsonCommentHandling.Disallow,
		AllowTrailingCommas = false,
		WriteIndented = true,
		DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
		UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
	};

	internal static async Task<T> ReadAsync<T>(string path, CancellationToken cancellationToken)
	{
		var info = new FileInfo(path);
		if (!info.Exists || info.Length > MaximumJsonBytes)
		{
			throw new InvalidDataException($"JSON document is missing or too large: {path}");
		}
		await using var stream = new FileStream(
			path, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024,
			FileOptions.Asynchronous | FileOptions.SequentialScan);
		var value = await JsonSerializer.DeserializeAsync<T>(stream, Options, cancellationToken);
		return value ?? throw new InvalidDataException($"JSON document is empty: {path}");
	}

	internal static async Task WriteAsync<T>(string path, T value, CancellationToken cancellationToken)
	{
		await using var stream = new FileStream(
			path, FileMode.CreateNew, FileAccess.Write, FileShare.None, 64 * 1024,
			FileOptions.Asynchronous | FileOptions.WriteThrough);
		await JsonSerializer.SerializeAsync(stream, value, Options, cancellationToken);
		await stream.FlushAsync(cancellationToken);
	}

	internal static async Task WriteAtomicAsync<T>(string path, T value, CancellationToken cancellationToken)
	{
		var fullPath = Path.GetFullPath(path);
		Directory.CreateDirectory(Path.GetDirectoryName(fullPath)!);
		var temporary = fullPath + ".new-" + Guid.NewGuid().ToString("N");
		try
		{
			await WriteAsync(temporary, value, cancellationToken);
			File.Move(temporary, fullPath, overwrite: true);
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
