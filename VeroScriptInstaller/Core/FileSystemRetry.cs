// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller.Core;

internal static class FileSystemRetry
{
	private const int MaximumAttempts = 10;

	internal static Task MoveDirectoryAsync(
		string source,
		string destination,
		CancellationToken cancellationToken) => RetryAsync(
			() => Directory.Move(source, destination),
			cancellationToken);

	internal static Task DeleteDirectoryAsync(string path, CancellationToken cancellationToken) => RetryAsync(
		() => Directory.Delete(path, recursive: true),
		cancellationToken);

	internal static Task DeleteFileAsync(string path, CancellationToken cancellationToken) => RetryAsync(
		() => File.Delete(path),
		cancellationToken);

	internal static async Task TryDeleteDirectoryAsync(string path)
	{
		try
		{
			if (Directory.Exists(path))
			{
				await DeleteDirectoryAsync(path, CancellationToken.None);
			}
		}
		catch
		{
			// Cleanup after a successful commit is best effort. A uniquely named
			// stage or backup remains recoverable and cannot shadow live files.
		}
	}

	internal static async Task TryDeleteFileAsync(string path)
	{
		try
		{
			if (File.Exists(path))
			{
				await DeleteFileAsync(path, CancellationToken.None);
			}
		}
		catch
		{
			// Best-effort cleanup for metadata that never became authoritative.
		}
	}

	private static async Task RetryAsync(Action operation, CancellationToken cancellationToken)
	{
		for (var attempt = 1; ; ++attempt)
		{
			cancellationToken.ThrowIfCancellationRequested();
			try
			{
				operation();
				return;
			}
			catch (Exception exception) when (IsRetryable(exception, attempt))
			{
				await Task.Delay(Math.Min(100 * (1 << (attempt - 1)), 2_000), cancellationToken);
			}
		}
	}

	private static bool IsRetryable(Exception exception, int attempt) =>
		OperatingSystem.IsWindows()
		&& attempt < MaximumAttempts
		&& exception is IOException or UnauthorizedAccessException;
}
