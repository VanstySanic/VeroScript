// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

namespace VeroScriptInstaller.Core;

/// <summary>Reads complete license notices embedded in the installer assembly.</summary>
public static class ProjectLicense
{
	/// <summary>Returns the embedded project MIT notice.</summary>
	public static string Read()
	{
		return ReadResource("VeroScriptInstaller.LICENSE");
	}

	/// <summary>Returns the embedded third-party license notices.</summary>
	public static string ReadThirdPartyNotices()
	{
		return ReadResource("VeroScriptInstaller.THIRD-PARTY-NOTICES");
	}

	private static string ReadResource(string resourceName)
	{
		using var stream = typeof(ProjectLicense).Assembly.GetManifestResourceStream(resourceName)
			?? throw new InvalidOperationException($"The embedded license notice is missing: {resourceName}");
		using var reader = new StreamReader(stream);
		return reader.ReadToEnd();
	}
}
