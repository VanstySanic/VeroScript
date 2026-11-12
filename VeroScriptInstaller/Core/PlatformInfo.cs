// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;

namespace VeroScriptInstaller.Core;

public static class PlatformInfo
{
	public static string CurrentPlatform =>
		OperatingSystem.IsWindows() ? "windows" :
		OperatingSystem.IsMacOS() ? "macos" :
		OperatingSystem.IsLinux() ? "linux" :
		"unknown";

	public static string CurrentArchitecture => RuntimeInformation.ProcessArchitecture switch
	{
		Architecture.X64 => "x64",
		Architecture.X86 => "x86",
		Architecture.Arm64 => "arm64",
		Architecture.Arm => "arm",
		_ => RuntimeInformation.ProcessArchitecture.ToString().ToLowerInvariant(),
	};

	public static string GetDefaultRuntimeInstallDirectory()
	{
		if (OperatingSystem.IsWindows())
		{
			return Path.Combine(
				Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
				"Programs", "VeroScript");
		}

		if (OperatingSystem.IsMacOS())
		{
			return Path.Combine(GetHomeDirectory(), "Applications", "VeroScript");
		}
		return Path.Combine(GetUserDataDirectory(), "VeroScript");
	}

	public static string GetDefaultSdkInstallDirectory(string version)
	{
		return Path.Combine(GetDefaultRuntimeInstallDirectory(), "SDK", "Core", version);
	}

	public static string GetDefaultInstallDirectory(InstallerRootDefinition root, string version)
	{
		ArgumentNullException.ThrowIfNull(root);
		var leaf = root.IncludeVersion
			? Path.Combine(root.DirectoryName, version)
			: root.DirectoryName;
		if (OperatingSystem.IsWindows())
		{
			return Path.Combine(
				Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
				"Programs", leaf);
		}
		if (OperatingSystem.IsMacOS())
		{
			return Path.Combine(GetHomeDirectory(), "Applications", leaf);
		}
		return Path.Combine(GetUserDataDirectory(), leaf);
	}

	public static string GetUserDataDirectory()
	{
		if (OperatingSystem.IsLinux())
		{
			var configured = Environment.GetEnvironmentVariable("XDG_DATA_HOME");
			if (!string.IsNullOrWhiteSpace(configured) && Path.IsPathFullyQualified(configured))
			{
				return Path.GetFullPath(configured);
			}
		}
		// Preserve the existing default installation location when XDG is absent.
		return Path.Combine(GetHomeDirectory(), ".local", "share");
	}

	private static string GetHomeDirectory()
	{
		var home = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
		if (string.IsNullOrWhiteSpace(home))
		{
			throw new InvalidOperationException("The current user's home directory is unavailable.");
		}
		return home;
	}
}
