// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller;

internal static class InstallerDiagnostics
{
	internal static string? WriteFailure(Exception exception)
	{
		try
		{
			var localData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
			if (string.IsNullOrWhiteSpace(localData))
			{
				return null;
			}

			var directory = Path.Combine(localData, "VeroScript", "Installer", "Logs");
			Directory.CreateDirectory(directory);
			var path = Path.Combine(
				directory,
				$"install-{DateTime.UtcNow:yyyyMMdd-HHmmss}-{Environment.ProcessId}.log");
			File.WriteAllText(path,
				$"UTC: {DateTime.UtcNow:O}{Environment.NewLine}"
				+ $"OS: {Environment.OSVersion}{Environment.NewLine}"
				+ $"Process: {Environment.ProcessPath}{Environment.NewLine}"
				+ $"Exception: {exception}{Environment.NewLine}");
			return path;
		}
		catch
		{
			// Diagnostics must never replace the original installation failure.
			return null;
		}
	}
}
