// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Diagnostics;

namespace VeroScriptInstaller.Core.Platform;

public static class DefaultAppsSettings
{
	public static bool TryOpen(string registeredApplicationName)
	{
		if (!OperatingSystem.IsWindows()) return false;
		try
		{
			using var process = Process.Start(new ProcessStartInfo(BuildUri(registeredApplicationName)) { UseShellExecute = true });
			return true; // The page was requested; this does not mean a default was assigned.
		}
		catch (Exception exception) when (exception is System.ComponentModel.Win32Exception or InvalidOperationException
			or System.Security.SecurityException or NotSupportedException)
		{
			return false;
		}
	}

	internal static string BuildUri(string name) => "ms-settings:defaultapps?registeredAppUser=" + Uri.EscapeDataString(name);
}
