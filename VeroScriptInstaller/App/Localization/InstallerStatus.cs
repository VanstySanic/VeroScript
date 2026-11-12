// SPDX-FileCopyrightText: VanstySanic
// SPDX-License-Identifier: MIT

using VeroScriptInstaller.Core;

namespace VeroScriptInstaller.Localization;

// Store meaning, not rendered text, so results and failures survive language switches.
internal sealed record InstallerStatus(string Key, params object[] Arguments)
{
	internal string Render(InstallerLocalizer localizer) => string.Format(
		localizer[Key],
		Arguments.Select(value => value is InstallerStatus message
			? message.Render(localizer) : value).ToArray());

	internal static InstallerStatus Failure(Exception exception, string? diagnosticPath)
	{
		var reason = InstallerErrors.GetCode(exception) is { } code
			? "Failure" + code
			: exception switch
			{
				UnauthorizedAccessException => "FailurePermission",
				FileNotFoundException or DirectoryNotFoundException => "FailureMissing",
				InvalidDataException or System.Text.Json.JsonException => "FailurePackage",
				PlatformNotSupportedException => "FailureWrongPlatform",
				IOException when OperatingSystem.IsWindows()
					&& (exception.HResult & 0xffff) is 32 or 33 => "FailureFileInUse",
				IOException when OperatingSystem.IsWindows()
					&& (exception.HResult & 0xffff) is 39 or 112 => "FailureDiskFull",
				IOException => "FailureIO",
				ArgumentException => "FailureInput",
				_ => "FailureUnknown",
			};
		return new InstallerStatus("FailureWithDetails",
			new InstallerStatus(reason),
			diagnosticPath is null
				? new InstallerStatus("FailureNoLog", exception.Message)
				: new InstallerStatus("DiagnosticLog", diagnosticPath));
	}
}
